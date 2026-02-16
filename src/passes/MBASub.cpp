#include <cstddef>
#include <cstdint>
#include <functional>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Value.h>

#include <passes/MBASub.hpp>
#include <core/ZyroxMetaData.h>
#include <quickjs/QuickConfig.h>
#include <utils/Random.h>

using namespace llvm;

void MBASub::RunOnFunction(Function &f, ZyroxPassOptions *options)
{
    int iterations_count = options->Get("PassIterations");

    for (int i = 0; i < iterations_count; i++)
    {
        ObfuscateFunction(f);
    }
}

void MBASub::RegisterFromAnnotation(Function &f, ZyroxAnnotationArgs *args)
{
    ZyroxMetaDataKV kv = {
        {"PassIterations", args->NextOrDefault(1)},
    };
    ZyroxPassesMetadata::AddPass(f, pass_info.CodeName, kv);
}

void MBASub::ObfuscateFunction(Function &func)
{
    for (BasicBlock &bb : func)
    {
        RunOnBasicBlock(bb);
    }
}

void MBASub::RunOnBasicBlock(BasicBlock &bb)
{
    runOnMul(bb);
    runOnSub(bb);
    runOnAdd(bb);
    runOnXor(bb);
    runOnOr(bb);
}

typedef std::function<Value *(IRBuilder<> *, BinaryOperator *)> Callback;

#define INT(type, val) ConstantInt::getSigned(Type::get##type##Ty(context), val)

Callback sub_ops[] = {
    [](IRBuilder<> *builder, BinaryOperator *operation) -> Value *
    {
        // X - Y == (X ^ -Y) + 2*(X & -Y)

        Type *type = operation->getOperand(0)->getType();

        return builder->CreateAdd(
            builder->CreateXor(operation->getOperand(0),
                               builder->CreateNeg(operation->getOperand(1))),
            builder->CreateMul(
                ConstantInt::getSigned(type, 2),
                builder->CreateAnd(
                    operation->getOperand(0),
                    builder->CreateNeg(operation->getOperand(1)))));
    },
};

Callback add_ops[] = {
    [](IRBuilder<> *builder, BinaryOperator *operation) -> Value *
    {
        // x + y = (~ (x +
        //        ((- x) +
        //            ((- x) + (~ y))
        //      )
        //  ))
        return builder->CreateNot(builder->CreateAdd(
            operation->getOperand(0),
            builder->CreateAdd(
                builder->CreateNeg(operation->getOperand(0)),
                builder->CreateAdd(
                    builder->CreateNeg(operation->getOperand(0)),
                    builder->CreateNot(operation->getOperand(1))))));
    },
    [](IRBuilder<> *builder, BinaryOperator *operation) -> Value *
    {
        // r = rand(); c = b + r; a = a + c; a = a - r
        Value *a = operation->getOperand(0);
        Value *b = operation->getOperand(1);

        Constant *r =
            ConstantInt::get(operation->getOperand(0)->getType(),
                             Random::IntRanged<uint64_t>(0, UINT64_MAX - 1));
        Value *c = builder->CreateAdd(b, r);
        a = builder->CreateAdd(a, c);
        return builder->CreateSub(a, r);
    },
};

Callback xor_ops[] = {
    [](IRBuilder<> *builder, BinaryOperator *operation) -> Value *
    {
        // a ^ b = (~a & b) | (a & ~b)
        Value *a = operation->getOperand(0);
        Value *b = operation->getOperand(1);
        return builder->CreateOr(builder->CreateAnd(builder->CreateNot(a), b),
                                 builder->CreateAnd(a, builder->CreateNot(b)));
    },
    [](IRBuilder<> *builder, BinaryOperator *operation) -> Value *
    {
        // a ^ b = (a | b) & ~(a & b)
        Value *a = operation->getOperand(0);
        Value *b = operation->getOperand(1);
        return builder->CreateAnd(builder->CreateOr(a, b),
                                  builder->CreateNot(builder->CreateAnd(a, b)));
    },
    [](IRBuilder<> *builder, BinaryOperator *operation) -> Value *
    {
        // a ^ b = (a + b) - 2 * (a & b)
        Value *a = operation->getOperand(0);
        Value *b = operation->getOperand(1);
        return builder->CreateSub(
            builder->CreateAdd(a, b),
            builder->CreateMul(ConstantInt::get(a->getType(), 2),
                               builder->CreateAnd(a, b)));
    },
    [](IRBuilder<> *builder, BinaryOperator *operation) -> Value *
    {
        // a ^ b = ~(~a & ~b) & ~(a & b)
        Value *a = operation->getOperand(0);
        Value *b = operation->getOperand(1);
        return builder->CreateAnd(
            builder->CreateNot(builder->CreateAnd(builder->CreateNot(a),
                                                  builder->CreateNot(b))),
            builder->CreateNot(builder->CreateAnd(a, b)));
    },
};

Callback mul_ops[] = {
    [](IRBuilder<> *builder, BinaryOperator *operation) -> Value *
    {
        //  b * c = (((b | c) * (b & c)) + ((b & ~c) * (c &  ~b)))
        Value *b = operation->getOperand(0);
        Value *c = operation->getOperand(1);
        return builder->CreateAdd(
            builder->CreateMul(builder->CreateOr(b, c),
                               builder->CreateAnd(b, c)),
            builder->CreateMul(builder->CreateAnd(b, builder->CreateNot(c)),
                               builder->CreateAnd(c, builder->CreateNot(b))));
    },
};

Callback or_ops[] = {
    [](IRBuilder<> *builder, BinaryOperator *operation) -> Value *
    {
        // a | b = ~(~a & ~b)
        Value *a = operation->getOperand(0);
        Value *b = operation->getOperand(1);
        return builder->CreateNot(
            builder->CreateAnd(builder->CreateNot(a), builder->CreateNot(b)));
    },
    [](IRBuilder<> *builder, BinaryOperator *operation) -> Value *
    {
        // a | b = a ^ b ^ (a & b)
        Value *a = operation->getOperand(0);
        Value *b = operation->getOperand(1);
        return builder->CreateXor(
            a, builder->CreateXor(b, builder->CreateAnd(a, b)));
    },
    [](IRBuilder<> *builder, BinaryOperator *operation) -> Value *
    {
        // a | b = (a + b) - (a & b)
        Value *a = operation->getOperand(0);
        Value *b = operation->getOperand(1);
        return builder->CreateSub(builder->CreateAdd(a, b),
                                  builder->CreateAnd(a, b));
    },
};

#define DEFINE_RUN(Op, Callbacks)                                              \
    Value *MBASub::Obfuscate##Op(IRBuilder<> &Builder, BinaryOperator *BinOp)  \
    {                                                                          \
        if (BinOp->getOpcode() != Instruction::Op)                             \
            return NULL;                                                       \
        return Callbacks[Random::IntRanged<size_t>(                            \
            0, sizeof(Callbacks) / sizeof(Callbacks[0]) - 1)](&Builder,        \
                                                              BinOp);          \
    }                                                                          \
                                                                               \
    void MBASub::runOn##Op(BasicBlock &BB)                                     \
    {                                                                          \
        std::vector<Instruction *> instructions;                               \
                                                                               \
        for (Instruction &Instr : BB)                                          \
        {                                                                      \
            if (Instr.getOpcode() == Instruction::Op)                          \
                instructions.push_back(&Instr);                                \
        }                                                                      \
                                                                               \
        for (auto &Instr : instructions)                                       \
        {                                                                      \
            BinaryOperator *BinOp = (BinaryOperator *)Instr;                   \
            IRBuilder<> Builder(Instr);                                        \
            Instr->replaceAllUsesWith(Obfuscate##Op(Builder, BinOp));          \
        }                                                                      \
    }

DEFINE_RUN(Sub, sub_ops)
DEFINE_RUN(Add, add_ops)
DEFINE_RUN(Xor, xor_ops)
DEFINE_RUN(Mul, mul_ops)
DEFINE_RUN(Or, or_ops)

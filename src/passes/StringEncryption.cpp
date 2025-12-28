#include <passes/BasicBlockSplitter.h>
#include <passes/IndirectBranch.h>
#include <passes/MBASub.hpp>
#include <passes/SimpleIndirectBranch.h>
#include <passes/StringEncryption.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>
#include <quickjs/QuickRt.h>
#include <string>
#include <utility>
#include <utils/Logger.h>
#include <utils/Random.h>
#include <vector>

uint32_t SplitMix32(uint32_t &state)
{
    state += 0x9E3779B9u;
    uint32_t z = state;
    z ^= z >> 16;
    z *= 0x85EBCA6Bu;
    z ^= z >> 13;
    z *= 0xC2B2AE35u;
    z ^= z >> 16;
    return z;
}

void XorEncryptStrings(std::vector<std::string> &strings, uint32_t master_seed)
{
    for (size_t i = 0; i < strings.size(); ++i)
    {
        uint32_t seed = master_seed ^ i;
        uint32_t state = seed;
        std::string &s = strings[i];
        size_t len = s.size();
        size_t offset = 0;
        while (offset < len)
        {
            uint32_t key_stream = SplitMix32(state);
            size_t chunk = std::min(len - offset, sizeof(uint32_t));
            for (size_t j = 0; j < chunk; ++j)
            {
                s[offset + j] ^= ((key_stream >> (j * 8)) & 0xFF);
            }
            offset += chunk;
        }
    }
}

std::pair<Value *, Value *> BuilderSplitMix32(IRBuilderBase &builder,
                                              Value *state)
{
    Value *c_add = builder.getInt32(0x9E3779B9);
    Value *c_mul1 = builder.getInt32(0x85EBCA6B);
    Value *c_mul2 = builder.getInt32(0xC2B2AE35);
    Value *new_state = builder.CreateAdd(state, c_add, "state.next");
    Value *z = new_state;
    Value *z_sr16 = builder.CreateLShr(z, builder.getInt32(16));
    z = builder.CreateXor(z, z_sr16);
    z = builder.CreateMul(z, c_mul1);
    Value *z_sr13 = builder.CreateLShr(z, builder.getInt32(13));
    z = builder.CreateXor(z, z_sr13);
    z = builder.CreateMul(z, c_mul2);
    z_sr16 = builder.CreateLShr(z, builder.getInt32(16));
    z = builder.CreateXor(z, z_sr16);
    return {new_state, z};
}

struct DBKVMap
{
    AllocaInst *off_var;
    AllocaInst *state_var;
    AllocaInst *j_var;
};

// used to reduce stack allocations per function
std::unordered_map<std::string, DBKVMap> map;

// ;)
static void EmitDecryptBuffer(IRBuilderBase &builder, Value *state_seed,
                              Value *in_ptr, Value *out_ptr, Value *str_len)
{
    LLVMContext &ctx = builder.getContext();
    IntegerType *i32 = Type::getInt32Ty(ctx);
    IntegerType *i8 = Type::getInt8Ty(ctx);

    BasicBlock *entry_bb = builder.GetInsertBlock();
    Function *f = entry_bb->getParent();
    std::string fn_name = f->getName().str();

    AllocaInst *off_var, *state_var, *j_var;

    if (!map.contains(fn_name))
    {
        IRBuilderBase::InsertPoint save_ip = builder.saveIP();
        builder.SetInsertPoint(&*f->getEntryBlock().getFirstInsertionPt());
        off_var = builder.CreateAlloca(i32, nullptr, "dec.offset.addr");
        state_var = builder.CreateAlloca(i32, nullptr, "dec.state.addr");
        j_var = builder.CreateAlloca(i32, nullptr, "dec.j.addr");
        builder.restoreIP(save_ip);
        map[fn_name] = {
            .off_var = off_var,
            .state_var = state_var,
            .j_var = j_var,
        };
    }
    else
    {
        DBKVMap kv = map[fn_name];
        off_var = kv.off_var;
        state_var = kv.state_var;
        j_var = kv.j_var;
    }

    builder.CreateStore(ConstantInt::get(i32, 0), off_var, true);
    builder.CreateStore(state_seed, state_var, true);

    BasicBlock *loop_off_bb = BasicBlock::Create(ctx, "dec.loop.off", f);
    BasicBlock *body_off_bb = BasicBlock::Create(ctx, "dec.body.off", f);
    BasicBlock *after_off_bb = BasicBlock::Create(ctx, "dec.after.off", f);

    builder.CreateBr(loop_off_bb);
    builder.SetInsertPoint(loop_off_bb);

    Value *current_off = builder.CreateLoad(i32, off_var, true, "dec.offset");
    Value *current_state =
        builder.CreateLoad(i32, state_var, true, "dec.state");

    Value *cmp_off = builder.CreateICmpULT(current_off, str_len, "dec.cmp.off");
    builder.CreateCondBr(cmp_off, body_off_bb, after_off_bb);

    builder.SetInsertPoint(body_off_bb);
    auto [new_state, key_stream] = BuilderSplitMix32(builder, current_state);
    Value *rem = builder.CreateSub(str_len, current_off, "dec.rem");
    Value *c4 = ConstantInt::get(i32, 4);
    Value *chunk = builder.CreateSelect(builder.CreateICmpULT(rem, c4), rem, c4,
                                        "dec.chunk");

    BasicBlock *loop_j_bb = BasicBlock::Create(ctx, "dec.loop.j", f);
    BasicBlock *body_j_bb = BasicBlock::Create(ctx, "dec.body.j", f);
    BasicBlock *after_j_bb = BasicBlock::Create(ctx, "dec.after.j", f);

    builder.CreateStore(ConstantInt::get(i32, 0), j_var, true);
    builder.CreateBr(loop_j_bb);

    builder.SetInsertPoint(loop_j_bb);
    Value *current_j = builder.CreateLoad(i32, j_var, true, "dec.j");
    Value *cmp_j = builder.CreateICmpULT(current_j, chunk, "dec.cmp.j");
    builder.CreateCondBr(cmp_j, body_j_bb, after_j_bb);

    builder.SetInsertPoint(body_j_bb);
    Value *off_plus_j = builder.CreateAdd(current_off, current_j, "dec.off.j");
    Value *in_byte =
        builder.CreateInBoundsGEP(i8, in_ptr, off_plus_j, "dec.in");
    Value *orig = builder.CreateLoad(i8, in_byte, true, "dec.orig");
    Value *shift =
        builder.CreateMul(current_j, ConstantInt::get(i32, 8), "j_x_8");
    Value *shift_32 = builder.CreateTrunc(shift, i32, "shift32");
    Value *shr = builder.CreateLShr(key_stream, shift_32, "shr");
    Value *mask = builder.CreateTrunc(shr, i8, "mask");
    Value *out = builder.CreateXor(orig, mask, "xor");
    Value *out_byte =
        builder.CreateInBoundsGEP(i8, out_ptr, off_plus_j, "dec.out");
    builder.CreateStore(out, out_byte, true);

    Value *j_next =
        builder.CreateAdd(current_j, ConstantInt::get(i32, 1), "dec.j.next");
    builder.CreateStore(j_next, j_var, true);
    builder.CreateBr(loop_j_bb);

    builder.SetInsertPoint(after_j_bb);
    Value *off_next = builder.CreateAdd(current_off, chunk, "dec.off.next");
    builder.CreateStore(off_next, off_var, true);
    builder.CreateStore(new_state, state_var, true);
    builder.CreateBr(loop_off_bb);

    builder.SetInsertPoint(after_off_bb);
}

void StringEncryption::ObfuscateGlobalArrayStrings(Module &m)
{
    LLVMContext &ctx = m.getContext();
    IntegerType *i32 = Type::getInt32Ty(ctx);
    Type *i8_ptr = PointerType::getUnqual(Type::getInt8Ty(ctx));

    std::vector<GlobalVariable *> gv_list;
    std::vector<std::string> raw_strings;
    std::vector<Constant *> ptr_list;
    std::vector<Constant *> len_list;

    std::vector<std::pair<GlobalVariable *, std::string>> stack_list;

    // JSValue* OnString()

    std::optional<JSValue> on_string_v = QuickRt::GetFunction("OnString");
    if (!on_string_v.has_value())
    {
        Logger::Warn(
            "OnString function not found, skipping StringEncryption pass");
        return;
    }

    JSValue on_string = on_string_v.value();
    JSValue js_z_this = QuickRt::ConfigClass();
    JSContext *js_ctx = QuickRt::JSContext();

    for (GlobalVariable &gv : m.globals())
    {
        if (!gv.hasInitializer())
            continue;
        Constant *init = gv.getInitializer();
        ConstantDataArray *arr = dyn_cast<ConstantDataArray>(init);
        if (!arr || !arr->isString() || !isa<ArrayType>(arr->getType()))
            continue;
        if (StringRef name = gv.getName(); name.starts_with("llvm."))
            continue;
        if (gv.hasSection() &&
            (StringRef(gv.getSection()).starts_with("debug") ||
             StringRef(gv.getSection()).starts_with("llvm")))
            continue;

        std::string raw = arr->getAsString().str();
        JSValue js_str = JS_NewString(js_ctx, raw.c_str());
        JSValue args[] = {js_str};
        JSValue rv = JS_Call(js_ctx, on_string, js_z_this, 1, args);
        JS_FreeValue(js_ctx, js_str);
        int option = 0;
        if (!JS_IsUndefined(rv))
        {
            if (JS_IsException(rv))
            {
                JSValue exc = JS_GetException(js_ctx);
                JSValue str = JS_ToString(js_ctx, exc);
                const char *ptr = JS_ToCString(js_ctx, str);
                Logger::Error("OnString returned an exception: {}", ptr);
            }

            JS_ToInt32(js_ctx, &option, rv);
            JS_FreeValue(js_ctx, rv);
        }

        if (bool starts_by_stack = raw.starts_with("/stack:");
            starts_by_stack || option == 1)
        {
            bool is_valid = true;
            for (Use &use : gv.uses())
            {
                Value *user = use.getUser();

                // Unwrap constant expressions
                while (auto *ce = dyn_cast<ConstantExpr>(user))
                {
                    // ConstantExpr can be used inside instructions
                    for (User *ce_user : ce->users())
                    {
                        if (auto *inst = dyn_cast<Instruction>(ce_user))
                        {
                            if (!inst->getFunction())
                            {
                                is_valid = false;
                                break;
                            }
                        }
                        else
                        {
                            is_valid = false;
                            break;
                        }
                    }
                    // No instruction uses found
                    if (!is_valid)
                        break;
                    goto next_use;
                }

                if (auto *inst = dyn_cast<Instruction>(user))
                {
                    if (!inst->getFunction())
                    {
                        is_valid = false;
                        break;
                    }
                }
                else
                {
                    is_valid = false;
                    break;
                }

            next_use:;
            }
            if (!is_valid)
            {
                Logger::Warn("string can't be encrypted on stack: '{}', it "
                             "has uses outside a function",
                             raw);

                continue;
            }
            stack_list.push_back({&gv, starts_by_stack ? raw.substr(7) : raw});
        }
        else if (option == 2)
        {
            uint64_t str_len = arr->getType()->getNumElements();

            gv_list.push_back(&gv);
            raw_strings.push_back(raw);

            Constant *ptr = ConstantExpr::getBitCast(&gv, i8_ptr);
            ptr_list.push_back(ptr);
            len_list.push_back(ConstantInt::get(i32, str_len));
        }
    }

    JS_FreeValue(js_ctx, on_string);

    if (!gv_list.empty())
    {
        uint32_t master_seed = Random::UInt32();
        XorEncryptStrings(raw_strings, master_seed);

        for (size_t i = 0; i < gv_list.size(); ++i)
        {
            GlobalVariable *gv = gv_list[i];
            const std::string &enc_str = raw_strings[i];
            Constant *enc_init =
                ConstantDataArray::getString(ctx, enc_str, false);
            gv->setInitializer(enc_init);
            gv->setConstant(false);
        }

        ArrayType *ptr_arr_ty = ArrayType::get(i8_ptr, ptr_list.size());
        Constant *ptr_arr_init = ConstantArray::get(ptr_arr_ty, ptr_list);
        auto *ptr_table = new GlobalVariable(m, ptr_arr_ty, false,
                                             GlobalValue::InternalLinkage,
                                             ptr_arr_init, "__enc_ptr_table");

        ArrayType *len_arr_ty = ArrayType::get(i32, len_list.size());
        Constant *len_arr_init = ConstantArray::get(len_arr_ty, len_list);
        auto *len_table = new GlobalVariable(m, len_arr_ty, false,
                                             GlobalValue::InternalLinkage,
                                             len_arr_init, "__enc_len_table");

        FunctionType *fn_ty = FunctionType::get(Type::getVoidTy(ctx), false);
        Function *decrypt_fn = Function::Create(
            fn_ty, GlobalValue::InternalLinkage, "__decrypt_ctor", &m);
        BasicBlock *entry = BasicBlock::Create(ctx, "entry", decrypt_fn);
        IRBuilder builder(entry);

        Value *num_strings = ConstantInt::get(i32, gv_list.size());
        BasicBlock *loop_header =
            BasicBlock::Create(ctx, "master.loop.header", decrypt_fn);
        BasicBlock *loop_body =
            BasicBlock::Create(ctx, "master.loop.body", decrypt_fn);
        BasicBlock *loop_exit =
            BasicBlock::Create(ctx, "master.loop.exit", decrypt_fn);

        AllocaInst *master_var = builder.CreateAlloca(i32);
        builder.CreateStore(builder.getInt32(0), master_var);

        builder.CreateBr(loop_header);
        builder.SetInsertPoint(loop_header);

        Value *master_val = builder.CreateLoad(i32, master_var);
        Value *cmp =
            builder.CreateICmpULT(master_val, num_strings, "master.cmp");
        builder.CreateCondBr(cmp, loop_body, loop_exit);

        builder.SetInsertPoint(loop_body);

        Value *str_ptr = builder.CreateLoad(
            i8_ptr,
            builder.CreateInBoundsGEP(ptr_arr_ty, ptr_table,
                                      {builder.getInt64(0), master_val}));
        Value *str_len = builder.CreateLoad(
            i32, builder.CreateInBoundsGEP(len_arr_ty, len_table,
                                           {builder.getInt64(0), master_val}));

        Value *master_seed_val = ConstantInt::get(i32, master_seed);
        Value *state_seed =
            builder.CreateXor(master_seed_val, master_val, "state.seed");

        EmitDecryptBuffer(builder, state_seed, str_ptr, str_ptr, str_len);

        builder.CreateStore(builder.CreateAdd(master_val, builder.getInt32(1),
                                              "master.idx.next"),
                            master_var);
        builder.CreateBr(loop_header);

        builder.SetInsertPoint(loop_exit);
        builder.CreateRetVoid();

        appendToGlobalCtors(m, decrypt_fn, 0);
        ZyroxAnnotationArgs args({1});
        MBASub::RegisterFromAnnotation(*decrypt_fn, &args);
        args = ZyroxAnnotationArgs({1, 20, 30, 70});
        BasicBlockSplitter::RegisterFromAnnotation(*decrypt_fn, &args);
        args = ZyroxAnnotationArgs({1, 100});
        IndirectBranch::RegisterFromAnnotation(*decrypt_fn, &args);
        args = ZyroxAnnotationArgs({1, 100});
        SimpleIndirectBranch::RegisterFromAnnotation(*decrypt_fn, &args);
    }

    for (auto [gv, stripped] : stack_list)
    {
        Logger::Info("encrypting {} on stack", stripped);
        uint32_t master_seed = Random::UInt32();

        std::vector strings = {stripped};
        XorEncryptStrings(strings, master_seed);
        stripped = strings[0];

        Constant *new_const =
            ConstantDataArray::getString(ctx, stripped, false);
        GlobalVariable *new_gv = new GlobalVariable(
            m, new_const->getType(), false, GlobalValue::PrivateLinkage,
            new_const, "", nullptr, GlobalValue::NotThreadLocal, 0);
        new_gv->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
        new_gv->setAlignment(Align(1));

        std::vector<Use *> uses_to_replace;
        for (Use &use : gv->uses())
            uses_to_replace.push_back(&use);

        for (Use *use_ptr : uses_to_replace)
        {
            Value *user = use_ptr->getUser();
            if (auto *ce = dyn_cast<ConstantExpr>(user))
            {
                if (ce->getOpcode() == Instruction::GetElementPtr)
                {
                    for (User *cei_user : ce->users())
                    {
                        if (Instruction *inst_user =
                                dyn_cast<Instruction>(cei_user))
                        {
                            IRBuilder<> b(inst_user);
                            auto *gep =
                                cast<GetElementPtrInst>(ce->getAsInstruction());
                            b.Insert(gep);
                            inst_user->replaceUsesOfWith(ce, gep);
                            gep->setName("gep_str");
                        }
                    }
                }
                continue;
            }

            Instruction *user_inst = dyn_cast<Instruction>(user);
            if (!user_inst || !user_inst->getFunction())
                continue;

            IRBuilder builder(&*user_inst->getFunction()
                                    ->getEntryBlock()
                                    .getFirstInsertionPt());

            int size = new_const->getType()->getArrayNumElements();

            AllocaInst *alloca =
                builder.CreateAlloca(ArrayType::get(Type::getInt8Ty(ctx), size),
                                     nullptr, "str_stack");
            alloca->setAlignment(Align(4));

            builder.SetInsertPoint(user_inst);
            BasicBlock *original = user_inst->getParent();
            BasicBlock *split = original->splitBasicBlock(user_inst);
            Instruction *term = original->getTerminator();

            builder.SetInsertPoint(term);

            Value *alloca_cast = builder.CreateBitCast(alloca, i8_ptr);
            Value *src_cast = ConstantExpr::getBitCast(new_gv, i8_ptr);

            builder.CreateMemCpy(alloca_cast, Align(1), src_cast, Align(1),
                                 ConstantInt::get(Type::getInt64Ty(ctx), size));

            Value *first_elem = builder.CreateInBoundsGEP(
                alloca->getAllocatedType(), alloca,
                {builder.getInt32(0), builder.getInt32(0)});

            EmitDecryptBuffer(builder, builder.getInt32(master_seed),
                              first_elem, first_elem, builder.getInt32(size));

            builder.CreateBr(split);
            term->eraseFromParent();

            use_ptr->set(first_elem);
        }
        gv->eraseFromParent();
    }
}

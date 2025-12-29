#ifndef QUICK_CONFIG_H
#define QUICK_CONFIG_H

#include "quickjs.h"
#include <llvm/IR/Module.h>

using namespace llvm;

class QuickConfig
{
  public:
    static void RegisterPasses(Module &m);

    static void RegisterFunctionPass(int obfuscation_type, JSValue obj);
};

#endif
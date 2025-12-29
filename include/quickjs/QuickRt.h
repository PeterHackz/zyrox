#ifndef QUICK_RT_H
#define QUICK_RT_H

#include "quickjs.h"
#include <optional>

class QuickRt
{
    static JSContext *ctx;
    static JSRuntime *rt;

    static JSValue config_class;

  public:
    static void InitZyroxRuntime();

    static std::optional<JSValue> GetFunction(const char *name);

    static JSValue ConfigClass();

    static void SetConfigClass(JSValue cls);

    static JSContext *JSContext();

    static void DestroyInstance();
};

#endif
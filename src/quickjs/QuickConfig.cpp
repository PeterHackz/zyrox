#include <passes/MBASub.hpp>
#include <core/ZyroxMetaData.h>
#include <core/ZyroxPassOptions.h>
#include <llvm/Demangle/Demangle.h>
#include <llvm/IR/Function.h>
#include <optional>
#include <quickjs/QuickConfig.h>
#include <quickjs/QuickRt.h>
#include <utils/Logger.h>

void JsFreeValues(JSContext *ctx, JSValue *argv, int argc)
{
    for (int i = 0; i < argc; i++)
    {
        JS_FreeValue(ctx, argv[i]);
    }
}

Function *current_function = nullptr;

void QuickConfig::RegisterFunctionPass(int obfuscation_type, JSValue obj)
{
    if (obfuscation_type < 0 || obfuscation_type >= zyrox_passes.size())
    {
        Logger::Error("invalid obfuscation type for {}: {}",
                      demangle(current_function->getName()).data(),
                      obfuscation_type);
    }

    ZyroxFunctionPass function_pass = zyrox_passes[obfuscation_type];

    JSContext *qjs_ctx = QuickRt::JSContext();

    JSValue value = JS_GetPropertyStr(qjs_ctx, obj, "PassIterations");
    int iterations_count = 0;
    if (!JS_IsUndefined(value))
    {
        const char *chars = JS_ToCString(qjs_ctx, value);
        std::string result(chars);
        iterations_count = std::stoi(result);
        JS_FreeCString(qjs_ctx, chars);
        JS_FreeValue(qjs_ctx, value);
    }

    if (iterations_count <= 0)
    {
        Logger::Warn("QuickConfig::RegisterFunctionPass: PassIterationsCount "
                     "is 0, ignoring pass {} on {}, PassIterations={} <= 0",
                     function_pass.Name,
                     demangle(current_function->getName()).data(),
                     iterations_count);
        return;
    }

    JSPropertyEnum *props;
    uint32_t len;

    if (JS_GetOwnPropertyNames(qjs_ctx, &props, &len, obj,
                               JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0)
        return;

    ZyroxMetaDataKV kv = {};

    for (uint32_t i = 0; i < len; i++)
    {
        JSAtom atom = props[i].atom;
        JSValue key = JS_AtomToValue(qjs_ctx, atom);
        JSValue val = JS_GetProperty(qjs_ctx, obj, atom);

        const char *key_str = JS_ToCString(qjs_ctx, key);
        const char *val_str = JS_ToCString(qjs_ctx, val);

        kv.push_back({key_str, std::atoi(val_str)});

        JS_FreeCString(qjs_ctx, key_str);
        JS_FreeCString(qjs_ctx, val_str);
        JS_FreeValue(qjs_ctx, key);
        JS_FreeValue(qjs_ctx, val);
        JS_FreeAtom(qjs_ctx, atom);
    }

    js_free(qjs_ctx, props);

    ZyroxPassesMetadata::AddPass(*current_function, function_pass.CodeName, kv);
}

void QuickConfig::RegisterPasses(Module &m)
{
    std::optional<JSValue> run_on_function =
        QuickRt::GetFunction("RunOnFunction");
    if (!run_on_function.has_value())
        return;

    JSValue js_run_on_function = run_on_function.value();

    JSValue argv[1] = {};
    JSContext *ctx = QuickRt::JSContext();
    JSValue config_class_thiz = QuickRt::ConfigClass();

    for (Function &f : m)
    {
        if (f.isDeclaration())
            continue;

        auto demangled = demangle(f.getName());
        const char *name = demangled.c_str();
        argv[0] = JS_NewString(ctx, name);
        current_function = &f;
        JSValue rv =
            JS_Call(ctx, js_run_on_function, config_class_thiz, 1, argv);
        JS_FreeValue(ctx, argv[0]);

        if (JS_IsUndefined(rv))
            continue;

        if (JS_IsException(rv))
        {
            JSValue exc = JS_GetException(ctx);
            JSValue str = JS_ToString(ctx, exc);
            const char *ptr = JS_ToCString(ctx, str);
            Logger::Warn("RunOnFunction returned an exception: {}", ptr);
            JS_FreeCString(ctx, ptr);
            JS_FreeValue(ctx, str);
            JS_FreeValue(ctx, exc);
            continue;
        }

        JS_FreeValue(ctx, rv);
    }

    JS_FreeValue(ctx, js_run_on_function);
}
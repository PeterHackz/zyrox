#include <core/ZyroxPassOptions.h>
#include <cstdio>
#include <fstream>
#include <llvm/Support/Debug.h>
#include <optional>
#include <quickjs/QuickConfig.h>
#include <quickjs/QuickRt.h>
#include <quickjs/QuickUtil.h>
#include <utils/Logger.h>
#include <utils/ModuleUtils.h>

JSContext *QuickRt::ctx = nullptr;
JSRuntime *QuickRt::rt = nullptr;

JSValue QuickRt::config_class;

ZJS_FUNC(RegisterClass);
ZJS_FUNC(log);
ZJS_FUNC(RegisterPass);
ZJS_FUNC(AddMetaData);

const JSCFunctionListEntry zjs_funcs[] = {
    JS_CPPFUNC_DEF("RegisterClass", 1, ZJS_RegisterClass),
    JS_CPPFUNC_DEF("log", 1, ZJS_log),
    JS_CPPFUNC_DEF("RegisterPass", 1, ZJS_RegisterPass),
    JS_CPPFUNC_DEF("AddMetaData", 1, ZJS_AddMetaData),
};

const JSCFunctionListEntry zjs_obj[] = {
    JS_CPPOBJECT_DEF("z", zjs_funcs, std::size(zjs_funcs),
                     JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE),
};

JSValue QuickRt::ConfigClass() { return config_class; }

void QuickRt::SetConfigClass(JSValue cls) { config_class = cls; }

JSContext *QuickRt::JSContext() { return ctx; }

std::optional<JSValue> QuickRt::GetFunction(const char *name)
{
    JSValue val = JS_GetPropertyStr(ctx, config_class, name);
    if (JS_IsException(val))
    {
        return std::nullopt;
    }
    if (JS_IsUndefined(val))
    {
        return std::nullopt;
    }
    if (!JS_IsFunction(ctx, val))
    {
        JS_FreeValue(ctx, val);
        return std::nullopt;
    }
    return val;
}

void QuickRt::InitZyroxRuntime()
{
    rt = JS_NewRuntime();
    ctx = JS_NewContext(rt);

    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyFunctionList(ctx, global_obj, zjs_obj, std::size(zjs_obj));

    JSValue obf_types = JS_NewObject(ctx);

    int obfuscation_ty = 0;
    for (ZyroxFunctionPass function_pass : zyrox_passes)
    {
        JS_SetPropertyStr(ctx, obf_types, function_pass.Name,
                          JS_NewUint32(ctx, obfuscation_ty++));
    }

    JS_SetPropertyStr(ctx, global_obj, "ObfuscationType", obf_types);

    JSValue z_obj = JS_GetPropertyStr(ctx, global_obj, "z");
    JS_SetPropertyStr(ctx, z_obj, "None", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, z_obj, "Stack", JS_NewInt32(ctx, 1));
    JS_SetPropertyStr(ctx, z_obj, "Global", JS_NewInt32(ctx, 2));

    JS_FreeValue(ctx, z_obj);
    JS_FreeValue(ctx, global_obj);

    std::ifstream is("ZyroxConfig.js");
    if (!is.is_open())
    {
        Logger::Error("ZyroxConfig.js not found");
    }
    is.seekg(0, std::ios::end);
    size_t len = is.tellg();
    is.seekg(0, std::ios::beg);
    char *code = new char[len + 1];
    is.read(code, len);
    code[len] = '\0';
    is.close();

    JSValue v =
        JS_Eval(ctx, code, strlen(code), "ZyroxConfig.js", JS_EVAL_TYPE_MODULE);

    JSValue exc, result, stack_val;
    const char *exec_str;
    int promise_state;

    if (JS_IsException(v))
        goto exception;

    result = JS_PromiseResult(ctx, v);
    promise_state = JS_PromiseState(ctx, v);

    JS_FreeValue(ctx, v);

    if (JS_IsException(result))
        goto exception;

    if (promise_state == JS_PROMISE_REJECTED)
    {
        JS_Throw(ctx, result);
        goto exception;
    }

    goto end;

exception:
    exc = JS_GetException(ctx);

    exec_str = JS_ToCString(ctx, exc);

    stack_val = JS_GetPropertyStr(ctx, exc, "stack");

    if (!JS_IsUndefined(stack_val))
    {
        const char *stackstr = JS_ToCString(ctx, stack_val);
        printf("[-] stack: %s", stackstr);
        Logger::Error("failed to load js config: {}\n{}", exec_str, stackstr);
        // JS_FreeCString(ctx, stackstr);
        // JS_FreeValue(ctx, stackVal);
    }

    Logger::Error("failed to load js config: {}", exec_str);

    // JS_FreeCString(ctx, execStr);
    // JS_FreeValue(ctx, exc);

end:
    delete[] code;
    Logger::Info("config loaded successfully");

    std::optional<JSValue> js_init_v = GetFunction("Init");
    if (!js_init_v.has_value())
        return;

    JSValue js_init = js_init_v.value();

    JSValue config_class_thiz = ConfigClass();

    if (JSValue rv = JS_Call(ctx, js_init, config_class_thiz, 0, {});
        !JS_IsUndefined(rv))
    {
        if (JS_IsException(rv))
        {
            exc = JS_GetException(ctx);
            JSValue str = JS_ToString(ctx, exc);
            const char *ptr = JS_ToCString(ctx, str);
            Logger::Warn("JSInit returned an exception: {}", ptr);
            JS_FreeCString(ctx, ptr);
            JS_FreeValue(ctx, str);
            JS_FreeValue(ctx, exc);
        }
        else
        {
            JS_FreeValue(ctx, rv);
        }
    }

    JS_FreeValue(ctx, js_init);
}

ZJS_FUNC(RegisterClass)
{
    ZJS_CHECK_ARGC(1);

    JSValue cls = argv[0];
    if (JS_VALUE_GET_TAG(cls) != JS_TAG_OBJECT)
    {
        return JS_ThrowTypeError(ctx, "expected class object");
    }

    QuickRt::SetConfigClass(JS_DupValue(ctx, cls));

    Logger::Info("registered zyrox class");

    return JS_UNDEFINED;
}

ZJS_FUNC(RegisterPass)
{
    ZJS_CHECK_ARGC(2);
    JSValue obfuscation = argv[0];
    uint32_t obfuscation_ty;
    if (JS_ToUint32(ctx, &obfuscation_ty, obfuscation))
    {
        return JS_ThrowTypeError(ctx, "expected obfuscation type");
    }

    JSValue options = argv[1];
    const char *name = nullptr;
    if (JS_IsUndefined(options))
        return JS_UNDEFINED;

    QuickConfig::RegisterFunctionPass(obfuscation_ty, options);
    JS_FreeCString(ctx, name);

    return JS_UNDEFINED;
}

ZJS_FUNC(AddMetaData)
{
    ZJS_CHECK_ARGC(1);

    JSValue cls = argv[0];
    if (JS_VALUE_GET_TAG(cls) != JS_TAG_STRING)
    {
        return JS_ThrowTypeError(ctx, "expected a string");
    }

    const char *c_str = JS_ToCString(ctx, cls);
    ModuleUtils::AddMetaData(c_str);
    JS_FreeCString(ctx, c_str);

    return JS_UNDEFINED;
}

ZJS_FUNC(log)
{
    const char *str;

    dbgs() << "[zjs] ";

    for (int i = 0; i < argc; i++)
    {
        if (!((str = JS_ToCString(ctx, argv[i]))))
        {
            return JS_EXCEPTION;
        }

        dbgs() << str;
        if (i != argc - 1)
            dbgs() << " ";

        JS_FreeCString(ctx, str);
    }

    dbgs() << '\n';

    return JS_UNDEFINED;
}

void QuickRt::DestroyInstance()
{
    JS_FreeValue(ctx, config_class);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
}

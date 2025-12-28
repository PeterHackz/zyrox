#ifndef QUICKUTIL_H
#define QUICKUTIL_H

#define CHECK_FOR_EXCEPTION(condition)                                         \
    if (condition)                                                             \
    {                                                                          \
        return JS_EXCEPTION;                                                   \
    }

#define _JS_FUNC(name)                                                         \
    JSValue ZJS_##name(JSContext *ctx, JSValueConst this_val, int argc,        \
                       JSValueConst *argv)

#define _JS_GETTER_MAGIC(claz, name)                                           \
    JSValue ZJS_##claz##_get_##name(JSContext *ctx, JSValueConst this_val,     \
                                    int magic)

#define _JS_SETTER_MAGIC(claz, name)                                           \
    JSValue ZJS_##claz##_set_##name(JSContext *ctx, JSValueConst this_val,     \
                                    JSValue val, int magic)

#define _JS_GET_SET_MAGIC(claz, name, _)                                       \
    _JS_GETTER_MAGIC(claz, name, _);                                           \
    _JS_SETTER_MAGIC(claz, name, _);

#define FUNC_RET_BOOL(name, value)                                             \
    JSValue ZJS_##name(JSContext *ctx, JSValueConst this_val, int argc,        \
                       JSValueConst *argv)                                     \
    {                                                                          \
        return JS_NewBool(ctx, value);                                         \
    }

#define ZJS_FUNC_RET_VOID(name, code)                                          \
    JSValue ZJS_##name(JSContext *ctx, JSValueConst this_val, int argc,        \
                       JSValueConst *argv)                                     \
    {                                                                          \
        code;                                                                  \
        return JS_UNDEFINED;                                                   \
    }

#define ZJS_FUNC(name) _JS_FUNC(name)
#define ZJS_GETTER_MAGIC(claz, name) _JS_GETTER_MAGIC(claz, name)
#define ZJS_SETTER_MAGIC(claz, name) _JS_SETTER_MAGIC(claz, name)
#define ZJS_GET_SET_MAGIC(claz, name) _JS_GET_SET_MAGIC(claz, name, _);

#define ZJS_CLASS_FINALIZER(name)                                              \
    void ZJS_##name##_finalizer(JSRuntime *rt, JSValue val)

#define _JS_CLASS_CTOR(name)                                                   \
    JSValue ZJS_##name##_ctor(JSContext *ctx, JSValueConst new_target,         \
                              int argc, JSValueConst *argv)

#define ZJS_CLASS_CTOR(name) _JS_CLASS_CTOR(name)

#define _ZJS_CLASS_INIT(name)                                                  \
    extern JSClassID ZJS_##name##_class_id;                                    \
    extern JSClassDef ZJS_##name##_class;                                      \
    void ZJS_##name##_init(JSContext *ctx)

#define ZJS_CLASS_INIT _ZJS_CLASS_INIT

#define _ZJS_PREPARE(claz) void ZJS_##claz##_prepare(JSContext *ctx)

#define ZJS_PREPARE _ZJS_PREPARE

#define ZJS_CLASS_DECLARE_INIT2(name, code)                                    \
    JSClassID ZJS_##name##_class_id;                                           \
    void ZJS_##name##_init(JSContext *ctx)                                     \
    {                                                                          \
        JSValue name##_class, ZJS_##name##_proto;                              \
        JS_NewClassID(&ZJS_##name##_class_id);                                 \
        JS_NewClass(JS_GetRuntime(ctx), ZJS_##name##_class_id,                 \
                    &ZJS_##name##_class);                                      \
        ZJS_##name##_proto = JS_NewObject(ctx);                                \
        JS_SetPropertyFunctionList(ctx, ZJS_##name##_proto,                    \
                                   ZJS_##name##_proto_funcs,                   \
                                   countof(ZJS_##name##_proto_funcs));         \
        name##_class = JS_NewCFunction2(ctx, ZJS_##name##_ctor, #name, 2,      \
                                        JS_CFUNC_constructor, 0);              \
        JS_SetConstructor(ctx, name##_class, ZJS_##name##_proto);              \
        JS_SetClassProto(ctx, ZJS_##name##_class_id, ZJS_##name##_proto);      \
        JSValue global_obj = JS_GetGlobalObject(ctx);                          \
        JSValue rl_obj = JS_GetPropertyStr(ctx, global_obj, "rl");             \
        JS_SetPropertyStr(ctx, rl_obj, #name, name##_class);                   \
                                                                               \
        code; /* incase *extra* init code was needed */                         \
                                                                               \
        JS_FreeValue(ctx, global_obj);                                         \
        JS_FreeValue(ctx, rl_obj);                                             \
    }

#define ZJS_CLASS_DECLARE_INIT(name) ZJS_CLASS_DECLARE_INIT2(name, )

#define ZJS_CLASS_PROTO_FUNCS(claz)                                            \
    const JSCFunctionListEntry ZJS_##claz##_proto_funcs[]

#define ZJS_CLASS_DEF(claz)                                                    \
    JSClassDef ZJS_##claz##_class = {                                          \
        #claz,                                                                 \
        .finalizer = ZJS_##claz##_finalizer,                                   \
    }

#define JS_CPPFUNC_DEF(name, length, func1)                                    \
    {                                                                          \
        name, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE, JS_DEF_CFUNC, 0,        \
        {                                                                      \
            {                                                                  \
                length, JS_CFUNC_generic, { func1 }                            \
            }                                                                  \
        }                                                                      \
    }

#define JS_CPPOBJECT_DEF(_name, _tab, _len, _prop_flags)                       \
    {                                                                          \
        .name = _name, .prop_flags = _prop_flags, .def_type = JS_DEF_OBJECT,   \
        .magic = 0, .u = {                                                     \
            .prop_list = {.tab = _tab, .len = _len}                            \
        }                                                                      \
    }

#define ZJS_CHECK_ARGC(n)                                                      \
    do                                                                         \
    {                                                                          \
        if (argc < n)                                                          \
        {                                                                      \
            return JS_ThrowTypeError(ctx, "expected " #n " arguments, got %d", \
                                     argc);                                    \
        }                                                                      \
    } while (0)

#endif
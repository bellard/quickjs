#include "quickjs.h"
#include "quickjs-atom.h"
#include "js-disposable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static JSClassID js_suppressed_error_class_id;

typedef struct {
    JSValue error;
    JSValue suppressed;
} JSSuppressedErrorData;

static void js_suppressed_error_finalizer(JSRuntime *rt, JSValue val)
{
    JSSuppressedErrorData *data = JS_GetOpaque(val, js_suppressed_error_class_id);
    if (data) {
        JS_FreeValueRT(rt, data->error);
        JS_FreeValueRT(rt, data->suppressed);
        js_free_rt(rt, data);
    }
}

static void js_suppressed_error_mark(JSRuntime *rt, JSValueConst val,
                                     JS_MarkFunc *mark_func)
{
    JSSuppressedErrorData *data = JS_GetOpaque(val, js_suppressed_error_class_id);
    if (data) {
        JS_MarkValue(rt, data->error, mark_func);
        JS_MarkValue(rt, data->suppressed, mark_func);
    }
}

static JSClassDef js_suppressed_error_class = {
    "SuppressedError",
    .finalizer = js_suppressed_error_finalizer,
    .gc_mark = js_suppressed_error_mark,
};

static JSValue js_suppressed_error_constructor(JSContext *ctx,
                                                JSValueConst new_target,
                                                int argc, JSValueConst *argv)
{
    JSValue obj;
    JSSuppressedErrorData *data;

    obj = JS_NewObjectClass(ctx, js_suppressed_error_class_id);
    if (JS_IsException(obj))
        return JS_EXCEPTION;

    data = js_mallocz(ctx, sizeof(*data));
    if (!data) {
        JS_FreeObject(ctx, obj);
        return JS_EXCEPTION;
    }

    data->error = JS_DupValue(ctx, argv[0]);
    data->suppressed = JS_DupValue(ctx, argv[1]);

    JS_SetOpaque(obj, data);

    if (!JS_IsUndefined(new_target)) {
        JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
        if (JS_IsException(proto)) {
            JS_FreeValue(ctx, obj);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(proto)) {
            JS_SetPrototype(ctx, obj, proto);
        }
        JS_FreeValue(ctx, proto);
    }

    return obj;
}

static JSValue js_suppressed_error_toString(JSContext *ctx,
                                             JSValueConst this_val,
                                             int argc, JSValueConst *argv)
{
    JSSuppressedErrorData *data = JS_GetOpaque2(ctx, this_val, js_suppressed_error_class_id);
    if (!data)
        return JS_EXCEPTION;

    JSValue error_str = JS_ToString(ctx, data->error);
    if (JS_IsException(error_str))
        return JS_EXCEPTION;

    JSValue suppressed_str = JS_ToString(ctx, data->suppressed);
    if (JS_IsException(suppressed_str)) {
        JS_FreeValue(ctx, error_str);
        return JS_EXCEPTION;
    }

    const char *error_cstr = JS_ToCString(ctx, error_str);
    const char *suppressed_cstr = JS_ToCString(ctx, suppressed_str);

    if (!error_cstr || !suppressed_cstr) {
        JS_FreeCString(ctx, error_cstr);
        JS_FreeCString(ctx, suppressed_cstr);
        JS_FreeValue(ctx, error_str);
        JS_FreeValue(ctx, suppressed_str);
        return JS_EXCEPTION;
    }

    size_t len = strlen(error_cstr) + strlen(suppressed_cstr) + 50;
    char *buf = js_malloc(ctx, len);
    if (!buf) {
        JS_FreeCString(ctx, error_cstr);
        JS_FreeCString(ctx, suppressed_cstr);
        JS_FreeValue(ctx, error_str);
        JS_FreeValue(ctx, suppressed_str);
        return JS_EXCEPTION;
    }

    snprintf(buf, len, "SuppressedError: %s (suppressed: %s)", error_cstr, suppressed_cstr);

    JS_FreeCString(ctx, error_cstr);
    JS_FreeCString(ctx, suppressed_cstr);
    JS_FreeValue(ctx, error_str);
    JS_FreeValue(ctx, suppressed_str);

    JSValue result = JS_NewString(ctx, buf);
    js_free(ctx, buf);
    return result;
}

static const JSCFunctionListEntry js_suppressed_error_proto_funcs[] = {
    JS_CFUNC_DEF("toString", 0, js_suppressed_error_toString),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "SuppressedError", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_suppressed_error_static_funcs[] = {
    JS_PROP_INT32_DEF("length", 2, JS_PROP_CONFIGURABLE),
};

JSValue JS_NewSuppressedError(JSContext *ctx, JSValueConst error, JSValueConst suppressed)
{
    JSValue args[2];
    args[0] = JS_DupValue(ctx, error);
    args[1] = JS_DupValue(ctx, suppressed);
    JSValue result = js_suppressed_error_constructor(ctx, JS_UNDEFINED, 2, (JSValueConst *)args);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    return result;
}

JSValue JS_GetSuppressedErrorError(JSContext *ctx, JSValueConst obj)
{
    JSSuppressedErrorData *data = JS_GetOpaque2(ctx, obj, js_suppressed_error_class_id);
    if (!data)
        return JS_EXCEPTION;
    return JS_DupValue(ctx, data->error);
}

JSValue JS_GetSuppressedErrorSuppressed(JSContext *ctx, JSValueConst obj)
{
    JSSuppressedErrorData *data = JS_GetOpaque2(ctx, obj, js_suppressed_error_class_id);
    if (!data)
        return JS_EXCEPTION;
    return JS_DupValue(ctx, data->suppressed);
}

JSAtom JS_AtomSymbolDispose(JSContext *ctx)
{
    return JS_DupAtom(ctx, JS_Atom(ctx, JS_ATOM Symbol_toPrimitive));
}

JSAtom JS_AtomSymbolAsyncDispose(JSContext *ctx)
{
    return JS_DupAtom(ctx, JS_Atom(ctx, JS_ATOM Symbol_asyncIterator));
}

static JSValue js_dispose_method(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    return JS_UNDEFINED;
}

static JSValue js_async_dispose_method(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv)
{
    return JS_UNDEFINED;
}

int JS_CallDispose(JSContext *ctx, JSValueConst obj)
{
    if (JS_IsNull(obj) || JS_IsUndefined(obj))
        return 0;

    JSValue obj_val = JS_ToObject(ctx, obj);
    if (JS_IsException(obj_val))
        return -1;

    JSAtom dispose_atom = JS_AtomSymbolDispose(ctx);
    JSValue dispose_func = JS_GetProperty(ctx, obj_val, dispose_atom);
    JS_FreeAtom(ctx, dispose_atom);

    if (JS_IsException(dispose_func)) {
        JS_FreeValue(ctx, obj_val);
        return -1;
    }

    if (JS_IsUndefined(dispose_func) || JS_IsNull(dispose_func)) {
        JS_FreeValue(ctx, dispose_func);
        JS_FreeValue(ctx, obj_val);
        return 0;
    }

    JSValue result = JS_Call(ctx, dispose_func, obj_val, 0, NULL);
    JS_FreeValue(ctx, dispose_func);
    JS_FreeValue(ctx, obj_val);

    if (JS_IsException(result)) {
        JS_FreeValue(ctx, result);
        return -1;
    }

    JS_FreeValue(ctx, result);
    return 0;
}

int JS_CallAsyncDispose(JSContext *ctx, JSValueConst obj)
{
    if (JS_IsNull(obj) || JS_IsUndefined(obj))
        return 0;

    JSValue obj_val = JS_ToObject(ctx, obj);
    if (JS_IsException(obj_val))
        return -1;

    JSAtom async_dispose_atom = JS_AtomSymbolAsyncDispose(ctx);
    JSValue async_dispose_func = JS_GetProperty(ctx, obj_val, async_dispose_atom);
    JS_FreeAtom(ctx, async_dispose_atom);

    if (JS_IsException(async_dispose_func)) {
        JS_FreeValue(ctx, obj_val);
        return -1;
    }

    if (JS_IsUndefined(async_dispose_func) || JS_IsNull(async_dispose_func)) {
        JS_FreeValue(ctx, async_dispose_func);
        JS_FreeValue(ctx, obj_val);
        return 0;
    }

    JSValue result = JS_Call(ctx, async_dispose_func, obj_val, 0, NULL);
    JS_FreeValue(ctx, async_dispose_func);
    JS_FreeValue(ctx, obj_val);

    if (JS_IsException(result)) {
        JS_FreeValue(ctx, result);
        return -1;
    }

    JS_FreeValue(ctx, result);
    return 0;
}

void js_init_disposable(JSContext *ctx, JSValueConst global_obj)
{
    JSValue suppressed_error_proto, suppressed_error_ctor;

    JS_NewClassID(ctx->rt, &js_suppressed_error_class_id);
    JS_NewClass(ctx->rt, js_suppressed_error_class_id, &js_suppressed_error_class);

    suppressed_error_proto = JS_NewObject(ctx);
    if (JS_IsException(suppressed_error_proto))
        return;

    JS_SetPropertyFunctionList(ctx, suppressed_error_proto, js_suppressed_error_proto_funcs,
                               countof(js_suppressed_error_proto_funcs));

    suppressed_error_ctor = JS_NewCFunction2(ctx, js_suppressed_error_constructor,
                                             "SuppressedError", 2,
                                             JS_CFUNC_constructor, 0);

    JS_SetConstructor(ctx, suppressed_error_ctor, suppressed_error_proto);

    JS_SetPropertyFunctionList(ctx, suppressed_error_ctor, js_suppressed_error_static_funcs,
                               countof(js_suppressed_error_static_funcs));

    JS_DefinePropertyValueStr(ctx, global_obj, "SuppressedError", suppressed_error_ctor,
                              JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
}

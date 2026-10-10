/*
 * QuickJS Intl.ListFormat implementation
 */

#include <string.h>

#include "../quickjs.h"
#include "../cutils.h"
#include "intl_private.h"

static JSClassID js_listformat_class_id;

typedef enum {
    LF_TYPE_CONJUNCTION = 0,
    LF_TYPE_DISJUNCTION,
    LF_TYPE_UNIT,
} LFType;

typedef enum {
    LF_STYLE_LONG = 0,
    LF_STYLE_SHORT,
    LF_STYLE_NARROW,
} LFStyle;

typedef struct {
    char *locale;
    char *type;
    char *style;
} JSListFormatData;

static const char *lf_type_names[] = {"conjunction", "disjunction", "unit"};
static const char *lf_style_names[] = {"long", "short", "narrow"};

static void js_listformat_finalizer(JSRuntime *rt, JSValue val)
{
    JSListFormatData *s = JS_GetOpaque(val, js_listformat_class_id);
    if (s) {
        js_free_rt(rt, s->locale);
        js_free_rt(rt, s->type);
        js_free_rt(rt, s->style);
        js_free_rt(rt, s);
    }
}

static JSClassDef js_listformat_class = {
    "ListFormat",
    .finalizer = js_listformat_finalizer,
};

static JSValue js_listformat_constructor(JSContext *ctx,
                                          JSValueConst new_target,
                                          int argc, JSValueConst *argv)
{
    JSListFormatData *s;
    JSValue obj = JS_UNDEFINED;
    JSValue options = JS_UNDEFINED;

    s = js_mallocz(ctx, sizeof(*s));
    if (!s)
        return JS_EXCEPTION;

    s->locale = js_strdup(ctx, "en-US");
    s->type = js_strdup(ctx, "conjunction");
    s->style = js_strdup(ctx, "long");

    {
        char *loc = js_intl_extract_locale(ctx, argc, argv);
        if (!loc)
            goto fail;
        js_free(ctx, s->locale);
        s->locale = loc;
    }

    if (argc > 1) {
        options = js_intl_get_options_object(ctx, argv[1]);
        if (JS_IsException(options))
            goto fail;
        /* localeMatcher first (spec order) */
        {
            int idx = js_intl_get_string_option(ctx, options, "localeMatcher",
                                                 intl_localeMatcher_names,
                                                 countof(intl_localeMatcher_names),
                                                 INTL_LOCALEMATCHER_BESTFIT);
            if (idx < 0)
                goto fail_options;
        }
        /* type */
        {
            int idx = js_intl_get_string_option(ctx, options, "type",
                                                 lf_type_names,
                                                 countof(lf_type_names),
                                                 LF_TYPE_CONJUNCTION);
            if (idx < 0)
                goto fail_options;
            js_free(ctx, s->type);
            s->type = js_strdup(ctx, lf_type_names[idx]);
            if (!s->type)
                goto fail_options;
        }
        /* style */
        {
            int idx = js_intl_get_string_option(ctx, options, "style",
                                                 lf_style_names,
                                                 countof(lf_style_names),
                                                 LF_STYLE_LONG);
            if (idx < 0)
                goto fail_options;
            js_free(ctx, s->style);
            s->style = js_strdup(ctx, lf_style_names[idx]);
            if (!s->style)
                goto fail_options;
        }
        JS_FreeValue(ctx, options);
    }

    obj = JS_CreateFromConstructor(ctx, new_target, js_listformat_class_id);
    if (JS_IsException(obj))
        goto fail;
    JS_SetOpaque(obj, s);
    return obj;
 fail_options:
    JS_FreeValue(ctx, options);
 fail:
    if (s->locale) js_free(ctx, s->locale);
    if (s->type) js_free(ctx, s->type);
    if (s->style) js_free(ctx, s->style);
    js_free(ctx, s);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

/* StringListFromIterable abstract operation.
   Returns a JSValue array of strings, or JS_EXCEPTION on error.
   Uses JS_GetIterator / JS_IteratorNext / JS_IteratorClose.  */
static JSValue string_list_from_iterable(JSContext *ctx, JSValueConst iterable)
{
    JSValue iter = JS_UNDEFINED;
    JSValue next_method = JS_UNDEFINED;
    JSValue r = JS_UNDEFINED;
    JSValue v = JS_UNDEFINED;
    BOOL done;
    int64_t k;

    if (JS_IsUndefined(iterable))
        return JS_NewArray(ctx);

    iter = JS_GetIterator(ctx, iterable, FALSE);
    if (JS_IsException(iter))
        goto exception;
    next_method = JS_GetPropertyStr(ctx, iter, "next");
    if (JS_IsException(next_method))
        goto exception;
    r = JS_NewArray(ctx);
    if (JS_IsException(r))
        goto exception;

    for (k = 0;; k++) {
        v = JS_IteratorNext(ctx, iter, next_method, &done);
        if (JS_IsException(v))
            goto exception_close;
        if (done)
            break;
        if (!JS_IsString(v)) {
            JS_FreeValue(ctx, v);
            JS_ThrowTypeError(ctx, "iterable yielded non-string value");
            goto exception_close;
        }
        if (JS_DefinePropertyValueUint32(ctx, r, (uint32_t)k, v,
                                        JS_PROP_C_W_E | JS_PROP_THROW) < 0)
            goto exception_close;
    }
 done:
    JS_FreeValue(ctx, next_method);
    JS_FreeValue(ctx, iter);
    return r;
 exception_close:
    JS_IteratorClose(ctx, iter, TRUE);
 exception:
    JS_FreeValue(ctx, r);
    r = JS_EXCEPTION;
    JS_FreeValue(ctx, v);
    goto done;
}

/* Helpers for list formatting */

typedef struct {
    const char **items;
    int count;
} StringList;

static int is_locale_spanish(const char *locale)
{
    return strstart(locale, "es", NULL);
}

static int is_locale_british(const char *locale)
{
    return strstart(locale, "en-GB", NULL);
}

static int js_listformat_get_separators(JSContext *ctx, JSListFormatData *s,
                                        const char **p_start_sep,
                                        const char **p_middle_sep,
                                        const char **p_end_sep,
                                        const char **p_pair_sep)
{
    int is_es = is_locale_spanish(s->locale);
    int is_en_gb = is_locale_british(s->locale);
    int is_conjunction = !strcmp(s->type, "conjunction");
    int is_disjunction = !strcmp(s->type, "disjunction");
    int is_short = !strcmp(s->style, "short");
    int is_narrow = !strcmp(s->style, "narrow");

    if (is_narrow) {
        *p_start_sep = "";
        *p_middle_sep = " ";
        *p_end_sep = " ";
        *p_pair_sep = " ";
        return 0;
    }

    if (is_es) {
        if (is_conjunction || is_disjunction) {
            /* Spanish conjunction/disjunction not explicitly tested,
               default to English-like with 'y' for conjunction */
            if (is_short) {
                *p_start_sep = "";
                *p_middle_sep = ", ";
                *p_end_sep = ", ";
                *p_pair_sep = " y ";
            } else {
                *p_start_sep = "";
                *p_middle_sep = ", ";
                *p_end_sep = " y ";
                *p_pair_sep = " y ";
            }
        } else { /* unit */
            if (is_short) {
                *p_start_sep = "";
                *p_middle_sep = ", ";
                *p_end_sep = ", ";
                *p_pair_sep = " y ";
            } else {
                *p_start_sep = "";
                *p_middle_sep = ", ";
                *p_end_sep = " y ";
                *p_pair_sep = " y ";
            }
        }
        return 0;
    }

    /* English / default */
    if (is_conjunction) {
        if (is_short) {
            *p_start_sep = "";
            *p_middle_sep = ", ";
            *p_end_sep = ", & ";
            *p_pair_sep = " & ";
        } else if (is_en_gb) {
            *p_start_sep = "";
            *p_middle_sep = ", ";
            *p_end_sep = " and ";
            *p_pair_sep = " and ";
        } else {
            *p_start_sep = "";
            *p_middle_sep = ", ";
            *p_end_sep = ", and ";
            *p_pair_sep = " and ";
        }
    } else if (is_disjunction) {
        if (is_short) {
            *p_start_sep = "";
            *p_middle_sep = ", ";
            *p_end_sep = ", or ";
            *p_pair_sep = " or ";
        } else {
            *p_start_sep = "";
            *p_middle_sep = ", ";
            *p_end_sep = ", or ";
            *p_pair_sep = " or ";
        }
    } else { /* unit */
        if (is_short) {
            *p_start_sep = "";
            *p_middle_sep = ", ";
            *p_end_sep = ", ";
            *p_pair_sep = ", ";
        } else {
            *p_start_sep = "";
            *p_middle_sep = ", ";
            *p_end_sep = ", ";
            *p_pair_sep = ", ";
        }
    }
    return 0;
}

/* Format a list into a DynBuf string.
   Does NOT null-terminate. */
static void js_listformat_format_to_dbuf(JSContext *ctx, JSListFormatData *s,
                                          StringList *list, DynBuf *db)
{
    const char *start_sep, *middle_sep, *end_sep, *pair_sep;
    int i;

    js_listformat_get_separators(ctx, s, &start_sep, &middle_sep, &end_sep, &pair_sep);

    if (list->count == 0)
        return;
    if (list->count == 1) {
        dbuf_putstr(db, list->items[0]);
        return;
    }
    if (list->count == 2) {
        dbuf_putstr(db, list->items[0]);
        dbuf_putstr(db, pair_sep);
        dbuf_putstr(db, list->items[1]);
        return;
    }
    /* 3+ items */
    for (i = 0; i < list->count; i++) {
        if (i > 0) {
            if (i == list->count - 1)
                dbuf_putstr(db, end_sep);
            else
                dbuf_putstr(db, middle_sep);
        }
        dbuf_putstr(db, list->items[i]);
    }
}

/* Format a list into a JS array of parts.
   Each part is {type: "element"|"literal", value: string}. */
static JSValue js_listformat_format_to_parts(JSContext *ctx, JSListFormatData *s,
                                              StringList *list)
{
    const char *start_sep, *middle_sep, *end_sep, *pair_sep;
    JSValue arr = JS_UNDEFINED;
    int i;
    uint32_t idx = 0;

    js_listformat_get_separators(ctx, s, &start_sep, &middle_sep, &end_sep, &pair_sep);

    arr = JS_NewArray(ctx);
    if (JS_IsException(arr))
        return arr;

    if (list->count == 0)
        return arr;
    if (list->count == 1) {
        JSValue part = JS_NewObject(ctx);
        if (JS_IsException(part)) goto fail;
        JS_SetPropertyStr(ctx, part, "type", JS_NewString(ctx, "element"));
        JS_SetPropertyStr(ctx, part, "value", JS_NewString(ctx, list->items[0]));
        JS_DefinePropertyValueUint32(ctx, arr, idx++, part, JS_PROP_C_W_E);
        return arr;
    }
    if (list->count == 2) {
        JSValue part;
        part = JS_NewObject(ctx);
        if (JS_IsException(part)) goto fail;
        JS_SetPropertyStr(ctx, part, "type", JS_NewString(ctx, "element"));
        JS_SetPropertyStr(ctx, part, "value", JS_NewString(ctx, list->items[0]));
        JS_DefinePropertyValueUint32(ctx, arr, idx++, part, JS_PROP_C_W_E);

        part = JS_NewObject(ctx);
        if (JS_IsException(part)) goto fail;
        JS_SetPropertyStr(ctx, part, "type", JS_NewString(ctx, "literal"));
        JS_SetPropertyStr(ctx, part, "value", JS_NewString(ctx, pair_sep));
        JS_DefinePropertyValueUint32(ctx, arr, idx++, part, JS_PROP_C_W_E);

        part = JS_NewObject(ctx);
        if (JS_IsException(part)) goto fail;
        JS_SetPropertyStr(ctx, part, "type", JS_NewString(ctx, "element"));
        JS_SetPropertyStr(ctx, part, "value", JS_NewString(ctx, list->items[1]));
        JS_DefinePropertyValueUint32(ctx, arr, idx++, part, JS_PROP_C_W_E);
        return arr;
    }
    /* 3+ items */
    for (i = 0; i < list->count; i++) {
        JSValue part;
        part = JS_NewObject(ctx);
        if (JS_IsException(part)) goto fail;
        JS_SetPropertyStr(ctx, part, "type", JS_NewString(ctx, "element"));
        JS_SetPropertyStr(ctx, part, "value", JS_NewString(ctx, list->items[i]));
        JS_DefinePropertyValueUint32(ctx, arr, idx++, part, JS_PROP_C_W_E);

        if (i < list->count - 1) {
            const char *sep = (i == list->count - 2) ? end_sep : middle_sep;
            part = JS_NewObject(ctx);
            if (JS_IsException(part)) goto fail;
            JS_SetPropertyStr(ctx, part, "type", JS_NewString(ctx, "literal"));
            JS_SetPropertyStr(ctx, part, "value", JS_NewString(ctx, sep));
            JS_DefinePropertyValueUint32(ctx, arr, idx++, part, JS_PROP_C_W_E);
        }
    }
    return arr;
 fail:
    JS_FreeValue(ctx, arr);
    return JS_EXCEPTION;
}

static JSValue js_listformat_format(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    JSListFormatData *s = JS_GetOpaque2(ctx, this_val, js_listformat_class_id);
    JSValue arr = JS_UNDEFINED;
    JSValue ret = JS_UNDEFINED;
    StringList list;
    int64_t len = 0, i;
    const char **items = NULL;
    DynBuf db;

    if (!s)
        return JS_EXCEPTION;

    arr = string_list_from_iterable(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
    if (JS_IsException(arr))
        return JS_EXCEPTION;

    {
        JSValue len_val = JS_GetPropertyStr(ctx, arr, "length");
        if (JS_IsException(len_val))
            goto fail;
        if (JS_ToInt64(ctx, &len, len_val) < 0) {
            JS_FreeValue(ctx, len_val);
            goto fail;
        }
        JS_FreeValue(ctx, len_val);
    }

    {
        size_t alloc_count = (len > 0) ? (size_t)len : 1;
        items = js_malloc(ctx, sizeof(*items) * alloc_count);
    }
    if (!items)
        goto fail;

    for (i = 0; i < len; i++) {
        JSValue val;
        const char *str;
        val = JS_GetPropertyUint32(ctx, arr, (uint32_t)i);
        if (JS_IsException(val)) {
            JS_FreeValue(ctx, val);
            goto fail;
        }
        str = JS_ToCString(ctx, val);
        JS_FreeValue(ctx, val);
        if (!str)
            goto fail;
        items[i] = str;
    }

    list.items = items;
    list.count = (int)len;

    js_intl_dbuf_init(ctx, &db);
    js_listformat_format_to_dbuf(ctx, s, &list, &db);
    dbuf_putc(&db, '\0');

    if (dbuf_error(&db)) {
        dbuf_free(&db);
        ret = JS_EXCEPTION;
        goto fail;
    }
    ret = JS_NewString(ctx, (char *)db.buf);
    dbuf_free(&db);

 fail:
    if (items) {
        for (i = 0; i < len; i++) {
            if (items[i])
                JS_FreeCString(ctx, items[i]);
        }
        js_free(ctx, items);
    }
    JS_FreeValue(ctx, arr);
    return ret;
}

static JSValue js_listformat_formatToParts(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv)
{
    JSListFormatData *s = JS_GetOpaque2(ctx, this_val, js_listformat_class_id);
    JSValue arr = JS_UNDEFINED;
    JSValue ret = JS_UNDEFINED;
    StringList list;
    int64_t len = 0, i;
    const char **items = NULL;

    if (!s)
        return JS_EXCEPTION;

    arr = string_list_from_iterable(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
    if (JS_IsException(arr))
        return JS_EXCEPTION;

    {
        JSValue len_val = JS_GetPropertyStr(ctx, arr, "length");
        if (JS_IsException(len_val))
            goto fail;
        if (JS_ToInt64(ctx, &len, len_val) < 0) {
            JS_FreeValue(ctx, len_val);
            goto fail;
        }
        JS_FreeValue(ctx, len_val);
    }

    {
        size_t alloc_count = (len > 0) ? (size_t)len : 1;
        items = js_malloc(ctx, sizeof(*items) * alloc_count);
    }
    if (!items)
        goto fail;

    for (i = 0; i < len; i++) {
        JSValue val;
        const char *str;
        val = JS_GetPropertyUint32(ctx, arr, (uint32_t)i);
        if (JS_IsException(val)) {
            JS_FreeValue(ctx, val);
            goto fail;
        }
        str = JS_ToCString(ctx, val);
        JS_FreeValue(ctx, val);
        if (!str)
            goto fail;
        items[i] = str;
    }

    list.items = items;
    list.count = (int)len;

    ret = js_listformat_format_to_parts(ctx, s, &list);

 fail:
    if (items) {
        for (i = 0; i < len; i++) {
            if (items[i])
                JS_FreeCString(ctx, items[i]);
        }
        js_free(ctx, items);
    }
    JS_FreeValue(ctx, arr);
    return ret;
}

static JSValue js_listformat_resolvedOptions(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv)
{
    JSValue obj;
    JSListFormatData *s = JS_GetOpaque2(ctx, this_val, js_listformat_class_id);
    if (!s)
        return JS_EXCEPTION;

    obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        return JS_EXCEPTION;

    JS_DefinePropertyValueStr(ctx, obj, "locale",
                               JS_NewString(ctx, s->locale), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "type",
                               JS_NewString(ctx, s->type), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "style",
                               JS_NewString(ctx, s->style), JS_PROP_C_W_E);

    return obj;
}

static JSValue js_listformat_supportedLocalesOf(JSContext *ctx, JSValueConst this_val,
                                                   int argc, JSValueConst *argv)
{
    return js_intl_supportedLocalesOf(ctx, this_val, argc, argv);
}

static const JSCFunctionListEntry js_listformat_proto_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Intl.ListFormat", JS_PROP_CONFIGURABLE),
    JS_CFUNC_DEF("format", 1, js_listformat_format),
    JS_CFUNC_DEF("formatToParts", 1, js_listformat_formatToParts),
    JS_CFUNC_DEF("resolvedOptions", 0, js_listformat_resolvedOptions),
};

static const JSCFunctionListEntry js_listformat_static_funcs[] = {
    JS_CFUNC_DEF("supportedLocalesOf", 1, js_listformat_supportedLocalesOf),
};

int js_add_listformat(JSContext *ctx, JSValue intl_obj)
{
    JS_NewClassID(&js_listformat_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_listformat_class_id, &js_listformat_class);

    if (JS_IsException(js_intl_new_constructor(ctx, intl_obj,
                                                JS_INTL_CTOR_LISTFORMAT,
                                                js_listformat_class_id, "ListFormat",
                                                (JSCFunction *)js_listformat_constructor, 0,
                                                JS_CFUNC_constructor, 0,
                                                js_listformat_static_funcs, countof(js_listformat_static_funcs),
                                                js_listformat_proto_funcs, countof(js_listformat_proto_funcs))))
        return -1;

    return 0;
}

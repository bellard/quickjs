/*
 * QuickJS Intl.PluralRules implementation
 */

#include <string.h>
#include <math.h>

#include "../quickjs.h"
#include "../cutils.h"
#include "intl_private.h"

static JSClassID js_pluralrules_class_id;

enum {
    PR_TYPE_CARDINAL = 0,
    PR_TYPE_ORDINAL,
};
static const char *pr_type_names[] = {"cardinal", "ordinal"};

enum {
    PR_NOTATION_STANDARD = 0,
    PR_NOTATION_SCIENTIFIC,
    PR_NOTATION_ENGINEERING,
    PR_NOTATION_COMPACT,
};
static const char *pr_notation_names[] = {"standard", "scientific", "engineering", "compact"};

enum {
    PR_COMPACT_SHORT = 0,
    PR_COMPACT_LONG,
};
static const char *pr_compactdisplay_names[] = {"short", "long"};

enum {
    PR_ROUNDINGMODE_CEIL = 0,
    PR_ROUNDINGMODE_FLOOR,
    PR_ROUNDINGMODE_EXPAND,
    PR_ROUNDINGMODE_TRUNC,
    PR_ROUNDINGMODE_HALFCEIL,
    PR_ROUNDINGMODE_HALFFLOOR,
    PR_ROUNDINGMODE_HALFEXPAND,
    PR_ROUNDINGMODE_HALFTRUNC,
    PR_ROUNDINGMODE_HALFEVEN,
};
static const char *pr_roundingmode_names[] = {"ceil", "floor", "expand", "trunc", "halfCeil", "halfFloor", "halfExpand", "halfTrunc", "halfEven"};

enum {
    PR_ROUNDINGPRIORITY_AUTO = 0,
    PR_ROUNDINGPRIORITY_MOREPRECISION,
    PR_ROUNDINGPRIORITY_LESSPRECISION,
};
static const char *pr_roundingpriority_names[] = {"auto", "morePrecision", "lessPrecision"};

enum {
    PR_TRAILINGZERO_AUTO = 0,
    PR_TRAILINGZERO_STRIPIFINTEGER,
};
static const char *pr_trailingzero_names[] = {"auto", "stripIfInteger"};

typedef struct {
    char *locale;
    int type;
    int notation;
    int compactDisplay;      /* only meaningful when notation == compact */
    int minimumIntegerDigits;
    int minimumFractionDigits;
    int maximumFractionDigits;
    int minimumSignificantDigits;
    int maximumSignificantDigits;
    int hasSignificantDigits;
    int roundingIncrement;
    int roundingMode;
    int roundingPriority;
    int trailingZeroDisplay;
} JSPluralRulesData;

static void js_pluralrules_finalizer(JSRuntime *rt, JSValue val)
{
    JSPluralRulesData *s = JS_GetOpaque(val, js_pluralrules_class_id);
    if (s) {
        js_free_rt(rt, s->locale);
        js_free_rt(rt, s);
    }
}

static JSClassDef js_pluralrules_class = {
    "PluralRules",
    .finalizer = js_pluralrules_finalizer,
};

const char *plural_category_names[PLURAL_COUNT] = {
    "zero", "one", "two", "few", "many", "other"
};

/* Return plural categories array for locale and type.
   bitmap: bit i set => include PLURAL_* category i.
   For cardinal: returns categories in canonical order. */
static JSValue js_pluralrules_get_categories(JSContext *ctx, const char *locale, int type)
{
    JSValue arr;
    int bitmap;
    int i;
    uint32_t idx;

    if (type == PR_TYPE_ORDINAL) {
        bitmap = (1 << PLURAL_ONE) | (1 << PLURAL_TWO) | (1 << PLURAL_FEW) | (1 << PLURAL_OTHER);
    } else if (!strcmp(locale, "ar")) {
        bitmap = (1 << PLURAL_ZERO) | (1 << PLURAL_ONE) | (1 << PLURAL_TWO) |
                 (1 << PLURAL_FEW) | (1 << PLURAL_MANY) | (1 << PLURAL_OTHER);
    } else if (!strcmp(locale, "fa")) {
        bitmap = (1 << PLURAL_ONE) | (1 << PLURAL_OTHER);
    } else if (!strcmp(locale, "fr")) {
        bitmap = (1 << PLURAL_ONE) | (1 << PLURAL_MANY) | (1 << PLURAL_OTHER);
    } else if (!strcmp(locale, "gv")) {
        bitmap = (1 << PLURAL_ONE) | (1 << PLURAL_TWO) | (1 << PLURAL_FEW) |
                 (1 << PLURAL_MANY) | (1 << PLURAL_OTHER);
    } else if (!strcmp(locale, "ko") || !strcmp(locale, "ja") ||
               strstart(locale, "zh", NULL)) {
        bitmap = (1 << PLURAL_OTHER);
    } else if (!strcmp(locale, "sl")) {
        bitmap = (1 << PLURAL_ONE) | (1 << PLURAL_TWO) | (1 << PLURAL_FEW) | (1 << PLURAL_OTHER);
    } else if (strstart(locale, "ru", NULL)) {
        bitmap = (1 << PLURAL_ONE) | (1 << PLURAL_FEW) | (1 << PLURAL_MANY) | (1 << PLURAL_OTHER);
    } else {
        /* Default: en, etc. */
        bitmap = (1 << PLURAL_ONE) | (1 << PLURAL_OTHER);
    }

    arr = JS_NewArray(ctx);
    if (JS_IsException(arr))
        return JS_EXCEPTION;

    idx = 0;
    for (i = 0; i < PLURAL_COUNT; i++) {
        if (bitmap & (1 << i)) {
            JSValue val = JS_NewString(ctx, plural_category_names[i]);
            if (JS_IsException(val)) {
                JS_FreeValue(ctx, arr);
                return JS_EXCEPTION;
            }
            if (JS_DefinePropertyValueUint32(ctx, arr, idx++, val, JS_PROP_C_W_E) < 0) {
                JS_FreeValue(ctx, arr);
                return JS_EXCEPTION;
            }
        }
    }
    return arr;
}

static JSValue js_pluralrules_constructor(JSContext *ctx,
                                           JSValueConst new_target,
                                           int argc, JSValueConst *argv)
{
    JSPluralRulesData *s;
    JSValue obj = JS_UNDEFINED;
    JSValue options = JS_UNDEFINED;
    int v;
    int ret;

    s = js_mallocz(ctx, sizeof(*s));
    if (!s)
        return JS_EXCEPTION;

    s->locale = js_strdup(ctx, "en-US");
    s->type = PR_TYPE_CARDINAL;
    s->notation = PR_NOTATION_STANDARD;
    s->minimumIntegerDigits = 1;
    s->minimumFractionDigits = 0;
    s->maximumFractionDigits = 3;
    s->roundingIncrement = 1;
    s->roundingMode = PR_ROUNDINGMODE_HALFEVEN;
    s->roundingPriority = PR_ROUNDINGPRIORITY_AUTO;
    s->trailingZeroDisplay = PR_TRAILINGZERO_AUTO;

    {
        char *loc = js_intl_extract_locale(ctx, argc, argv);
        if (!loc)
            goto fail;
        js_free(ctx, s->locale);
        s->locale = loc;
    }

    /* Handle options */
    if (argc > 1 && !JS_IsUndefined(argv[1])) {
        options = JS_ToObject(ctx, argv[1]);
        if (JS_IsException(options))
            goto fail;
    } else {
        options = JS_NewObjectProto(ctx, JS_NULL);
        if (JS_IsException(options))
            goto fail;
    }

    /* Read localeMatcher (from ResolveOptions) */
    {
        int idx = js_intl_get_string_option(ctx, options, "localeMatcher",
                                             intl_localeMatcher_names,
                                             countof(intl_localeMatcher_names),
                                             INTL_LOCALEMATCHER_BESTFIT);
        if (idx < 0)
            goto fail;
    }

    /* Read type */
    {
        int idx = js_intl_get_string_option(ctx, options, "type",
                                             pr_type_names,
                                             countof(pr_type_names),
                                             PR_TYPE_CARDINAL);
        if (idx < 0)
            goto fail;
        s->type = idx;
    }

    /* Read notation */
    {
        int idx = js_intl_get_string_option(ctx, options, "notation",
                                             pr_notation_names,
                                             countof(pr_notation_names),
                                             PR_NOTATION_STANDARD);
        if (idx < 0)
            goto fail;
        s->notation = idx;
    }

    /* Read compactDisplay */
    {
        int idx = js_intl_get_string_option(ctx, options, "compactDisplay",
                                             pr_compactdisplay_names,
                                             countof(pr_compactdisplay_names),
                                             PR_COMPACT_SHORT);
        if (idx < 0)
            goto fail;
        if (s->notation == PR_NOTATION_COMPACT)
            s->compactDisplay = idx;
    }

    /* SetNumberFormatDigitOptions */
    ret = js_intl_get_number_option(ctx, options, "minimumIntegerDigits", &v, 1, 21);
    if (ret < 0) goto fail;
    if (ret > 0) s->minimumIntegerDigits = v;

    ret = js_intl_get_number_option(ctx, options, "minimumFractionDigits", &v, 0, 100);
    if (ret < 0) goto fail;
    if (ret > 0) s->minimumFractionDigits = v;

    ret = js_intl_get_number_option(ctx, options, "maximumFractionDigits", &v, 0, 100);
    if (ret < 0) goto fail;
    if (ret > 0) s->maximumFractionDigits = v;

    {
        int has_mnsd = 0, has_mxsd = 0;
        int mnsd = 0, mxsd = 0;

        ret = js_intl_get_number_option(ctx, options, "minimumSignificantDigits", &mnsd, 1, 21);
        if (ret < 0) goto fail;
        has_mnsd = ret;

        ret = js_intl_get_number_option(ctx, options, "maximumSignificantDigits", &mxsd, 1, 21);
        if (ret < 0) goto fail;
        has_mxsd = ret;

        if (has_mnsd && has_mxsd) {
            s->minimumSignificantDigits = mnsd;
            s->maximumSignificantDigits = mxsd;
            s->hasSignificantDigits = 1;
        }
    }

    ret = js_intl_get_number_option(ctx, options, "roundingIncrement", &v, 1, 5000);
    if (ret < 0) goto fail;
    if (ret > 0) s->roundingIncrement = v;

    {
        int idx = js_intl_get_string_option(ctx, options, "roundingMode",
                                             pr_roundingmode_names,
                                             countof(pr_roundingmode_names),
                                             PR_ROUNDINGMODE_HALFEVEN);
        if (idx < 0)
            goto fail;
        s->roundingMode = idx;
    }

    {
        int idx = js_intl_get_string_option(ctx, options, "roundingPriority",
                                             pr_roundingpriority_names,
                                             countof(pr_roundingpriority_names),
                                             PR_ROUNDINGPRIORITY_AUTO);
        if (idx < 0)
            goto fail;
        s->roundingPriority = idx;
    }

    {
        int idx = js_intl_get_string_option(ctx, options, "trailingZeroDisplay",
                                             pr_trailingzero_names,
                                             countof(pr_trailingzero_names),
                                             PR_TRAILINGZERO_AUTO);
        if (idx < 0)
            goto fail;
        s->trailingZeroDisplay = idx;
    }

    JS_FreeValue(ctx, options);
    options = JS_UNDEFINED;

    obj = JS_CreateFromConstructor(ctx, new_target, js_pluralrules_class_id);
    if (JS_IsException(obj))
        goto fail;
    JS_SetOpaque(obj, s);
    return obj;
 fail:
    JS_FreeValue(ctx, options);
    if (s->locale) js_free(ctx, s->locale);
    js_free(ctx, s);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static int is_power_of_10(double n)
{
    if (n <= 0 || floor(n) != n)
        return 0;
    while (n > 1) {
        if (fmod(n, 10.0) != 0)
            return 0;
        n /= 10.0;
    }
    return n == 1.0;
}

static JSValue js_pluralrules_select(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    JSPluralRulesData *s = JS_GetOpaque2(ctx, this_val, js_pluralrules_class_id);
    double n, abs_n;
    int ret;
    if (!s)
        return JS_EXCEPTION;
    if (JS_IsUndefined(argv[0]))
        return JS_ThrowTypeError(ctx, "select requires a value");
    ret = JS_ToFloat64(ctx, &n, argv[0]);
    if (ret < 0)
        return JS_EXCEPTION;
    if (isnan(n) || isinf(n))
        return JS_NewString(ctx, "other");

    abs_n = fabs(n);

    if (!strcmp(s->locale, "ko") || !strcmp(s->locale, "ja") ||
        strstart(s->locale, "zh", NULL)) {
        return JS_NewString(ctx, plural_category_names[PLURAL_OTHER]);
    }

    if (strstart(s->locale, "fr", NULL)) {
        if (abs_n < 2.0)
            return JS_NewString(ctx, plural_category_names[PLURAL_ONE]);
        if (s->notation == PR_NOTATION_COMPACT && abs_n >= 1000000.0)
            return JS_NewString(ctx, plural_category_names[PLURAL_MANY]);
        if (is_power_of_10(abs_n))
            return JS_NewString(ctx, plural_category_names[PLURAL_MANY]);
        return JS_NewString(ctx, plural_category_names[PLURAL_OTHER]);
    }

    if (strstart(s->locale, "ru", NULL)) {
        int iv = (int)abs_n;
        int mod10 = iv % 10;
        int mod100 = iv % 100;
        if (mod10 == 1 && mod100 != 11)
            return JS_NewString(ctx, plural_category_names[PLURAL_ONE]);
        if (mod10 >= 2 && mod10 <= 4 && (mod100 < 10 || mod100 >= 20))
            return JS_NewString(ctx, plural_category_names[PLURAL_FEW]);
        return JS_NewString(ctx, plural_category_names[PLURAL_MANY]);
    }

    /* Default English-like rule */
    if (abs_n == 1.0)
        return JS_NewString(ctx, plural_category_names[PLURAL_ONE]);
    return JS_NewString(ctx, plural_category_names[PLURAL_OTHER]);
}

static JSValue js_pluralrules_selectRange(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv)
{
    JSPluralRulesData *s = JS_GetOpaque2(ctx, this_val, js_pluralrules_class_id);
    double start, end;
    int ret;
    if (!s)
        return JS_EXCEPTION;
    if (JS_IsUndefined(argv[0]) || JS_IsUndefined(argv[1]))
        return JS_ThrowTypeError(ctx, "selectRange requires two arguments");
    ret = JS_ToFloat64(ctx, &start, argv[0]);
    if (ret < 0)
        return JS_EXCEPTION;
    ret = JS_ToFloat64(ctx, &end, argv[1]);
    if (ret < 0)
        return JS_EXCEPTION;
    if (isnan(start) || isnan(end))
        return JS_ThrowRangeError(ctx, "selectRange argument is NaN");
    return JS_NewString(ctx, "other");
}

static int js_add_prop_str(JSContext *ctx, JSValue obj, const char *name, const char *val)
{
    JSValue v = JS_NewString(ctx, val);
    if (JS_IsException(v))
        return -1;
    return JS_DefinePropertyValueStr(ctx, obj, name, v, JS_PROP_C_W_E);
}

static int js_add_prop_int32(JSContext *ctx, JSValue obj, const char *name, int32_t val)
{
    return JS_DefinePropertyValueStr(ctx, obj, name, JS_NewInt32(ctx, val), JS_PROP_C_W_E);
}

static JSValue js_pluralrules_resolvedOptions(JSContext *ctx, JSValueConst this_val,
                                               int argc, JSValueConst *argv)
{
    JSValue obj;
    JSPluralRulesData *s = JS_GetOpaque2(ctx, this_val, js_pluralrules_class_id);
    if (!s)
        return JS_EXCEPTION;

    obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        return JS_EXCEPTION;

    if (js_add_prop_str(ctx, obj, "locale", s->locale) < 0 ||
        js_add_prop_str(ctx, obj, "type", pr_type_names[s->type]) < 0 ||
        js_add_prop_str(ctx, obj, "notation", pr_notation_names[s->notation]) < 0)
        goto fail;
    if (s->notation == PR_NOTATION_COMPACT) {
        if (js_add_prop_str(ctx, obj, "compactDisplay", pr_compactdisplay_names[s->compactDisplay]) < 0)
            goto fail;
    }
    if (js_add_prop_int32(ctx, obj, "minimumIntegerDigits", s->minimumIntegerDigits) < 0 ||
        js_add_prop_int32(ctx, obj, "minimumFractionDigits", s->minimumFractionDigits) < 0 ||
        js_add_prop_int32(ctx, obj, "maximumFractionDigits", s->maximumFractionDigits) < 0)
        goto fail;
    if (s->hasSignificantDigits) {
        if (js_add_prop_int32(ctx, obj, "minimumSignificantDigits", s->minimumSignificantDigits) < 0 ||
            js_add_prop_int32(ctx, obj, "maximumSignificantDigits", s->maximumSignificantDigits) < 0)
            goto fail;
    }
    {
        JSValue cats = js_pluralrules_get_categories(ctx, s->locale, s->type);
        if (JS_IsException(cats)) {
            JS_FreeValue(ctx, obj);
            return JS_EXCEPTION;
        }
        if (JS_DefinePropertyValueStr(ctx, obj, "pluralCategories", cats, JS_PROP_C_W_E) < 0)
            goto fail;
    }
    if (js_add_prop_int32(ctx, obj, "roundingIncrement", s->roundingIncrement) < 0 ||
        js_add_prop_str(ctx, obj, "roundingMode", pr_roundingmode_names[s->roundingMode]) < 0 ||
        js_add_prop_str(ctx, obj, "roundingPriority", pr_roundingpriority_names[s->roundingPriority]) < 0 ||
        js_add_prop_str(ctx, obj, "trailingZeroDisplay", pr_trailingzero_names[s->trailingZeroDisplay]) < 0)
        goto fail;

    return obj;
fail:
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static JSValue js_pluralrules_supportedLocalesOf(JSContext *ctx, JSValueConst this_val,
                                                    int argc, JSValueConst *argv)
{
    return js_intl_supportedLocalesOf(ctx, this_val, argc, argv);
}

static const JSCFunctionListEntry js_pluralrules_proto_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Intl.PluralRules", JS_PROP_CONFIGURABLE),
    JS_CFUNC_DEF("select", 1, js_pluralrules_select),
    JS_CFUNC_DEF("selectRange", 2, js_pluralrules_selectRange),
    JS_CFUNC_DEF("resolvedOptions", 0, js_pluralrules_resolvedOptions),
};

static const JSCFunctionListEntry js_pluralrules_static_funcs[] = {
    JS_CFUNC_DEF("supportedLocalesOf", 1, js_pluralrules_supportedLocalesOf),
};

int js_add_pluralrules(JSContext *ctx, JSValue intl_obj)
{
    JS_NewClassID(&js_pluralrules_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_pluralrules_class_id, &js_pluralrules_class);

    if (JS_IsException(js_intl_new_constructor(ctx, intl_obj,
                                                -1,
                                                js_pluralrules_class_id, "PluralRules",
                                                (JSCFunction *)js_pluralrules_constructor, 0,
                                                JS_CFUNC_constructor, 0,
                                                js_pluralrules_static_funcs, countof(js_pluralrules_static_funcs),
                                                js_pluralrules_proto_funcs, countof(js_pluralrules_proto_funcs))))
        return -1;

    return 0;
}

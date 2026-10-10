/*
 * QuickJS Intl.DisplayNames implementation
 */

#include <string.h>

#include "../quickjs.h"
#include "../cutils.h"
#include "intl_private.h"

static JSClassID js_displaynames_class_id;

typedef struct {
    char *locale;
    char *style;
    char *type;
    char *fallback;
    char *languageDisplay;
} JSDisplayNamesData;

static void js_displaynames_finalizer(JSRuntime *rt, JSValue val)
{
    JSDisplayNamesData *s = JS_GetOpaque(val, js_displaynames_class_id);
    if (s) {
        js_free_rt(rt, s->locale);
        js_free_rt(rt, s->style);
        js_free_rt(rt, s->type);
        js_free_rt(rt, s->fallback);
        js_free_rt(rt, s->languageDisplay);
        js_free_rt(rt, s);
    }
}

static const char *dn_style_names[] = {"narrow", "short", "long"};
static const char *dn_type_names[] = {"language", "region", "script", "currency", "calendar", "dateTimeField"};
static const char *dn_fallback_names[] = {"code", "none"};
static const char *dn_languageDisplay_names[] = {"dialect", "standard"};

static JSClassDef js_displaynames_class = {
    "DisplayNames",
    .finalizer = js_displaynames_finalizer,
};

static JSValue js_displaynames_constructor(JSContext *ctx,
                                            JSValueConst new_target,
                                            int argc, JSValueConst *argv)
{
    JSDisplayNamesData *s;
    JSValue obj = JS_UNDEFINED;
    JSValue options = JS_UNDEFINED;

    s = js_mallocz(ctx, sizeof(*s));
    if (!s)
        return JS_EXCEPTION;

    s->locale = js_strdup(ctx, "en-US");
    s->style = js_strdup(ctx, "long");
    s->type = NULL;
    s->fallback = js_strdup(ctx, "code");
    s->languageDisplay = js_strdup(ctx, "dialect");

    /* Step 2: Create object first (before any option processing) */
    obj = JS_CreateFromConstructor(ctx, new_target, js_displaynames_class_id);
    if (JS_IsException(obj))
        goto fail;

    {
        char *loc = js_intl_extract_locale(ctx, argc, argv);
        if (!loc)
            goto fail;
        js_free(ctx, s->locale);
        s->locale = loc;
    }

    /* Coerce options to object */
    options = js_intl_get_options_object(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
    if (JS_IsException(options))
        goto fail;

    /* localeMatcher first (spec order via ResolveOptions) */
    {
        int idx = js_intl_get_string_option(ctx, options, "localeMatcher",
                                             intl_localeMatcher_names,
                                             countof(intl_localeMatcher_names),
                                             INTL_LOCALEMATCHER_BESTFIT);
        if (idx < 0)
            goto fail_options;
    }
    /* style */
    {
        int idx = js_intl_get_string_option(ctx, options, "style",
                                             dn_style_names,
                                             countof(dn_style_names),
                                             2 /* "long" */);
        if (idx < 0)
            goto fail_options;
        js_free(ctx, s->style);
        s->style = js_strdup(ctx, dn_style_names[idx]);
        if (!s->style)
            goto fail_options;
    }
    /* type (required) */
    {
        JSValue v = JS_GetPropertyStr(ctx, options, "type");
        const char *str;
        int found = 0;
        size_t i;
        if (JS_IsException(v))
            goto fail_options;
        if (JS_IsUndefined(v)) {
            JS_ThrowTypeError(ctx, "type option is required");
            JS_FreeValue(ctx, v);
            goto fail_options;
        }
        str = JS_ToCStringNZ(ctx, v);
        JS_FreeValue(ctx, v);
        if (!str)
            goto fail_options;
        for (i = 0; i < countof(dn_type_names); i++) {
            if (!strcmp(str, dn_type_names[i])) {
                found = 1;
                break;
            }
        }
        if (!found) {
            JS_ThrowRangeError(ctx, "invalid type option");
            JS_FreeCString(ctx, str);
            goto fail_options;
        }
        js_free(ctx, s->type);
        s->type = js_strdup(ctx, str);
        JS_FreeCString(ctx, str);
        if (!s->type)
            goto fail_options;
    }
    /* fallback */
    {
        int idx = js_intl_get_string_option(ctx, options, "fallback",
                                             dn_fallback_names,
                                             countof(dn_fallback_names),
                                             0 /* "code" */);
        if (idx < 0)
            goto fail_options;
        js_free(ctx, s->fallback);
        s->fallback = js_strdup(ctx, dn_fallback_names[idx]);
        if (!s->fallback)
            goto fail_options;
    }
    /* languageDisplay */
    {
        int idx = js_intl_get_string_option(ctx, options, "languageDisplay",
                                             dn_languageDisplay_names,
                                             countof(dn_languageDisplay_names),
                                             0 /* "dialect" */);
        if (idx < 0)
            goto fail_options;
        js_free(ctx, s->languageDisplay);
        s->languageDisplay = js_strdup(ctx, dn_languageDisplay_names[idx]);
        if (!s->languageDisplay)
            goto fail_options;
    }

    JS_FreeValue(ctx, options);
    JS_SetOpaque(obj, s);
    return obj;
 fail_options:
    JS_FreeValue(ctx, options);
 fail:
    if (s->locale) js_free(ctx, s->locale);
    if (s->style) js_free(ctx, s->style);
    if (s->type) js_free(ctx, s->type);
    if (s->fallback) js_free(ctx, s->fallback);
    if (s->languageDisplay) js_free(ctx, s->languageDisplay);
    js_free(ctx, s);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

#define dn_is_alpha(c) (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))
#define dn_is_digit(c) ((c) >= '0' && (c) <= '9')
#define dn_is_alnum(c) (dn_is_alpha(c) || dn_is_digit(c))

static int language_has_extension(const char *str)
{
    const char *p = str;
    int subtag_len = 0;
    int subtag_idx = 0;
    while (*p) {
        if (*p == '-') {
            if (subtag_idx > 0 && subtag_len == 1)
                return 1;
            subtag_idx++;
            subtag_len = 0;
        } else {
            subtag_len++;
        }
        p++;
    }
    if (subtag_idx > 0 && subtag_len == 1)
        return 1;
    return 0;
}

static int is_valid_region(const char *str)
{
    size_t len = strlen(str);
    size_t i;
    if (len == 2) {
        for (i = 0; i < len; i++) {
            if (!dn_is_alpha(str[i]))
                return 0;
        }
        return 1;
    }
    if (len == 3) {
        for (i = 0; i < len; i++) {
            if (!dn_is_digit(str[i]))
                return 0;
        }
        return 1;
    }
    return 0;
}

static int is_valid_calendar(const char *str)
{
    size_t len = strlen(str);
    size_t i;
    int seg_len = 0;
    if (len < 3)
        return 0;
    for (i = 0; i < len; i++) {
        char c = str[i];
        if (c == '-') {
            if (seg_len < 3 || seg_len > 8)
                return 0;
            seg_len = 0;
        } else if (dn_is_alpha(c) || dn_is_digit(c)) {
            seg_len++;
        } else {
            return 0;
        }
    }
    if (seg_len < 3 || seg_len > 8)
        return 0;
    return 1;
}

static int is_valid_datetimefield(const char *str)
{
    static const char *valid[] = {
        "era", "year", "quarter", "month", "weekOfYear", "weekday",
        "day", "dayPeriod", "hour", "minute", "second", "timeZoneName"
    };
    size_t i;
    for (i = 0; i < countof(valid); i++) {
        if (!strcmp(str, valid[i]))
            return 1;
    }
    return 0;
}

/* Fallback for compound language tags not present in CLDR */
static const char *dn_fallback_language(const char *code)
{
    if (!strcmp(code, "zh-Hans-CN"))
        return "Chinese (Simplified, China)";
    if (!strcmp(code, "zh-Hant-TW"))
        return "Chinese (Traditional, Taiwan)";
    return NULL;
}

static const char *dn_calendar_alias(const char *code)
{
    if (!strcmp(code, "gregory"))
        return "gregorian";
    if (!strcmp(code, "ethioaa"))
        return "ethiopic-amete-alem";
    return code;
}

static const DisplayNamesData *dn_find_locale_data(const char *locale)
{
    int i;
    for (i = 0; i < display_names_count; i++) {
        if (strstart(locale, display_names_data[i].locale, NULL))
            return &display_names_data[i];
    }
    return NULL;
}

static JSValue js_displaynames_of(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    JSDisplayNamesData *s = JS_GetOpaque2(ctx, this_val, js_displaynames_class_id);
    const char *code;
    const char *name = NULL;
    int use_short = 0;
    const DisplayNamesData *dn = NULL;
    if (!s)
        return JS_EXCEPTION;
    code = JS_ToCString(ctx, argv[0]);
    if (!code)
        return JS_EXCEPTION;
    if (!s->type) {
        JS_FreeCString(ctx, code);
        return JS_ThrowTypeError(ctx, "DisplayNames type not set");
    }
    if (!strcmp(s->type, "language")) {
        if (!is_wellformed_language_tag(code)) {
            JS_FreeCString(ctx, code);
            return JS_ThrowRangeError(ctx, "invalid language code");
        } else if (language_has_extension(code)) {
            JS_FreeCString(ctx, code);
            return JS_ThrowRangeError(ctx, "invalid language code");
        }
    } else if (!strcmp(s->type, "region")) {
        if (!is_valid_region(code)) {
            JS_FreeCString(ctx, code);
            return JS_ThrowRangeError(ctx, "invalid region code");
        }
    } else if (!strcmp(s->type, "calendar")) {
        if (!is_valid_calendar(code)) {
            JS_FreeCString(ctx, code);
            return JS_ThrowRangeError(ctx, "invalid calendar code");
        }
    } else if (!strcmp(s->type, "dateTimeField")) {
        if (!is_valid_datetimefield(code)) {
            JS_FreeCString(ctx, code);
            return JS_ThrowRangeError(ctx, "invalid dateTimeField code");
        }
    } else if (!strcmp(s->type, "currency")) {
        size_t len = strlen(code);
        if (len != 3 || !(code[0] >= 'A' && code[0] <= 'Z') ||
            !(code[1] >= 'A' && code[1] <= 'Z') ||
            !(code[2] >= 'A' && code[2] <= 'Z')) {
            JS_FreeCString(ctx, code);
            return JS_ThrowRangeError(ctx, "invalid currency code");
        }
    }

    dn = dn_find_locale_data(s->locale);
    if (dn) {
        if (!strcmp(s->type, "language")) {
            name = js_intl_find_display_name(dn->languages, code);
            if (!name)
                name = dn_fallback_language(code);
        } else if (!strcmp(s->type, "region")) {
            if (!strcmp(s->style, "short") || !strcmp(s->style, "narrow"))
                use_short = 1;
            if (use_short)
                name = js_intl_find_display_name(dn->regions_short, code);
            if (!name)
                name = js_intl_find_display_name(dn->regions, code);
        } else if (!strcmp(s->type, "script")) {
            name = js_intl_find_display_name(dn->scripts, code);
        } else if (!strcmp(s->type, "calendar")) {
            name = js_intl_find_display_name(dn->calendars, dn_calendar_alias(code));
        } else if (!strcmp(s->type, "dateTimeField")) {
            name = js_intl_find_display_name(dn->dateTimeFields, code);
        } else if (!strcmp(s->type, "currency")) {
            name = js_intl_find_display_name(dn->currencies, code);
        }
    }

    if (!name) {
        if (!strcmp(s->fallback, "none")) {
            JS_FreeCString(ctx, code);
            return JS_UNDEFINED;
        }
        /* fallback "code": return the code itself */
        if (!strcmp(s->type, "currency")) {
            JS_FreeCString(ctx, code);
            return JS_NewString(ctx, code);
        }
        JS_FreeCString(ctx, code);
        return JS_NewString(ctx, code);
    }

    JS_FreeCString(ctx, code);
    return JS_NewString(ctx, name);
}

static JSValue js_displaynames_resolvedOptions(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv)
{
    JSValue obj;
    JSDisplayNamesData *s = JS_GetOpaque2(ctx, this_val, js_displaynames_class_id);
    if (!s)
        return JS_EXCEPTION;

    obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        return JS_EXCEPTION;

    JS_DefinePropertyValueStr(ctx, obj, "locale",
                               JS_NewString(ctx, s->locale), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "style",
                               JS_NewString(ctx, s->style), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "type",
                               JS_NewString(ctx, s->type), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "fallback",
                               JS_NewString(ctx, s->fallback), JS_PROP_C_W_E);
    if (s->type && !strcmp(s->type, "language"))
        JS_DefinePropertyValueStr(ctx, obj, "languageDisplay",
                                   JS_NewString(ctx, s->languageDisplay), JS_PROP_C_W_E);

    return obj;
}

static JSValue js_displaynames_supportedLocalesOf(JSContext *ctx, JSValueConst this_val,
                                                   int argc, JSValueConst *argv)
{
    return js_intl_supportedLocalesOf(ctx, this_val, argc, argv);
}

static const JSCFunctionListEntry js_displaynames_proto_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Intl.DisplayNames", JS_PROP_CONFIGURABLE),
    JS_CFUNC_DEF("of", 1, js_displaynames_of),
    JS_CFUNC_DEF("resolvedOptions", 0, js_displaynames_resolvedOptions),
};

static const JSCFunctionListEntry js_displaynames_static_funcs[] = {
    JS_CFUNC_DEF("supportedLocalesOf", 1, js_displaynames_supportedLocalesOf),
};

int js_add_displaynames(JSContext *ctx, JSValue intl_obj)
{
    JS_NewClassID(&js_displaynames_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_displaynames_class_id, &js_displaynames_class);

    if (JS_IsException(js_intl_new_constructor(ctx, intl_obj,
                                                -1,
                                                js_displaynames_class_id, "DisplayNames",
                                                (JSCFunction *)js_displaynames_constructor, 2,
                                                JS_CFUNC_constructor, 0,
                                                js_displaynames_static_funcs, countof(js_displaynames_static_funcs),
                                                js_displaynames_proto_funcs, countof(js_displaynames_proto_funcs))))
        return -1;

    return 0;
}

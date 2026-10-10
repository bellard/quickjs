/*
 * QuickJS Intl implementation (ECMA-402)
 */

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>

#include "../quickjs.h"
#include "../cutils.h"
#include "intl_private.h"
#include "../temporal/calendar.h"
#include "../temporal/timezone.h"

const char *intl_localeMatcher_names[] = {"lookup", "best fit"};

JSValue js_canonicalizeLocaleList(JSContext *ctx, JSValueConst locales);
static void js_intl_set_locale_callbacks(JSContext *ctx);

/* --------------------------------------------------------------------- */
/* Shared option helpers                                                 */
/* --------------------------------------------------------------------- */

JSValue js_intl_get_options_object(JSContext *ctx, JSValueConst options)
{
    if (JS_IsUndefined(options))
        return JS_NewObjectProto(ctx, JS_NULL);
    if (!JS_IsObject(options))
        return JS_ThrowTypeError(ctx, "options must be an object");
    return JS_DupValue(ctx, options);
}

int js_intl_get_string_option(JSContext *ctx, JSValueConst options, const char *prop,
                               const char **valid_values, int num_values, int default_index)
{
    JSValue val = JS_GetPropertyStr(ctx, options, prop);
    size_t slen;
    const char *str;
    int i;

    if (JS_IsException(val))
        return -1;
    if (JS_IsUndefined(val))
        return default_index;

    str = JS_ToCStringLen(ctx, &slen, val);
    JS_FreeValue(ctx, val);
    if (!str)
        return -1;

    for (i = 0; i < num_values; i++) {
        size_t vlen = strlen(valid_values[i]);
        if (slen == vlen && memcmp(str, valid_values[i], vlen) == 0) {
            JS_FreeCString(ctx, str);
            return i;
        }
    }

    JS_ThrowRangeError(ctx, "invalid %s", prop);
    JS_FreeCString(ctx, str);
    return -1;
}

int js_intl_get_number_option(JSContext *ctx, JSValueConst options, const char *prop,
                               int *pval, int val_min, int val_max)
{
    JSValue val = JS_GetPropertyStr(ctx, options, prop);
    double d;
    int ret;

    if (JS_IsException(val))
        return -1;
    if (JS_IsUndefined(val)) {
        JS_FreeValue(ctx, val);
        return 0;
    }

    ret = JS_ToFloat64(ctx, &d, val);
    JS_FreeValue(ctx, val);
    if (ret < 0)
        return -1;

    if (isnan(d) || isinf(d) || d < val_min || d > val_max) {
        JS_ThrowRangeError(ctx, "%s out of range", prop);
        return -1;
    }
    *pval = (int)d;
    return 1;
}

/* --------------------------------------------------------------------- */
/* Shared supportedLocalesOf helper                                      */
/* --------------------------------------------------------------------- */

/* Strip Unicode extensions (-u-...) from locale string */
char *js_intl_strip_unicode_extensions(JSContext *ctx, const char *locale)
{
    const char *p = strstr(locale, "-u-");
    if (!p) {
        /* also check uppercase, though unlikely */
        p = strstr(locale, "-U-");
    }
    if (p) {
        size_t len = (size_t)(p - locale);
        char *res = js_malloc(ctx, len + 1);
        if (res) {
            memcpy(res, locale, len);
            res[len] = '\0';
        }
        return res;
    }
    return js_strdup(ctx, locale);
}

/* Extract locale from Intl constructor first argument.
   Uses CanonicalizeLocaleList then returns the first locale with
   Unicode extensions stripped, or "en-US" if list is empty.
   Returns a newly allocated string, or NULL on error/exception.
   Caller must js_free the result. */
const char *intl_all_numbering_systems[] = {
    "adlm", "ahom", "arab", "arabext", "armn", "armnlow", "bali", "beng", "bhks",
    "brah", "cakm", "cham", "cyrl", "deva", "diak", "ethi", "finance", "fullwide",
    "gara", "geor", "gong", "gonm", "grek", "greklow", "gujr", "gukh", "guru",
    "hanidays", "hanidec", "hans", "hansfin", "hant", "hantfin", "hebr", "hmng",
    "hmnp", "java", "jpan", "jpanfin", "jpanyear", "kali", "kawi", "khmr", "knda",
    "krai", "lana", "lanatham", "laoo", "latn", "lepc", "limb", "mathbold", "mathdbl",
    "mathmono", "mathsanb", "mathsans", "mlym", "modi", "mong", "mroo", "mtei",
    "mymr", "mymrepka", "mymrpao", "mymrshan", "mymrtlng", "nagm", "native", "newa",
    "nkoo", "olck", "onao", "orya", "osma", "outlined", "rohg", "roman", "romanlow",
    "saur", "segment", "shrd", "sind", "sinh", "sora", "sund", "sunu", "takr",
    "talu", "taml", "tamldec", "telu", "thai", "tibt", "tirh", "tnsa", "tols",
    "traditio", "vaii", "wara", "wcho"
};
const int intl_all_numbering_systems_count = countof(intl_all_numbering_systems);

const char *intl_supported_collations[] = {
    "eor", "phonebk"
};
const int intl_supported_collations_count = countof(intl_supported_collations);

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

static int is_valid_numbering_system(const char *name)
{
    if (!strcmp(name, "native") || !strcmp(name, "traditio") || !strcmp(name, "finance"))
        return 0;
    return bsearch(&name, intl_all_numbering_systems,
                   (size_t)intl_all_numbering_systems_count, sizeof(char *),
                   cmp_str) != NULL;
}

int js_intl_string_to_numbering_system(const char *str)
{
    const char **p;
    if (!str)
        return NS_UNKNOWN;
    p = bsearch(&str, intl_all_numbering_systems,
                (size_t)intl_all_numbering_systems_count, sizeof(char *),
                cmp_str);
    if (p)
        return (int)(p - intl_all_numbering_systems);
    return NS_UNKNOWN;
}

const char *js_intl_numbering_system_to_string(int ns)
{
    if (ns >= 0 && ns < intl_all_numbering_systems_count)
        return intl_all_numbering_systems[ns];
    return NULL;
}

/* Extract a Unicode extension keyword value from a canonicalized locale string.
   key must be exactly 2 characters. Returns a newly allocated string, or NULL.
   For 'nu', validates against known numbering systems. */
const char *js_intl_find_u_extension(const char *locale)
{
    const char *u = strstr(locale, "-u-");
    const char *x = strstr(locale, "-x-");
    if (u && x && u > x)
        return NULL;
    return u;
}

int js_intl_locale_has_key(const char *locale, const char *key)
{
    const char *u = js_intl_find_u_extension(locale);
    if (!u)
        return 0;
    u += 3;
    while (*u) {
        const char *next = u;
        while (*next && *next != '-')
            next++;
        int len = (int)(next - u);
        if (len == 2 && u[0] == key[0] && u[1] == key[1])
            return 1;
        if (*next == '-')
            next++;
        u = next;
    }
    return 0;
}

char *js_intl_locale_get_unicode_key(JSContext *ctx, const char *locale, const char *key)
{
    const char *u_ext = js_intl_find_u_extension(locale);
    if (!u_ext)
        return NULL;
    u_ext += 3;
    while (*u_ext) {
        const char *next = u_ext;
        while (*next && *next != '-')
            next++;
        int key_len = (int)(next - u_ext);
        if (key_len == 2 && u_ext[0] == key[0] && u_ext[1] == key[1]) {
            const char *val = next;
            if (*val == '-')
                val++;
            const char *val_end = val;
            /* read all subtags until end, singleton, or another key */
            while (*val_end) {
                const char *sub_end = val_end;
                while (*sub_end && *sub_end != '-')
                    sub_end++;
                int sub_len = (int)(sub_end - val_end);
                /* singleton (1 char) ends this extension block */
                if (sub_len == 1)
                    break;
                /* another key (2 chars) means our key's value ends here */
                if (sub_len == 2)
                    break;
                val_end = sub_end;
                if (*val_end == '-')
                    val_end++;
            }
            size_t val_len = (size_t)(val_end - val);
            /* trim trailing hyphen if any */
            if (val_len > 0 && val[val_len - 1] == '-')
                val_len--;
            if (val_len == 0)
                return NULL;
            char *res = js_malloc(ctx, val_len + 1);
            if (res) {
                memcpy(res, val, val_len);
                res[val_len] = '\0';
                if (key[0] == 'n' && key[1] == 'u' && !is_valid_numbering_system(res)) {
                    js_free(ctx, res);
                    return NULL;
                }
            }
            return res;
        }
        /* skip this subtag and continue */
        if (*next == '-')
            next++;
        u_ext = next;
    }
    return NULL;
}

char *js_intl_locale_get_numbering_system(JSContext *ctx, const char *locale)
{
    return js_intl_locale_get_unicode_key(ctx, locale, "nu");
}

char *js_intl_extract_locale(JSContext *ctx, int argc, JSValueConst *argv)
{
    char *canonical;
    char *res = NULL;

    if (argc == 0 || JS_IsUndefined(argv[0]))
        return js_strdup(ctx, "en-US");

    if (js_canonicalizeLocaleList_first(ctx, argv[0], &canonical) < 0)
        return NULL;
    if (!canonical)
        return js_strdup(ctx, "en-US");
    res = js_intl_strip_unicode_extensions(ctx, canonical);
    js_free(ctx, canonical);
    if (!res)
        res = js_strdup(ctx, "en-US");
    return res;
}

JSValue js_intl_supportedLocalesOf(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    JSValue arr;
    int i;

    (void)this_val;

    if (argc > 0 && JS_IsNull(argv[0])) {
        JS_ThrowTypeError(ctx, "null is not a valid locale list");
        return JS_EXCEPTION;
    }

    if (argc == 0 || JS_IsUndefined(argv[0])) {
        return JS_NewArray(ctx);
    }

    /* SupportedLocales: if options is not undefined, let options be ? ToObject(options) */
    if (argc > 1 && !JS_IsUndefined(argv[1])) {
        JSValue options = JS_ToObject(ctx, argv[1]);
        if (JS_IsException(options))
            return JS_EXCEPTION;
        {
            int lm = js_intl_get_string_option(ctx, options, "localeMatcher",
                                                intl_localeMatcher_names, 2,
                                                INTL_LOCALEMATCHER_BESTFIT);
            if (lm < 0) {
                JS_FreeValue(ctx, options);
                return JS_EXCEPTION;
            }
        }
        JS_FreeValue(ctx, options);
    }

    {
        char **clocales = NULL;
        int clen = 0;
        if (js_canonicalizeLocaleList_c(ctx, argv[0], &clocales, &clen) < 0)
            return JS_EXCEPTION;

        arr = JS_NewArray(ctx);
        if (JS_IsException(arr)) {
            js_canonicalizeLocaleList_free_c(ctx, clocales, clen);
            return JS_EXCEPTION;
        }

        {
            uint32_t out_idx = 0;
            for (i = 0; i < clen; i++) {
                const char *str = clocales[i];
                if (str[0] != '\0' && strcmp(str, "zxx") != 0 && strncmp(str, "zxx-", 4) != 0) {
                    uint32_t j;
                    int dup = 0;
                    for (j = 0; j < out_idx; j++) {
                        JSValue existing = JS_GetPropertyUint32(ctx, arr, j);
                        if (!JS_IsUndefined(existing)) {
                            const char *estr = JS_ToCString(ctx, existing);
                            if (estr) {
                                if (!strcmp(estr, str))
                                    dup = 1;
                                JS_FreeCString(ctx, estr);
                            }
                        }
                        JS_FreeValue(ctx, existing);
                    }
                    if (!dup) {
                        JSValue str_val = JS_NewString(ctx, str);
                        if (JS_IsException(str_val)) {
                            js_canonicalizeLocaleList_free_c(ctx, clocales, clen);
                            JS_FreeValue(ctx, arr);
                            return JS_EXCEPTION;
                        }
                        if (JS_DefinePropertyValueUint32(ctx, arr, (uint32_t)out_idx++,
                                                          str_val, JS_PROP_C_W_E) < 0) {
                            js_canonicalizeLocaleList_free_c(ctx, clocales, clen);
                            JS_FreeValue(ctx, arr);
                            return JS_EXCEPTION;
                        }
                    }
                }
            }
        }

        js_canonicalizeLocaleList_free_c(ctx, clocales, clen);
        return arr;
    }
}

/* --------------------------------------------------------------------- */
/* Locale helpers                                                        */
/* --------------------------------------------------------------------- */

static int is_alpha(const char *s, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
            return 0;
    }
    return 1;
}

static int is_digit(const char *s, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        char c = s[i];
        if (!(c >= '0' && c <= '9'))
            return 0;
    }
    return 1;
}

static int is_alphanum(const char *s, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
            return 0;
    }
    return 1;
}

static void strtolower(char *s)
{
    while (*s) {
        if (*s >= 'A' && *s <= 'Z')
            *s += 32;
        s++;
    }
}

static void strntolower(char *s, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        if (s[i] >= 'A' && s[i] <= 'Z')
            s[i] += 32;
    }
}

static void strntoupper(char *s, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        if (s[i] >= 'a' && s[i] <= 'z')
            s[i] -= 32;
    }
}

/* Check if a language tag is well-formed per UTS35.
   This operates on the ASCII-lowercase form.  */
static int is_grandfathered_irregular(const char *tag)
{
    static const char *irregular[] = {
        "en-gb-oed", "i-ami", "i-bnn", "i-default", "i-enochian",
        "i-hak", "i-klingon", "i-lux", "i-mingo", "i-navajo",
        "i-pwn", "i-tao", "i-tay", "i-tsu", "sgn-be-fr",
        "sgn-be-nl", "sgn-ch-de"
    };
    static const char *regular_non_uts35[] = {
        "no-bok", "no-nyn", "zh-min", "zh-min-nan"
    };
    int i;
    for (i = 0; i < (int)countof(irregular); i++)
        if (!strcasecmp(tag, irregular[i]))
            return 1;
    for (i = 0; i < (int)countof(regular_non_uts35); i++)
        if (!strcasecmp(tag, regular_non_uts35[i]))
            return 1;
    return 0;
}

int is_wellformed_language_tag(const char *tag)
{
    char buf[512];
    char *subtags[64];
    int n = 0, i, j;
    int state; /* 0=base, 1=extension, 2=privateuse */
    int seen_singletons[256] = {0};
    int has_script = 0, has_region = 0;
    char ext_singleton = 0;
    int ext_has_subtag = 0;
    int tlang_state = 0; /* 0=not in tlang, 1=in tlang language, 2=in tlang script, 3=in tlang region, 4=in tlang variant */
    int u_attr_done = 0; /* 0=may see u attributes, 1=seen u key */

    if (!tag || !*tag)
        return 0;
    if (strlen(tag) >= sizeof(buf))
        return 0;
    if (tag[0] == '-')
        return 0;
    if (is_grandfathered_irregular(tag))
        return 0;

    pstrcpy(buf, sizeof(buf), tag);
    strtolower(buf);

    /* split on '-' */
    {
        char *p = buf;
        subtags[n++] = p;
        for (i = 0; buf[i]; i++) {
            if (buf[i] == '-') {
                buf[i] = '\0';
                subtags[n++] = &buf[i + 1];
                if (n >= 64) return 0;
            }
        }
    }

    if (n == 0) return 0;
    if (subtags[0][0] == '\0') return 0;

    /* first subtag: language */
    {
        int llen = (int)strlen(subtags[0]);
        if (!((llen >= 2 && llen <= 3) || (llen >= 5 && llen <= 8)))
            return 0;
        if (!is_alpha(subtags[0], llen))
            return 0;
    }

    state = 0;
    for (i = 1; i < n; i++) {
        int len = (int)strlen(subtags[i]);
        if (len == 0) return 0;

        if (state == 2) {
            /* private use: 1-8 alphanum */
            if (len < 1 || len > 8 || !is_alphanum(subtags[i], len))
                return 0;
            continue;
        }

        if (state == 1) {
            if (len == 1) {
                /* new extension or privateuse */
                /* Empty extension (singleton immediately followed by another singleton) is invalid */
                if (!ext_has_subtag)
                    return 0;
                /* If t extension ended with a key expecting a value, it's invalid */
                if (ext_singleton == 't' && tlang_state == 5)
                    return 0;
                state = 0;
                ext_singleton = 0;
                tlang_state = 0;
                u_attr_done = 0;
                i--; /* reprocess this singleton */
                continue;
            }
            ext_has_subtag = 1;
            if (ext_singleton == 't') {
                /* transformed extension: tlang or tfield */
                /* tfield key is alpha+digit, must be followed by at least one value (3-8 alphanum) */
                if (tlang_state == 0) {
                    /* could be tlang language or tfield key */
                    if (((len >= 2 && len <= 3) || (len >= 5 && len <= 8)) && is_alpha(subtags[i], len)) {
                        /* tlang language */
                        tlang_state = 1;
                    } else if (len == 2 && is_alpha(subtags[i], 1) && is_digit(subtags[i] + 1, 1)) {
                        /* tfield key */
                        tlang_state = 5;
                    } else {
                        return 0;
                    }
                } else if (tlang_state == 1) {
                    /* after tlang language: script, region, variant, or tfield key */
                    if (len == 4 && is_alpha(subtags[i], 4)) {
                        tlang_state = 2;
                    } else if ((len == 2 && is_alpha(subtags[i], 2)) ||
                               (len == 3 && is_digit(subtags[i], 3))) {
                        tlang_state = 3;
                    } else if ((len >= 5 && len <= 8 && is_alphanum(subtags[i], len)) ||
                               (len == 4 && isdigit((unsigned char)subtags[i][0]) && is_alphanum(subtags[i] + 1, 3))) {
                        for (j = 2; j < i; j++) {
                            int jlen = (int)strlen(subtags[j]);
                            if (jlen == len && !memcmp(subtags[j], subtags[i], (size_t)len))
                                return 0;
                        }
                        tlang_state = 4;
                    } else if (len == 2 && is_alpha(subtags[i], 1) && is_digit(subtags[i] + 1, 1)) {
                        tlang_state = 5;
                    } else {
                        return 0;
                    }
                } else if (tlang_state == 2) {
                    /* after tlang script: region, variant, or tfield key */
                    if ((len == 2 && is_alpha(subtags[i], 2)) ||
                        (len == 3 && is_digit(subtags[i], 3))) {
                        tlang_state = 3;
                    } else if ((len >= 5 && len <= 8 && is_alphanum(subtags[i], len)) ||
                               (len == 4 && isdigit((unsigned char)subtags[i][0]) && is_alphanum(subtags[i] + 1, 3))) {
                        for (j = 2; j < i; j++) {
                            int jlen = (int)strlen(subtags[j]);
                            if (jlen == len && !memcmp(subtags[j], subtags[i], (size_t)len))
                                return 0;
                        }
                        tlang_state = 4;
                    } else if (len == 2 && is_alpha(subtags[i], 1) && is_digit(subtags[i] + 1, 1)) {
                        tlang_state = 5;
                    } else {
                        return 0;
                    }
                } else if (tlang_state == 3) {
                    /* after tlang region: variant or tfield key */
                    if ((len >= 5 && len <= 8 && is_alphanum(subtags[i], len)) ||
                        (len == 4 && isdigit((unsigned char)subtags[i][0]) && is_alphanum(subtags[i] + 1, 3))) {
                        for (j = 2; j < i; j++) {
                            int jlen = (int)strlen(subtags[j]);
                            if (jlen == len && !memcmp(subtags[j], subtags[i], (size_t)len))
                                return 0;
                        }
                        tlang_state = 4;
                    } else if (len == 2 && is_alpha(subtags[i], 1) && is_digit(subtags[i] + 1, 1)) {
                        tlang_state = 5;
                    } else {
                        return 0;
                    }
                } else if (tlang_state == 4) {
                    /* after tlang variant: more variant or tfield key */
                    if ((len >= 5 && len <= 8 && is_alphanum(subtags[i], len)) ||
                        (len == 4 && isdigit((unsigned char)subtags[i][0]) && is_alphanum(subtags[i] + 1, 3))) {
                        for (j = 2; j < i; j++) {
                            int jlen = (int)strlen(subtags[j]);
                            if (jlen == len && !memcmp(subtags[j], subtags[i], (size_t)len))
                                return 0;
                        }
                    } else if (len == 2 && is_alpha(subtags[i], 1) && is_digit(subtags[i] + 1, 1)) {
                        tlang_state = 5;
                    } else {
                        return 0;
                    }
                } else if (tlang_state == 5) {
                    /* in tfield, expecting value (3-8 alphanum) */
                    if (len >= 3 && len <= 8 && is_alphanum(subtags[i], len)) {
                        tlang_state = 6;
                    } else {
                        return 0;
                    }
                } else if (tlang_state == 6) {
                    /* after tfield value: more values or new key */
                    if (len >= 3 && len <= 8 && is_alphanum(subtags[i], len)) {
                        /* another value */
                    } else if (len == 2 && is_alpha(subtags[i], 1) && is_digit(subtags[i] + 1, 1)) {
                        /* new key */
                        tlang_state = 5;
                    } else {
                        return 0;
                    }
                }
            } else if (ext_singleton == 'u') {
                /* unicode extension: attributes or key-value pairs */
                if (!u_attr_done) {
                    if (len == 2 && is_alphanum(subtags[i], 2) && is_alpha(subtags[i] + 1, 1)) {
                        u_attr_done = 1; /* first key */
                    } else if (len >= 3 && len <= 8 && is_alphanum(subtags[i], len)) {
                        /* attribute */
                    } else {
                        return 0;
                    }
                } else {
                    /* after first key: values (3-8) or next key (2 with alpha second) */
                    if (len == 2 && is_alphanum(subtags[i], 2) && is_alpha(subtags[i] + 1, 1)) {
                        /* next key */
                    } else if (len >= 3 && len <= 8 && is_alphanum(subtags[i], len)) {
                        /* value */
                    } else {
                        return 0;
                    }
                }
            } else {
                /* other extensions: subtags 2-8 alphanum */
                if (len < 2 || len > 8 || !is_alphanum(subtags[i], len))
                    return 0;
            }
            continue;
        }

        /* state == 0: base or singleton */
        if (len == 1) {
            char c = subtags[i][0];
            if (!isalnum((unsigned char)c))
                return 0;
            if (c == 'x') {
                /* privateuse must have at least one following subtag */
                if (i + 1 >= n)
                    return 0;
                state = 2;
                continue;
            }
            /* extension singleton */
            if (seen_singletons[(unsigned char)c])
                return 0;
            seen_singletons[(unsigned char)c] = 1;
            /* extension must have at least one following subtag */
            if (i + 1 >= n)
                return 0;
            ext_singleton = c;
            tlang_state = 0;
            u_attr_done = 0;
            ext_has_subtag = 0;
            state = 1;
            continue;
        }

        if (!has_script && !has_region && len == 4 && is_alpha(subtags[i], 4)) {
            has_script = 1;
            continue;
        }

        if (!has_region && ((len == 2 && is_alpha(subtags[i], 2)) ||
                            (len == 3 && is_digit(subtags[i], 3)))) {
            has_region = 1;
            continue;
        }

        /* variant: 5-8 alphanum, or digit + 3 alphanum */
        if ((len >= 5 && len <= 8 && is_alphanum(subtags[i], len)) ||
            (len == 4 && isdigit((unsigned char)subtags[i][0]) && is_alphanum(subtags[i] + 1, 3))) {
            /* check duplicate variants */
            for (j = 1; j < i; j++) {
                int jlen = (int)strlen(subtags[j]);
                if (jlen == len && !memcmp(subtags[j], subtags[i], (size_t)len))
                    return 0;
            }
            continue;
        }

        return 0;
    }

    /* If we ended inside a t extension expecting a tfield value, it's invalid */
    if (state == 1 && ext_singleton == 't' && tlang_state == 5)
        return 0;

    return 1;
}

/* Canonicalize a well-formed language tag.
   Returns a newly allocated string in ctx.  */
typedef struct {
    int start;
    int len;
    char singleton;
} ExtBlock;

static int ext_block_cmp(const void *a, const void *b)
{
    const ExtBlock *ea = a;
    const ExtBlock *eb = b;
    return (int)ea->singleton - (int)eb->singleton;
}

char *canonicalize_locale(JSContext *ctx, const char *locale)
{
    char buf[512];
    char *subtags[64];
    int n = 0, i;
    char *result;
    int in_extension = 0;
    int has_script = 0, has_region = 0;
    DynBuf dbuf;
    ExtBlock ext_blocks[16];
    int ext_count = 0;
    int base_end = 0;

    if (strlen(locale) >= sizeof(buf))
        return NULL;
    pstrcpy(buf, sizeof(buf), locale);

    /* split */
    {
        char *p = buf;
        subtags[n++] = p;
        for (i = 0; buf[i]; i++) {
            if (buf[i] == '-') {
                buf[i] = '\0';
                subtags[n++] = &buf[i + 1];
                if (n >= 64) return NULL;
            }
        }
    }

    /* apply alias mappings for grandfathered/deprecated tags */
    if (!strcasecmp(subtags[0], "cmn")) {
        pstrcpy(subtags[0], sizeof(buf), "zh");
    } else if (n == 1 && !strcasecmp(subtags[0], "ji")) {
        pstrcpy(subtags[0], sizeof(buf), "yi");
    } else if (n == 1 && !strcasecmp(subtags[0], "in")) {
        pstrcpy(subtags[0], sizeof(buf), "id");
    } else if (n == 2 && !strcasecmp(subtags[0], "sgn") && !strcasecmp(subtags[1], "GR")) {
        pstrcpy(subtags[0], sizeof(buf), "gss");
        subtags[1][0] = '\0';
        n = 1;
    } else if (n == 2 && !strcasecmp(subtags[0], "de") && !strcasecmp(subtags[1], "DD")) {
        pstrcpy(subtags[1], sizeof(buf), "DE");
    } else if (!strcasecmp(subtags[0], "mo")) {
        pstrcpy(subtags[0], sizeof(buf), "ro");
    } else if (!strcasecmp(subtags[0], "aar")) {
        pstrcpy(subtags[0], sizeof(buf), "aa");
    } else if (!strcasecmp(subtags[0], "heb")) {
        pstrcpy(subtags[0], sizeof(buf), "he");
    } else if (!strcasecmp(subtags[0], "ces")) {
        pstrcpy(subtags[0], sizeof(buf), "cs");
    } else if (!strcasecmp(subtags[0], "sh")) {
        int found_script = 0, si;
        for (si = 1; si < n; si++) {
            if (strlen(subtags[si]) == 4 && is_alpha(subtags[si], 4)) {
                found_script = 1;
                break;
            }
        }
        subtags[0] = (char *)"sr";
        if (!found_script) {
            if (n > 1)
                memmove(&subtags[2], &subtags[1], (size_t)(n - 1) * sizeof(char *));
            subtags[1] = (char *)"Latn";
            n++;
        }
    } else if (!strcasecmp(subtags[0], "cnr")) {
        int found_region = 0, insert_pos = 1, ri;
        for (ri = 1; ri < n; ri++) {
            int rlen = (int)strlen(subtags[ri]);
            if (rlen == 4 && is_alpha(subtags[ri], 4)) {
                insert_pos = ri + 1;
            } else if ((rlen == 2 && is_alpha(subtags[ri], 2)) ||
                       (rlen == 3 && is_digit(subtags[ri], 3))) {
                found_region = 1;
                break;
            }
        }
        subtags[0] = (char *)"sr";
        if (!found_region) {
            if (insert_pos < n)
                memmove(&subtags[insert_pos + 1], &subtags[insert_pos], (size_t)(n - insert_pos) * sizeof(char *));
            subtags[insert_pos] = (char *)"ME";
            n++;
        }
    } else if (n == 2 && !strcasecmp(subtags[0], "hy") && !strcasecmp(subtags[1], "arevela")) {
        pstrcpy(buf, sizeof(buf), "hy");
        subtags[0] = buf;
        n = 1;
    } else if (n == 2 && !strcasecmp(subtags[0], "hy") && !strcasecmp(subtags[1], "arevmda")) {
        pstrcpy(buf, sizeof(buf), "hyw");
        subtags[0] = buf;
        n = 1;
    } else if (n == 2 && !strcasecmp(subtags[0], "art") && !strcasecmp(subtags[1], "lojban")) {
        pstrcpy(buf, sizeof(buf), "jbo");
        subtags[0] = buf;
        n = 1;
    } else if (n == 2 && !strcasecmp(subtags[0], "cel") && !strcasecmp(subtags[1], "gaulish")) {
        pstrcpy(buf, sizeof(buf), "xtg");
        subtags[0] = buf;
        n = 1;
    } else if (n == 2 && !strcasecmp(subtags[0], "zh") && !strcasecmp(subtags[1], "guoyu")) {
        pstrcpy(buf, sizeof(buf), "zh");
        subtags[0] = buf;
        n = 1;
    } else if (n == 2 && !strcasecmp(subtags[0], "zh") && !strcasecmp(subtags[1], "hakka")) {
        pstrcpy(buf, sizeof(buf), "hak");
        subtags[0] = buf;
        n = 1;
    } else if (n == 2 && !strcasecmp(subtags[0], "zh") && !strcasecmp(subtags[1], "xiang")) {
        pstrcpy(buf, sizeof(buf), "hsn");
        subtags[0] = buf;
        n = 1;
    }

    /* identify extension blocks for reordering */
    base_end = n;
    for (i = 1; i < n; i++) {
        if (strlen(subtags[i]) == 1) {
            char c = (char)tolower((unsigned char)subtags[i][0]);
            /* found a singleton, this starts an extension block */
            if (ext_count < 16) {
                ext_blocks[ext_count].start = i;
                ext_blocks[ext_count].singleton = c;
                /* find the end of this extension block */
                int j;
                for (j = i + 1; j < n; j++) {
                    if (strlen(subtags[j]) == 1)
                        break;
                }
                ext_blocks[ext_count].len = j - i;
                ext_count++;
                i = j - 1; /* skip to end of this block */
            }
            base_end = i; /* base ends before first extension */
            if (c == 'x') {
                /* private use: everything after is part of this block */
                if (ext_count > 0) {
                    ext_blocks[ext_count - 1].len = n - ext_blocks[ext_count - 1].start;
                }
                break;
            }
        }
    }
    /* base_end should be the index of the first singleton */
    if (ext_count > 0)
        base_end = ext_blocks[0].start;

    /* sort extension blocks by singleton */
    if (ext_count > 1)
        qsort(ext_blocks, (size_t)ext_count, sizeof(ExtBlock), ext_block_cmp);

    /* apply case rules to all subtags */
    for (i = 0; i < n; i++) {
        int len = (int)strlen(subtags[i]);

        if (in_extension) {
            if (len == 1) {
                in_extension = 0;
                /* reprocess below */
            } else {
                strntolower(subtags[i], len);
                continue;
            }
        }

        if (len == 1) {
            /* singleton */
            if (subtags[i][0] == 'x' || subtags[i][0] == 'X') {
                /* private use, lowercase it */
                strntolower(subtags[i], len);
                /* everything after is lowercase too */
                for (i = i + 1; i < n; i++)
                    strntolower(subtags[i], (int)strlen(subtags[i]));
                break;
            }
            strntolower(subtags[i], len);
            in_extension = 1;
            continue;
        }

        if (i == 0) {
            /* language: lowercase */
            strntolower(subtags[i], len);
            continue;
        }

        if (!has_script && !has_region && len == 4 && is_alpha(subtags[i], 4)) {
            /* script: Titlecase */
            if (subtags[i][0] >= 'a' && subtags[i][0] <= 'z')
                subtags[i][0] -= 32;
            {
                int j;
                for (j = 1; j < 4; j++) {
                    if (subtags[i][j] >= 'A' && subtags[i][j] <= 'Z')
                        subtags[i][j] += 32;
                }
            }
            has_script = 1;
            continue;
        }

        if (!has_region && ((len == 2 && is_alpha(subtags[i], 2)) ||
                            (len == 3 && is_digit(subtags[i], 3)))) {
            /* region: UPPERCASE */
            strntoupper(subtags[i], len);
            /* Map numeric UN M.49 region codes to ISO 3166-1 alpha-2 */
            if (len == 3) {
                if (!strcmp(subtags[i], "554"))
                    pstrcpy(subtags[i], sizeof(buf), "NZ");
            }
            /* Region alias replacements */
            if (!strcmp(subtags[i], "SU") || !strcmp(subtags[i], "810")) {
                int has_armn = 0;
                if (!strcmp(subtags[0], "hy"))
                    has_armn = 1;
                else if (has_script) {
                    int j;
                    for (j = i - 1; j > 0; j--) {
                        if (strlen(subtags[j]) == 4 && is_alpha(subtags[j], 4)) {
                            if (!strcmp(subtags[j], "Armn"))
                                has_armn = 1;
                            break;
                        }
                    }
                }
                pstrcpy(subtags[i], sizeof(buf), has_armn ? "AM" : "RU");
            } else if (!strcmp(subtags[i], "CS")) {
                pstrcpy(subtags[i], sizeof(buf), "RS");
            } else if (!strcmp(subtags[i], "NT")) {
                pstrcpy(subtags[i], sizeof(buf), "SA");
            }
            has_region = 1;
            continue;
        }

        /* variant or other: lowercase */
        strntolower(subtags[i], len);
        /* variant alias mappings */
        if (!strcmp(subtags[i], "heploc"))
            pstrcpy(subtags[i], sizeof(buf), "alalc97");
    }
    {
        /* Special case: hepburn + heploc -> alalc97 (remove hepburn) */
        int has_alalc97 = 0, has_hepburn = 0;
        for (i = 0; i < base_end; i++) {
            if (!strcmp(subtags[i], "alalc97")) has_alalc97 = 1;
            if (!strcmp(subtags[i], "hepburn")) has_hepburn = 1;
        }
        if (has_alalc97 && has_hepburn) {
            for (i = 0; i < base_end; i++) {
                if (!strcmp(subtags[i], "hepburn")) {
                    subtags[i] = (char *)"";
                }
            }
        }
    }

    /* Sort base tag variants alphabetically */
    {
        int var_start = 0;
        if (base_end > 1) {
            if (base_end > 2 && strlen(subtags[1]) == 4 && is_alpha(subtags[1], 4))
                var_start = 2;
            else
                var_start = 1;
            if (var_start < base_end && ((strlen(subtags[var_start]) == 2 && is_alpha(subtags[var_start], 2)) ||
                                         (strlen(subtags[var_start]) == 3 && is_digit(subtags[var_start], 3))))
                var_start++;
        }
        if (var_start < base_end && base_end - var_start > 1) {
            qsort(&subtags[var_start], (size_t)(base_end - var_start), sizeof(char *), cmp_str);
        }
    }

    /* After sorting, apply grandfathered alias replacements for regular grandfathered tags
       when combined with extra subtags (e.g., art-lojban-fonipa -> jbo-fonipa) */
    {
        int gf_variant = -1;
        if (!strcasecmp(subtags[0], "art")) {
            for (i = 1; i < base_end; i++) {
                if (!strcasecmp(subtags[i], "lojban")) { gf_variant = i; break; }
            }
        } else if (!strcasecmp(subtags[0], "cel")) {
            for (i = 1; i < base_end; i++) {
                if (!strcasecmp(subtags[i], "gaulish")) { gf_variant = i; break; }
            }
        } else if (!strcasecmp(subtags[0], "zh")) {
            for (i = 1; i < base_end; i++) {
                if (!strcasecmp(subtags[i], "guoyu")) { gf_variant = i; break; }
                if (!strcasecmp(subtags[i], "hakka")) { gf_variant = i; break; }
                if (!strcasecmp(subtags[i], "xiang")) { gf_variant = i; break; }
            }
        }
        if (gf_variant >= 0) {
            if (!strcasecmp(subtags[0], "art")) {
                subtags[0] = (char *)"jbo";
            } else if (!strcasecmp(subtags[0], "cel")) {
                subtags[0] = (char *)"xtg";
            } else if (!strcasecmp(subtags[0], "zh")) {
                if (!strcasecmp(subtags[gf_variant], "guoyu"))
                    subtags[0] = (char *)"zh";
                else if (!strcasecmp(subtags[gf_variant], "hakka"))
                    subtags[0] = (char *)"hak";
                else if (!strcasecmp(subtags[gf_variant], "xiang"))
                    subtags[0] = (char *)"hsn";
            }
            subtags[gf_variant] = (char *)"";
            /* compact empty slots */
            {
                int j, k = 0;
                for (j = 0; j < base_end; j++) {
                    if (strlen(subtags[j]) > 0) {
                        if (j != k)
                            subtags[k] = subtags[j];
                        k++;
                    }
                }
                base_end = k;
            }
        }
    }

    /* reconstruct using DynBuf: base subtags + sorted extensions */
    js_intl_dbuf_init(ctx, &dbuf);
    for (i = 0; i < base_end; i++) {
        if (strlen(subtags[i]) == 0)
            continue;
        if (dbuf.size > 0)
            dbuf_putc(&dbuf, '-');
        dbuf_putstr(&dbuf, subtags[i]);
    }
    {
        int e;
        for (e = 0; e < ext_count; e++) {
            int j;
            char singleton = subtags[ext_blocks[e].start][0];
            /* t-extension canonicalization */
            if (singleton == 't') {
                int t_start = ext_blocks[e].start + 1;
                int t_len = ext_blocks[e].len - 1;
                int has_tlang = 0;
                int tlang_end = 0;
                if (t_len > 0) {
                    const char *first = subtags[t_start];
                    int flen = (int)strlen(first);
                    if ((flen == 2 || flen == 3 || (flen >= 5 && flen <= 8)) && is_alpha(first, flen)) {
                        has_tlang = 1;
                        tlang_end = 1;
                        if (tlang_end < t_len && strlen(subtags[t_start + tlang_end]) == 4 &&
                            is_alpha(subtags[t_start + tlang_end], 4))
                            tlang_end++;
                        if (tlang_end < t_len &&
                            ((strlen(subtags[t_start + tlang_end]) == 2 && is_alpha(subtags[t_start + tlang_end], 2)) ||
                             (strlen(subtags[t_start + tlang_end]) == 3 && is_digit(subtags[t_start + tlang_end], 3))))
                            tlang_end++;
                        while (tlang_end < t_len) {
                            const char *sub = subtags[t_start + tlang_end];
                            int sublen = (int)strlen(sub);
                            if (sublen == 2 &&
                                ((is_alpha(sub, 1) && is_digit(sub + 1, 1)) ||
                                 (is_digit(sub, 1) && is_alpha(sub + 1, 1))))
                                break;
                            tlang_end++;
                        }
                    }
                }
                if (has_tlang) {
                    int var_start = 1;
                    if (var_start < tlang_end && strlen(subtags[t_start + var_start]) == 4 &&
                        is_alpha(subtags[t_start + var_start], 4))
                        var_start++;
                    if (var_start < tlang_end &&
                        ((strlen(subtags[t_start + var_start]) == 2 && is_alpha(subtags[t_start + var_start], 2)) ||
                         (strlen(subtags[t_start + var_start]) == 3 && is_digit(subtags[t_start + var_start], 3))))
                        var_start++;
                    if (var_start < tlang_end && tlang_end - var_start > 1)
                        qsort(&subtags[t_start + var_start], (size_t)(tlang_end - var_start), sizeof(char *), cmp_str);
                    if (!strcmp(subtags[t_start], "iw"))
                        subtags[t_start] = (char *)"he";
                }
                {
                    typedef struct { const char *key; int nvalues; const char *values[4]; } TFieldEntry;
                    TFieldEntry tfields[16];
                    int ntfields = 0;
                    int pos = has_tlang ? tlang_end : 0;
                    while (pos < t_len) {
                        const char *key = subtags[t_start + pos];
                        pos++;
                        int nvalues = 0;
                        const char *values[4];
                        while (pos < t_len) {
                            const char *sub = subtags[t_start + pos];
                            int sublen = (int)strlen(sub);
                            if (sublen == 2 &&
                                ((is_alpha(sub, 1) && is_digit(sub + 1, 1)) ||
                                 (is_digit(sub, 1) && is_alpha(sub + 1, 1))))
                                break;
                            if (nvalues < 4) {
                                if (!strcmp(sub, "names"))
                                    values[nvalues] = "prprname";
                                else
                                    values[nvalues] = sub;
                                nvalues++;
                            }
                            pos++;
                        }
                        if (ntfields < 16) {
                            int v;
                            tfields[ntfields].key = key;
                            tfields[ntfields].nvalues = nvalues;
                            for (v = 0; v < nvalues; v++)
                                tfields[ntfields].values[v] = values[v];
                            ntfields++;
                        }
                    }
                    if (ntfields > 1) {
                        int a, b;
                        for (a = 0; a < ntfields - 1; a++) {
                            for (b = a + 1; b < ntfields; b++) {
                                if (strcmp(tfields[a].key, tfields[b].key) > 0) {
                                    TFieldEntry tmp = tfields[a];
                                    tfields[a] = tfields[b];
                                    tfields[b] = tmp;
                                }
                            }
                        }
                    }
                    dbuf_putc(&dbuf, '-');
                    dbuf_putstr(&dbuf, "t");
                    if (has_tlang) {
                        for (j = 0; j < tlang_end; j++) {
                            dbuf_putc(&dbuf, '-');
                            dbuf_putstr(&dbuf, subtags[t_start + j]);
                        }
                    }
                    for (j = 0; j < ntfields; j++) {
                        int v;
                        dbuf_putc(&dbuf, '-');
                        dbuf_putstr(&dbuf, tfields[j].key);
                        for (v = 0; v < tfields[j].nvalues; v++) {
                            dbuf_putc(&dbuf, '-');
                            dbuf_putstr(&dbuf, tfields[j].values[v]);
                        }
                    }
                }
                continue;
            }
            for (j = 0; j < ext_blocks[e].len; j++) {
                const char *sub = subtags[ext_blocks[e].start + j];
                int slen = (int)strlen(sub);
                /* u-extension: canonicalize yes -> true for specific keys, then strip true */
                if (singleton == 'u' && slen == 2 && is_alpha(sub, 1) &&
                    (is_alpha(sub + 1, 1) || is_digit(sub + 1, 1))) {
                    int skip_value = 0;
                    const char *value = NULL;
                    if (j + 1 < ext_blocks[e].len) {
                        const char *next = subtags[ext_blocks[e].start + j + 1];
                        int nlen = (int)strlen(next);
                        if (nlen != 1 || (is_alpha(next, 1) && is_digit(next, 1))) {
                            /* next is a value segment, not a key or singleton */
                            value = next;
                            if (!strcmp(next, "yes") &&
                                (!strcmp(sub, "kb") || !strcmp(sub, "kc") ||
                                 !strcmp(sub, "kh") || !strcmp(sub, "kk") ||
                                 !strcmp(sub, "kn"))) {
                                skip_value = 1;
                            } else if (!strcmp(next, "true")) {
                                skip_value = 1;
                            } else if (!strcmp(sub, "ca")) {
                                if (!strcmp(next, "ethiopic-amete-alem"))
                                    value = "ethioaa";
                                else if (!strcmp(next, "islamicc"))
                                    value = "islamic-civil";
                                else if (!strcmp(next, "ethiopic") && j + 3 < ext_blocks[e].len &&
                                         !strcmp(subtags[ext_blocks[e].start + j + 2], "amete") &&
                                         !strcmp(subtags[ext_blocks[e].start + j + 3], "alem")) {
                                    value = "ethioaa";
                                    j += 2; /* skip the extra segments */
                                }
                            } else if (!strcmp(sub, "ms")) {
                                if (!strcmp(next, "imperial"))
                                    value = "uksystem";
                            } else if (!strcmp(sub, "ks")) {
                                if (!strcmp(next, "primary"))
                                    value = "level1";
                                else if (!strcmp(next, "tertiary"))
                                    value = "level3";
                            } else if (!strcmp(sub, "tz")) {
                                if (!strcmp(next, "cnckg"))
                                    value = "cnsha";
                                else if (!strcmp(next, "eire"))
                                    value = "iedub";
                                else if (!strcmp(next, "est"))
                                    value = "papty";
                                else if (!strcmp(next, "gmt0"))
                                    value = "gmt";
                                else if (!strcmp(next, "uct"))
                                    value = "utc";
                                else if (!strcmp(next, "zulu"))
                                    value = "utc";
                            } else if (!strcmp(sub, "rg") || !strcmp(sub, "sd")) {
                                if (!strcmp(next, "no23"))
                                    value = "no50";
                                else if (!strcmp(next, "cn11"))
                                    value = "cnbj";
                                else if (!strcmp(next, "cz10a"))
                                    value = "cz110";
                                else if (!strcmp(next, "fra"))
                                    value = "frges";
                                else if (!strcmp(next, "frg"))
                                    value = "frges";
                                else if (!strcmp(next, "lud"))
                                    value = "lucl";
                            }
                        }
                    }
                    dbuf_putc(&dbuf, '-');
                    dbuf_putstr(&dbuf, sub);
                    if (value && !skip_value) {
                        dbuf_putc(&dbuf, '-');
                        dbuf_putstr(&dbuf, value);
                        j++;
                    } else if (skip_value) {
                        j++;
                    }
                    continue;
                }
                dbuf_putc(&dbuf, '-');
                dbuf_putstr(&dbuf, sub);
            }
        }
    }
    dbuf_putc(&dbuf, '\0');
    if (dbuf_error(&dbuf)) {
        dbuf_free(&dbuf);
        return NULL;
    }
    result = (char *)dbuf.buf;
    return result;
}

void js_intl_dbuf_init(JSContext *ctx, DynBuf *s)
{
    dbuf_init2(s, ctx, (DynBufReallocFunc *)js_realloc);
}

/* Helper to create an Intl constructor and register it on the Intl object */
JSValue js_intl_new_constructor(JSContext *ctx, JSValue intl_obj,
                                 int intl_idx,
                                 JSClassID class_id,
                                 const char *name,
                                 JSCFunction *func, int length,
                                 JSCFunctionEnum cproto, int magic,
                                 const JSCFunctionListEntry *static_funcs, int n_static,
                                 const JSCFunctionListEntry *proto_funcs, int n_proto)
{
    JSValue ctor = JS_NewCConstructor(ctx, (int)class_id, name, func, length, cproto, magic,
                                       JS_UNDEFINED,
                                       static_funcs, n_static,
                                       proto_funcs, n_proto,
                                       JS_NEW_CTOR_NO_GLOBAL);
    if (JS_IsException(ctor))
        return JS_EXCEPTION;
    if (intl_idx >= 0)
        JS_SetContextValue(ctx, intl_idx, JS_DupValue(ctx, ctor));
    if (JS_DefinePropertyValueStr(ctx, intl_obj, name, ctor,
                                   JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE) < 0)
        return JS_EXCEPTION;
    return JS_UNDEFINED;
}

/* --------------------------------------------------------------------- */
/* CanonicalizeLocaleList (locales)                                      */
/* --------------------------------------------------------------------- */

/* Internal core: canonicalize locales into a C array of strings.
   Returns 0 on success, -1 on exception.
   On success, *plocales is an array of canonical strings (allocated with
   js_strdup / js_realloc) and *pcount is the number of strings.
   Caller must free with js_canonicalizeLocaleList_free_c(). */
static int js_canonicalizeLocaleList_core(JSContext *ctx, JSValueConst locales,
                                           char ***plocales, int *pcount)
{
    JSValue O = JS_UNDEFINED;
    int64_t len, k;
    int ret;
    char **arr = NULL;
    int arr_count = 0;
    int arr_size = 0;

    *plocales = NULL;
    *pcount = 0;

    if (JS_IsUndefined(locales))
        return 0;

    {
        JSValue locale_str = JS_UNDEFINED;
        int is_locale = 0;
        if (JS_IsObject(locales)) {
            locale_str = js_locale_get_locale_string(ctx, locales);
            if (JS_IsException(locale_str))
                goto fail;
            is_locale = !JS_IsUndefined(locale_str);
            if (!is_locale)
                JS_FreeValue(ctx, locale_str);
        }
        if (JS_IsString(locales) || is_locale) {
            O = JS_NewArray(ctx);
            if (JS_IsException(O))
                goto fail;
            ret = JS_DefinePropertyValueUint32(ctx, O, 0, JS_DupValue(ctx, locales),
                                               JS_PROP_C_W_E);
            if (ret < 0)
                goto fail;
            ret = JS_DefinePropertyValueStr(ctx, O, "length", JS_NewInt32(ctx, 1),
                                            JS_PROP_C_W_E);
            if (ret < 0)
                goto fail;
            if (is_locale)
                JS_FreeValue(ctx, locale_str);
        } else {
            O = JS_ToObject(ctx, locales);
            if (JS_IsException(O))
                goto fail;
        }
    }

    {
        JSValue len_val = JS_GetPropertyStr(ctx, O, "length");
        if (JS_IsException(len_val))
            goto fail;
        ret = JS_ToInt64(ctx, &len, len_val);
        JS_FreeValue(ctx, len_val);
        if (ret < 0)
            goto fail;
    }

    for (k = 0; k < len; k++) {
        JSValue val;
        int has_prop;
        const char *tag;
        char *canonical;
        int i;

        {
            JSAtom prop = JS_NewAtomUInt32(ctx, (uint32_t)k);
            has_prop = JS_HasProperty(ctx, O, prop);
            JS_FreeAtom(ctx, prop);
        }
        if (has_prop < 0)
            goto fail;
        if (!has_prop)
            continue;

        val = JS_GetPropertyUint32(ctx, O, (uint32_t)k);
        if (JS_IsException(val))
            goto fail;

        if (!JS_IsString(val) && !JS_IsObject(val)) {
            JS_FreeValue(ctx, val);
            JS_ThrowTypeError(ctx, "locale must be a string or object");
            goto fail;
        }

        if (JS_IsObject(val)) {
            JSValue loc_str = js_locale_get_locale_string(ctx, val);
            if (JS_IsException(loc_str)) {
                JS_FreeValue(ctx, val);
                goto fail;
            }
            if (!JS_IsUndefined(loc_str)) {
                tag = JS_ToCStringNZ(ctx, loc_str);
                JS_FreeValue(ctx, loc_str);
                if (!tag) {
                    JS_FreeValue(ctx, val);
                    goto fail;
                }
            } else {
                tag = JS_ToCStringNZ(ctx, val);
                if (!tag) {
                    JS_FreeValue(ctx, val);
                    goto fail;
                }
            }
            JS_FreeValue(ctx, val);
        } else {
            tag = JS_ToCStringNZ(ctx, val);
            JS_FreeValue(ctx, val);
            if (!tag)
                goto fail;
        }

        if (!is_wellformed_language_tag(tag)) {
            JS_FreeCString(ctx, tag);
            JS_ThrowRangeError(ctx, "invalid language tag");
            goto fail;
        }

        canonical = canonicalize_locale(ctx, tag);
        JS_FreeCString(ctx, tag);

        if (!canonical)
            goto fail;

        /* check for duplicates against C array */
        for (i = 0; i < arr_count; i++) {
            if (!strcmp(arr[i], canonical)) {
                js_free(ctx, canonical);
                canonical = NULL;
                break;
            }
        }
        if (!canonical)
            continue;

        if (arr_count >= arr_size) {
            int new_size = arr_size ? arr_size * 2 : 4;
            char **new_arr = js_realloc(ctx, arr, (size_t)new_size * sizeof(*arr));
            if (!new_arr) {
                js_free(ctx, canonical);
                goto fail;
            }
            arr = new_arr;
            arr_size = new_size;
        }
        arr[arr_count++] = canonical;
    }

    JS_FreeValue(ctx, O);
    *plocales = arr;
    *pcount = arr_count;
    return 0;

fail:
    JS_FreeValue(ctx, O);
    if (arr) {
        for (k = 0; k < arr_count; k++)
            js_free(ctx, arr[k]);
        js_free(ctx, arr);
    }
    return -1;
}

int js_canonicalizeLocaleList_c(JSContext *ctx, JSValueConst locales,
                                 char ***plocales, int *pcount)
{
    return js_canonicalizeLocaleList_core(ctx, locales, plocales, pcount);
}

void js_canonicalizeLocaleList_free_c(JSContext *ctx, char **locales, int count)
{
    int i;
    if (locales) {
        for (i = 0; i < count; i++)
            js_free(ctx, locales[i]);
        js_free(ctx, locales);
    }
}

/* Convenience: return the first canonical locale string via *plocale.
   Returns 0 on success (empty list yields *plocale = NULL), -1 on exception.
   Caller must free with js_free. */
int js_canonicalizeLocaleList_first(JSContext *ctx, JSValueConst locales, char **plocale)
{
    char **arr = NULL;
    int count = 0;

    *plocale = NULL;
    if (js_canonicalizeLocaleList_core(ctx, locales, &arr, &count) < 0)
        return -1;
    if (count > 0) {
        *plocale = arr[0];
        /* Shift remaining down and free the array container */
        if (count > 1) {
            int i;
            for (i = 1; i < count; i++)
                js_free(ctx, arr[i]);
        }
        js_free(ctx, arr);
    }
    return 0;
}

JSValue js_canonicalizeLocaleList(JSContext *ctx, JSValueConst locales)
{
    char **arr = NULL;
    int count = 0;
    int i;
    JSValue result;

    if (js_canonicalizeLocaleList_core(ctx, locales, &arr, &count) < 0)
        return JS_EXCEPTION;

    result = JS_NewArray(ctx);
    if (JS_IsException(result))
        goto fail;

    for (i = 0; i < count; i++) {
        JSValue str = JS_NewString(ctx, arr[i]);
        if (JS_IsException(str))
            goto fail;
        if (JS_DefinePropertyValueUint32(ctx, result, (uint32_t)i, str,
                                          JS_PROP_C_W_E) < 0)
            goto fail;
    }

    js_canonicalizeLocaleList_free_c(ctx, arr, count);
    return result;
 fail:
    js_canonicalizeLocaleList_free_c(ctx, arr, count);
    JS_FreeValue(ctx, result);
    return JS_EXCEPTION;
}

/* --------------------------------------------------------------------- */
/* Intl.getCanonicalLocales (locales)                                    */
/* --------------------------------------------------------------------- */

static JSValue js_intl_getCanonicalLocales(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv)
{
    return js_canonicalizeLocaleList(ctx, argv[0]);
}

/* --------------------------------------------------------------------- */
/* Intl.supportedValuesOf (key)                                          */
/* --------------------------------------------------------------------- */

static JSValue js_intl_supportedValuesOf(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    const char *key;
    JSValue result;

    key = JS_ToCStringNZ(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
    if (!key)
        return JS_EXCEPTION;

    result = JS_NewArray(ctx);
    if (!strcmp(key, "calendar")) {
        int i;
        for (i = 0; i < CAL_COUNT; i++) {
            JS_DefinePropertyValueUint32(ctx, result, (uint32_t)i,
                                         JS_NewString(ctx, ti_calendar_enum_to_string(i)),
                                         JS_PROP_C_W_E);
        }
    } else if (!strcmp(key, "collation")) {
        int i;
        for (i = 0; i < intl_supported_collations_count; i++) {
            JS_DefinePropertyValueUint32(ctx, result, (uint32_t)i,
                                         JS_NewString(ctx, intl_supported_collations[i]),
                                         JS_PROP_C_W_E);
        }
    } else if (!strcmp(key, "currency")) {
        int i;
        for (i = 0; i < intl_supported_currencies_count; i++) {
            JS_DefinePropertyValueUint32(ctx, result, (uint32_t)i,
                                         JS_NewString(ctx, intl_supported_currencies[i]),
                                         JS_PROP_C_W_E);
        }
    } else if (!strcmp(key, "numberingSystem")) {
        int i;
        for (i = 0; i < intl_all_numbering_systems_count; i++) {
            JS_DefinePropertyValueUint32(ctx, result, (uint32_t)i,
                                         JS_NewString(ctx, intl_all_numbering_systems[i]),
                                         JS_PROP_C_W_E);
        }
    } else if (!strcmp(key, "timeZone")) {
        int i;
        for (i = 0; i < ti_tz_zone_count; i++) {
            JS_DefinePropertyValueUint32(ctx, result, (uint32_t)i,
                                         JS_NewString(ctx, ti_tz_zone_data[i].canonical_name),
                                         JS_PROP_C_W_E);
        }
    } else if (!strcmp(key, "unit")) {
        int i;
        for (i = 0; i < sanctioned_simple_units_count; i++) {
            JS_DefinePropertyValueUint32(ctx, result, (uint32_t)i,
                                         JS_NewString(ctx, sanctioned_simple_units[i]),
                                         JS_PROP_C_W_E);
        }
    } else {
        JS_FreeValue(ctx, result);
        JS_ThrowRangeError(ctx, "invalid key: %s", key);
        JS_FreeCString(ctx, key);
        return JS_EXCEPTION;
    }

    JS_FreeCString(ctx, key);
    return result;
}

/* --------------------------------------------------------------------- */
/* Intl object definition                                                */
/* --------------------------------------------------------------------- */

static const JSCFunctionListEntry js_intl_funcs[] = {
    JS_CFUNC_DEF("getCanonicalLocales", 1, js_intl_getCanonicalLocales),
    JS_CFUNC_DEF("supportedValuesOf", 1, js_intl_supportedValuesOf),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Intl", JS_PROP_CONFIGURABLE),
};

int JS_AddIntrinsicIntl(JSContext *ctx)
{
    JSValue global_obj, intl_obj;
    int ret;

    global_obj = JS_GetGlobalObject(ctx);
    intl_obj = JS_NewObject(ctx);
    if (JS_IsException(intl_obj))
        goto fail;

    /* add functions and Symbol.toStringTag */
    ret = JS_SetPropertyFunctionList(ctx, intl_obj, js_intl_funcs,
                                     countof(js_intl_funcs));
    if (ret < 0)
        goto fail;

    /* add Locale constructor */
    ret = js_add_locale(ctx, intl_obj);
    if (ret < 0)
        goto fail;

    /* add NumberFormat constructor */
    ret = js_add_numberformat(ctx, intl_obj);
    if (ret < 0)
        goto fail;

    /* add Collator constructor */
    ret = js_add_collator(ctx, intl_obj);
    if (ret < 0)
        goto fail;

    /* add DateTimeFormat constructor */
    ret = js_add_datetimeformat(ctx, intl_obj);
    if (ret < 0)
        goto fail;

    /* add PluralRules constructor */
    ret = js_add_pluralrules(ctx, intl_obj);
    if (ret < 0)
        goto fail;

    /* add ListFormat constructor */
    ret = js_add_listformat(ctx, intl_obj);
    if (ret < 0)
        goto fail;

    /* add RelativeTimeFormat constructor */
    ret = js_add_relativetimeformat(ctx, intl_obj);
    if (ret < 0)
        goto fail;

    /* add DisplayNames constructor */
    ret = js_add_displaynames(ctx, intl_obj);
    if (ret < 0)
        goto fail;

    /* add Segmenter constructor */
    ret = js_add_segmenter(ctx, intl_obj);
    if (ret < 0)
        goto fail;

    /* add DurationFormat constructor */
    ret = js_add_durationformat(ctx, intl_obj);
    if (ret < 0)
        goto fail;

    /* Create [[FallbackSymbol]] for legacy constructor chaining */
    {
        JSValue sym = JS_NewSymbol(ctx, "IntlLegacyConstructedSymbol", FALSE);
        if (JS_IsException(sym))
            goto fail;
        JS_SetContextValue(ctx, JS_INTL_FALLBACK_SYMBOL, sym);
    }

    /* set Intl on global object */
    ret = JS_SetPropertyStr(ctx, global_obj, "Intl", intl_obj);
    JS_FreeValue(ctx, global_obj);
    if (ret < 0)
        return -1;

    js_intl_set_locale_callbacks(ctx);

    return 0;
fail:
    JS_FreeValue(ctx, intl_obj);
    JS_FreeValue(ctx, global_obj);
    return -1;
}

/* --------------------------------------------------------------------- */
/* Date locale callbacks                                                 */
/* --------------------------------------------------------------------- */

int js_intl_options_has_date_fields(JSContext *ctx, JSValueConst options)
{
    static const char *date_fields[] = {"weekday", "year", "month", "day", "dateStyle"};
    int i;
    if (JS_IsUndefined(options) || JS_IsNull(options))
        return 0;
    for (i = 0; i < 5; i++) {
        JSValue v = JS_GetPropertyStr(ctx, options, date_fields[i]);
        if (JS_IsException(v)) {
            JS_FreeValue(ctx, v);
            return -1;
        }
        if (!JS_IsUndefined(v)) {
            JS_FreeValue(ctx, v);
            return 1;
        }
        JS_FreeValue(ctx, v);
    }
    return 0;
}

int js_intl_options_has_time_fields(JSContext *ctx, JSValueConst options)
{
    static const char *time_fields[] = {"hour", "minute", "second", "timeStyle"};
    int i;
    if (JS_IsUndefined(options) || JS_IsNull(options))
        return 0;
    for (i = 0; i < 4; i++) {
        JSValue v = JS_GetPropertyStr(ctx, options, time_fields[i]);
        if (JS_IsException(v)) {
            JS_FreeValue(ctx, v);
            return -1;
        }
        if (!JS_IsUndefined(v)) {
            JS_FreeValue(ctx, v);
            return 1;
        }
        JS_FreeValue(ctx, v);
    }
    return 0;
}

static JSValue js_intl_date_toLocaleString(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv, int part)
{
    double dval;
    JSValue ctor, obj, format_func, result;
    JSValue new_argv[2];
    int has_options = (argc > 1 && !JS_IsUndefined(argv[1]));

    if (JS_ThisDateValue(ctx, &dval, this_val) < 0)
        return JS_EXCEPTION;
    if (isnan(dval))
        return JS_NewString(ctx, "Invalid Date");

    ctor = JS_GetContextValue(ctx, JS_INTL_CTOR_DATETIMEFORMAT);
    if (JS_IsUndefined(ctor))
        return JS_UNDEFINED;

    new_argv[0] = argc > 0 ? JS_DupValue(ctx, argv[0]) : JS_UNDEFINED;
    if (has_options) {
        new_argv[1] = JS_DupValue(ctx, argv[1]);
    } else {
        new_argv[1] = JS_NewObject(ctx);
        if (JS_IsException(new_argv[1])) {
            JS_FreeValue(ctx, ctor);
            JS_FreeValue(ctx, new_argv[0]);
            return JS_EXCEPTION;
        }
    }

    if (!JS_IsException(new_argv[1])) {
        if (part == 3) { /* toLocaleString */
            if (!has_options ||
                (!js_intl_options_has_date_fields(ctx, new_argv[1]) &&
                 !js_intl_options_has_time_fields(ctx, new_argv[1]))) {
                JS_DefinePropertyValueStr(ctx, new_argv[1], "year",
                                           JS_NewString(ctx, "numeric"), JS_PROP_C_W_E);
                JS_DefinePropertyValueStr(ctx, new_argv[1], "month",
                                           JS_NewString(ctx, "numeric"), JS_PROP_C_W_E);
                JS_DefinePropertyValueStr(ctx, new_argv[1], "day",
                                           JS_NewString(ctx, "numeric"), JS_PROP_C_W_E);
                JS_DefinePropertyValueStr(ctx, new_argv[1], "hour",
                                           JS_NewString(ctx, "numeric"), JS_PROP_C_W_E);
                JS_DefinePropertyValueStr(ctx, new_argv[1], "minute",
                                           JS_NewString(ctx, "numeric"), JS_PROP_C_W_E);
                JS_DefinePropertyValueStr(ctx, new_argv[1], "second",
                                           JS_NewString(ctx, "numeric"), JS_PROP_C_W_E);
            }
        } else if (part == 1) { /* toLocaleDateString */
            if (!has_options || !js_intl_options_has_date_fields(ctx, new_argv[1])) {
                JS_DefinePropertyValueStr(ctx, new_argv[1], "year",
                                           JS_NewString(ctx, "numeric"), JS_PROP_C_W_E);
                JS_DefinePropertyValueStr(ctx, new_argv[1], "month",
                                           JS_NewString(ctx, "numeric"), JS_PROP_C_W_E);
                JS_DefinePropertyValueStr(ctx, new_argv[1], "day",
                                           JS_NewString(ctx, "numeric"), JS_PROP_C_W_E);
            }
        } else if (part == 2) { /* toLocaleTimeString */
            if (!has_options || !js_intl_options_has_time_fields(ctx, new_argv[1])) {
                JS_DefinePropertyValueStr(ctx, new_argv[1], "hour",
                                           JS_NewString(ctx, "numeric"), JS_PROP_C_W_E);
                JS_DefinePropertyValueStr(ctx, new_argv[1], "minute",
                                           JS_NewString(ctx, "numeric"), JS_PROP_C_W_E);
                JS_DefinePropertyValueStr(ctx, new_argv[1], "second",
                                           JS_NewString(ctx, "numeric"), JS_PROP_C_W_E);
            }
        }
    }

    obj = JS_CallConstructor(ctx, ctor, 2, new_argv);
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, new_argv[0]);
    JS_FreeValue(ctx, new_argv[1]);
    if (JS_IsException(obj))
        return JS_EXCEPTION;
    format_func = JS_GetPropertyStr(ctx, obj, "format");
    if (JS_IsException(format_func)) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    result = JS_Call(ctx, format_func, obj, 1, &this_val);
    JS_FreeValue(ctx, format_func);
    JS_FreeValue(ctx, obj);
    return result;
}

static JSValue js_intl_string_localeCompare(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv)
{
    JSValue ctor, collator, compare, ret, str_a, str_b;
    JSValueConst collator_argv[2];
    int collator_argc = 0;

    ctor = JS_GetContextValue(ctx, JS_INTL_CTOR_COLLATOR);
    if (JS_IsException(ctor))
        return JS_EXCEPTION;

    if (argc > 1)
        collator_argv[collator_argc++] = argv[1];
    if (argc > 2)
        collator_argv[collator_argc++] = argv[2];

    collator = JS_CallConstructor(ctx, ctor, collator_argc, collator_argv);
    JS_FreeValue(ctx, ctor);
    if (JS_IsException(collator))
        return JS_EXCEPTION;

    compare = JS_GetPropertyStr(ctx, collator, "compare");
    if (JS_IsException(compare)) {
        JS_FreeValue(ctx, collator);
        return JS_EXCEPTION;
    }

    if (JS_IsNull(this_val) || JS_IsUndefined(this_val)) {
        JS_FreeValue(ctx, compare);
        JS_FreeValue(ctx, collator);
        return JS_ThrowTypeError(ctx, "null or undefined are forbidden");
    }

    str_a = JS_ToString(ctx, this_val);
    if (JS_IsException(str_a)) {
        JS_FreeValue(ctx, compare);
        JS_FreeValue(ctx, collator);
        return JS_EXCEPTION;
    }

    str_b = JS_ToString(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
    if (JS_IsException(str_b)) {
        JS_FreeValue(ctx, str_a);
        JS_FreeValue(ctx, compare);
        JS_FreeValue(ctx, collator);
        return JS_EXCEPTION;
    }

    {
        JSValueConst compare_argv[2] = { str_a, str_b };
        ret = JS_Call(ctx, compare, JS_UNDEFINED, 2, compare_argv);
    }

    JS_FreeValue(ctx, str_a);
    JS_FreeValue(ctx, str_b);
    JS_FreeValue(ctx, compare);
    JS_FreeValue(ctx, collator);
    return ret;
}

/* --------------------------------------------------------------------- */
/* Numbering-system digit helpers                                        */
/* --------------------------------------------------------------------- */

static const uint32_t nf_digit_base[NS_COUNT] = {
    [NS_ADLM]    = 0x1E950,
    [NS_AHOM]    = 0x11730,
    [NS_ARAB]    = 0x0660,
    [NS_ARABEXT] = 0x06F0,
    [NS_BALI]    = 0x1B50,
    [NS_BENG]    = 0x09E6,
    [NS_BHKS]    = 0x11C50,
    [NS_BRAH]    = 0x11066,
    [NS_CAKM]    = 0x11136,
    [NS_CHAM]    = 0xAA50,
    [NS_DEVA]    = 0x0966,
    [NS_DIAK]    = 0x11950,
    [NS_FULLWIDE]= 0xFF10,
    [NS_GARA]    = 0x10D40,
    [NS_GONG]    = 0x11DA0,
    [NS_GONM]    = 0x11D50,
    [NS_GUJR]    = 0x0AE6,
    [NS_GUKH]    = 0x16130,
    [NS_GURU]    = 0x0A66,
    [NS_HMNG]    = 0x16B50,
    [NS_HMNP]    = 0x1E140,
    [NS_JAVA]    = 0xA9D0,
    [NS_KALI]    = 0xA900,
    [NS_KAWI]    = 0x11F50,
    [NS_KHMR]    = 0x17E0,
    [NS_KNDA]    = 0x0CE6,
    [NS_KRAI]    = 0x16D70,
    [NS_LANA]    = 0x1A80,
    [NS_LANATHAM]= 0x1A90,
    [NS_LAOO]    = 0x0ED0,
    [NS_LATN]    = 0x0030,
    [NS_LEPC]    = 0x1C40,
    [NS_LIMB]    = 0x1946,
    [NS_MATHBOLD]= 0x1D7CE,
    [NS_MATHDBL] = 0x1D7D8,
    [NS_MATHMONO]= 0x1D7F6,
    [NS_MATHSANB]= 0x1D7EC,
    [NS_MATHSANS]= 0x1D7E2,
    [NS_MLYM]    = 0x0D66,
    [NS_MODI]    = 0x11650,
    [NS_MONG]    = 0x1810,
    [NS_MROO]    = 0x16A60,
    [NS_MTEI]    = 0xABF0,
    [NS_MYMR]    = 0x1040,
    [NS_MYMREPKA]= 0x116DA,
    [NS_MYMRPAO] = 0x116D0,
    [NS_MYMRSHAN] = 0x1090,
    [NS_MYMRTLNG]= 0xA9F0,
    [NS_NAGM]    = 0x1E4F0,
    [NS_NEWA]    = 0x11450,
    [NS_NKOO]    = 0x07C0,
    [NS_OLCK]    = 0x1C50,
    [NS_ONAO]    = 0x1E5F1,
    [NS_ORYA]    = 0x0B66,
    [NS_OSMA]    = 0x104A0,
    [NS_OUTLINED]= 0x1CCF0,
    [NS_ROHG]    = 0x10D30,
    [NS_SAUR]    = 0xA8D0,
    [NS_SEGMENT] = 0x1FBF0,
    [NS_SHRD]    = 0x111D0,
    [NS_SIND]    = 0x112F0,
    [NS_SINH]    = 0x0DE6,
    [NS_SORA]    = 0x110F0,
    [NS_SUND]    = 0x1BB0,
    [NS_SUNU]    = 0x11BF0,
    [NS_TAKR]    = 0x116C0,
    [NS_TALU]    = 0x19D0,
    [NS_TAMLDEC] = 0x0BE6,
    [NS_TELU]    = 0x0C66,
    [NS_THAI]    = 0x0E50,
    [NS_TIBT]    = 0x0F20,
    [NS_TIRH]    = 0x114D0,
    [NS_TNSA]    = 0x16AC0,
    [NS_TOLS]    = 0x11DE0,
    [NS_VAII]    = 0xA620,
    [NS_WARA]    = 0x118E0,
    [NS_WCHO]    = 0x1E2F0,
};

static const uint32_t hanidec_cps[10] = {
    0x3007, 0x4E00, 0x4E8C, 0x4E09, 0x56DB,
    0x4E94, 0x516D, 0x4E03, 0x516B, 0x4E5D
};

uint32_t nf_get_digit(int ns, int d)
{
    if (ns == NS_HANIDEC)
        return hanidec_cps[d];
    if (ns >= 0 && ns < NS_COUNT && nf_digit_base[ns] != 0)
        return nf_digit_base[ns] + (uint32_t)d;
    return 0;
}

void nf_put_digit(DynBuf *db, int ns, int d)
{
    uint8_t buf[4];
    uint32_t cp = nf_get_digit(ns, d);
    int len;
    if (cp != 0) {
        len = unicode_to_utf8(buf, cp);
        dbuf_put(db, buf, (size_t)len);
    } else {
        dbuf_putc(db, (uint8_t)('0' + d));
    }
}

/* --------------------------------------------------------------------- */
/* Locale data lookup                                                    */
/* --------------------------------------------------------------------- */

const LocaleData *js_intl_find_locale_data(const char *locale)
{
    int i;
    for (i = 0; i < locale_data_count; i++) {
        if (!strcmp(locale_data_table[i].locale, locale))
            return &locale_data_table[i];
    }
    for (i = 0; i < locale_data_count; i++) {
        const char *p = locale_data_table[i].locale;
        int len = (int)strlen(p);
        if (!strncmp(locale, p, (size_t)len) && (locale[len] == '\0' || locale[len] == '-'))
            return &locale_data_table[i];
    }
    return &locale_data_table[0];
}

int js_intl_get_currency_digits(const char *currency)
{
    if (!strcmp(currency, "JPY") || !strcmp(currency, "KRW") ||
        !strcmp(currency, "VND") || !strcmp(currency, "ISK"))
        return 0;
    if (!strcmp(currency, "CLF") || !strcmp(currency, "BHD") ||
        !strcmp(currency, "JOD") || !strcmp(currency, "KWD") ||
        !strcmp(currency, "OMR") || !strcmp(currency, "TND"))
        return 3;
    if (!strcmp(currency, "CLP") || !strcmp(currency, "KMF") ||
        !strcmp(currency, "GNF") || !strcmp(currency, "PYG") ||
        !strcmp(currency, "RWF"))
        return 0;
    return 2;
}

static int decode_leb128(const uint8_t **pp)
{
    const uint8_t *p = *pp;
    int val = 0;
    int shift = 0;
    uint8_t b;
    do {
        b = *p++;
        val |= (b & 0x7f) << shift;
        shift += 7;
    } while (b & 0x80);
    *pp = p;
    return val;
}

const char *js_intl_get_currency_symbol(const char *currency, const char *locale)
{
    const LocaleData *ld = js_intl_find_locale_data(locale);
    const uint8_t *p = locale_currency_data + ld->currency_offset;
    int i;
    for (i = 0; i < ld->num_currencies; i++) {
        int code_idx = decode_leb128(&p);
        int sym_idx = decode_leb128(&p);
        if (!strcmp(currency_code_tab[code_idx], currency))
            return currency_sym_tab[sym_idx];
    }
    return currency;
}

/* --------------------------------------------------------------------- */
/* Locale callbacks registration                                         */
static int js_intl_get_string_locale_lang(JSContext *ctx, int argc, JSValueConst *argv)
{
    char *canonical;
    int lang = 0;
    if (js_canonicalizeLocaleList_first(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, &canonical) < 0)
        return -1;
    if (!canonical)
        return 0;
    if (!strncasecmp(canonical, "tr", 2) || !strncasecmp(canonical, "az", 2))
        lang = 1; /* Turkish / Azeri */
    else if (!strncasecmp(canonical, "lt", 2))
        lang = 2; /* Lithuanian */
    js_free(ctx, canonical);
    return lang;
}

/* --------------------------------------------------------------------- */

/* DisplayNames lookup in a zero-separated key\0value\0key\0value... pool */
const char *js_intl_find_display_name(const char *pool, const char *code)
{
    const char *p = pool;
    while (*p) {
        if (!strcmp(p, code))
            return p + strlen(p) + 1;
        p += strlen(p) + 1;
        p += strlen(p) + 1;
    }
    return NULL;
}

static void js_intl_set_locale_callbacks(JSContext *ctx)
{
    static JSLocaleCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.number_toLocaleString = js_intl_number_toLocaleString;
    callbacks.bigint_toLocaleString = js_intl_bigint_toLocaleString;
    callbacks.date_toLocaleString = js_intl_date_toLocaleString;
    callbacks.string_localeCompare = js_intl_string_localeCompare;
    callbacks.get_string_locale_lang = js_intl_get_string_locale_lang;
    JS_SetLocaleCallbacks(ctx, &callbacks);
}

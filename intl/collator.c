/*
 * QuickJS Intl.Collator implementation
 */

#include <string.h>
#include <ctype.h>

#include "../quickjs.h"
#include "../cutils.h"
#include "../libunicode.h"
#include "intl_private.h"

static JSClassID js_collator_class_id;

enum {
    COLLATOR_USAGE_SORT = 0,
    COLLATOR_USAGE_SEARCH,
};
static const char *collator_usage_names[] = {"sort", "search"};

enum {
    COLLATOR_SENSITIVITY_BASE = 0,
    COLLATOR_SENSITIVITY_ACCENT,
    COLLATOR_SENSITIVITY_CASE,
    COLLATOR_SENSITIVITY_VARIANT,
};
static const char *collator_sensitivity_names[] = {"base", "accent", "case", "variant"};

enum {
    COLLATOR_CASEFIRST_UPPER = 0,
    COLLATOR_CASEFIRST_LOWER,
    COLLATOR_CASEFIRST_FALSE,
};
static const char *collator_casefirst_names[] = {"upper", "lower", "false"};

typedef struct {
    char *locale;
    char *locale_canonical;
    int usage;
    int sensitivity;
    int caseFirst;
    int numeric;
    int ignorePunctuation;
    char *collation;
    int hasNumeric;
    int hasCaseFirst;
} JSCollatorData;

static void js_collator_finalizer(JSRuntime *rt, JSValue val)
{
    JSCollatorData *s = JS_GetOpaque(val, js_collator_class_id);
    if (s) {
        js_free_rt(rt, s->locale);
        js_free_rt(rt, s->locale_canonical);
        js_free_rt(rt, s->collation);
        js_free_rt(rt, s);
    }
}

static int locale_has_key(const char *locale, const char *key)
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
        /* skip this subtag and continue */
        if (*next == '-')
            next++;
        u = next;
    }
    return 0;
}

static int is_collation_supported(const char *collation, const char *locale)
{
    if (!strcmp(collation, "default"))
        return 1;
    if (!strcmp(locale, "de") || !strncmp(locale, "de-", 3)) {
        if (!strcmp(collation, "phonebk") || !strcmp(collation, "eor"))
            return 1;
    }
    return 0;
}

static JSClassDef js_collator_class = {
    "Collator",
    .finalizer = js_collator_finalizer,
};

static int js_collator_get_locale(JSContext *ctx, JSCollatorData *s,
                                   int argc, JSValueConst *argv)
{
    char *loc = js_intl_extract_locale(ctx, argc, argv);
    if (!loc)
        return -1;
    js_free(ctx, s->locale);
    s->locale = loc;
    return 0;
}

static JSValue js_collator_constructor(JSContext *ctx,
                                        JSValueConst func_obj,
                                        JSValueConst this_val,
                                        int argc, JSValueConst *argv,
                                        int flags)
{
    JSCollatorData *s;
    JSValue obj = JS_UNDEFINED;
    JSValue options = JS_UNDEFINED;
    JSValueConst new_target;
    int is_new = (flags & JS_CALL_FLAG_CONSTRUCTOR) != 0;

    if (is_new) {
        new_target = this_val;
    } else {
        new_target = func_obj;
    }

    s = js_mallocz(ctx, sizeof(*s));
    if (!s)
        return JS_EXCEPTION;

    s->locale = js_strdup(ctx, "en-US");
    s->usage = COLLATOR_USAGE_SORT;
    s->sensitivity = COLLATOR_SENSITIVITY_VARIANT;
    s->caseFirst = COLLATOR_CASEFIRST_FALSE;
    s->numeric = 0;
    s->ignorePunctuation = 0;
    s->collation = js_strdup(ctx, "default");

    if (js_collator_get_locale(ctx, s, argc, argv) < 0)
        goto fail;

    /* Get canonical locale with extensions for resolvedOptions */
    {
        char *canonical = NULL;
        if (js_canonicalizeLocaleList_first(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, &canonical) < 0)
            goto fail;
        s->locale_canonical = canonical;
    }

    /* Default ignorePunctuation for Thai */
    if (s->locale && (!strcmp(s->locale, "th") || !strncmp(s->locale, "th-", 3))) {
        s->ignorePunctuation = 1;
    }

    /* Apply locale extensions as defaults */
    if (s->locale_canonical) {
        if (locale_has_key(s->locale_canonical, "kn")) {
            char *v = js_intl_locale_get_unicode_key(ctx, s->locale_canonical, "kn");
            s->hasNumeric = 1;
            s->numeric = (v == NULL) ? 1 : 0;
            js_free(ctx, v);
        }
        if (locale_has_key(s->locale_canonical, "kf")) {
            char *v = js_intl_locale_get_unicode_key(ctx, s->locale_canonical, "kf");
            s->hasCaseFirst = 1;
            if (v) {
                if (!strcmp(v, "upper")) s->caseFirst = COLLATOR_CASEFIRST_UPPER;
                else if (!strcmp(v, "lower")) s->caseFirst = COLLATOR_CASEFIRST_LOWER;
                else s->caseFirst = COLLATOR_CASEFIRST_FALSE;
            } else {
                s->caseFirst = COLLATOR_CASEFIRST_FALSE;
            }
            js_free(ctx, v);
        }
        {
            char *v = js_intl_locale_get_unicode_key(ctx, s->locale_canonical, "co");
            if (v) {
                if (is_collation_supported(v, s->locale)) {
                    js_free(ctx, s->collation);
                    s->collation = js_strdup(ctx, v);
                }
                js_free(ctx, v);
            }
        }
    }

    /* parse options */
    if (argc > 1 && !JS_IsUndefined(argv[1])) {
        options = JS_ToObject(ctx, argv[1]);
        if (JS_IsException(options))
            goto fail;
    } else {
        options = JS_NewObjectProto(ctx, JS_NULL);
        if (JS_IsException(options))
            goto fail;
    }

    {
        int idx = js_intl_get_string_option(ctx, options, "localeMatcher",
                                             intl_localeMatcher_names,
                                             countof(intl_localeMatcher_names),
                                             INTL_LOCALEMATCHER_BESTFIT);
        if (idx < 0)
            goto fail;
    }

    {
        int idx = js_intl_get_string_option(ctx, options, "usage",
                                             collator_usage_names,
                                             countof(collator_usage_names),
                                             COLLATOR_USAGE_SORT);
        if (idx < 0)
            goto fail;
        s->usage = idx;
    }

    {
        int idx = js_intl_get_string_option(ctx, options, "sensitivity",
                                             collator_sensitivity_names,
                                             countof(collator_sensitivity_names),
                                             COLLATOR_SENSITIVITY_VARIANT);
        if (idx < 0)
            goto fail;
        s->sensitivity = idx;
    }

    {
        JSValue val = JS_GetPropertyStr(ctx, options, "ignorePunctuation");
        if (JS_IsException(val))
            goto fail;
        if (!JS_IsUndefined(val)) {
            s->ignorePunctuation = JS_ToBool(ctx, val);
        }
        JS_FreeValue(ctx, val);
    }

    {
        JSValue val = JS_GetPropertyStr(ctx, options, "collation");
        if (JS_IsException(val))
            goto fail;
        if (!JS_IsUndefined(val)) {
            const char *str = JS_ToCString(ctx, val);
            JS_FreeValue(ctx, val);
            if (str) {
                int valid = 1;
                int len = (int)strlen(str);
                int i;
                if (len < 3 || len > 8) valid = 0;
                for (i = 0; i < len && valid; i++) {
                    if (!isalnum((unsigned char)str[i]))
                        valid = 0;
                }
                if (!valid) {
                    JS_FreeCString(ctx, str);
                    JS_ThrowRangeError(ctx, "invalid collation option");
                    goto fail;
                }
                if (is_collation_supported(str, s->locale)) {
                    js_free(ctx, s->collation);
                    s->collation = js_strdup(ctx, str);
                } else {
                    /* Unsupported option: keep locale extension if supported */
                    char *co_val = js_intl_locale_get_unicode_key(ctx, s->locale_canonical, "co");
                    if (co_val && is_collation_supported(co_val, s->locale)) {
                        js_free(ctx, s->collation);
                        s->collation = js_strdup(ctx, co_val);
                    } else {
                        js_free(ctx, s->collation);
                        s->collation = js_strdup(ctx, "default");
                    }
                    js_free(ctx, co_val);
                }
                JS_FreeCString(ctx, str);
            }
        }
    }

    {
        JSValue val = JS_GetPropertyStr(ctx, options, "numeric");
        if (JS_IsException(val))
            goto fail;
        if (!JS_IsUndefined(val)) {
            s->numeric = JS_ToBool(ctx, val);
            s->hasNumeric = 1;
        }
        JS_FreeValue(ctx, val);
    }

    {
        JSValue val = JS_GetPropertyStr(ctx, options, "caseFirst");
        if (JS_IsException(val))
            goto fail;
        if (!JS_IsUndefined(val)) {
            size_t slen;
            const char *str = JS_ToCStringLen(ctx, &slen, val);
            JS_FreeValue(ctx, val);
            if (str) {
                int idx = -1;
                int i;
                for (i = 0; i < (int)countof(collator_casefirst_names); i++) {
                    size_t vlen = strlen(collator_casefirst_names[i]);
                    if (slen == vlen && !memcmp(str, collator_casefirst_names[i], vlen)) {
                        idx = i;
                        break;
                    }
                }
                JS_FreeCString(ctx, str);
                if (idx < 0) {
                    JS_ThrowRangeError(ctx, "invalid caseFirst option");
                    goto fail;
                }
                s->caseFirst = idx;
                s->hasCaseFirst = 1;
            }
        } else {
            JS_FreeValue(ctx, val);
        }
    }

    JS_FreeValue(ctx, options);
    options = JS_UNDEFINED;

    /* Rebuild locale with preserved Unicode extensions */
    if (s->locale_canonical) {
        int preserve_kn = 0, preserve_kf = 0, preserve_co = 0;

        if (locale_has_key(s->locale_canonical, "kn")) {
            char *v = js_intl_locale_get_unicode_key(ctx, s->locale_canonical, "kn");
            int kn_val = (v == NULL) ? 1 : 0;
            js_free(ctx, v);
            if (s->hasNumeric && s->numeric == kn_val)
                preserve_kn = 1;
        }
        if (locale_has_key(s->locale_canonical, "kf")) {
            char *v = js_intl_locale_get_unicode_key(ctx, s->locale_canonical, "kf");
            const char *resolved_kf = collator_casefirst_names[s->caseFirst];
            int kf_matches = 0;
            if (v) {
                kf_matches = !strcmp(v, resolved_kf);
            } else {
                kf_matches = !strcmp(resolved_kf, "false");
            }
            js_free(ctx, v);
            if (s->hasCaseFirst && kf_matches)
                preserve_kf = 1;
        }
        {
            char *v = js_intl_locale_get_unicode_key(ctx, s->locale_canonical, "co");
            if (v) {
                if (!strcmp(v, s->collation))
                    preserve_co = 1;
                js_free(ctx, v);
            }
        }

        if (preserve_kn || preserve_kf || preserve_co) {
            DynBuf db;
            js_intl_dbuf_init(ctx, &db);
            dbuf_putstr(&db, s->locale);
            dbuf_put(&db, (uint8_t *)"-u", 2);
            if (preserve_kn) {
                char *v = js_intl_locale_get_unicode_key(ctx, s->locale_canonical, "kn");
                if (v) {
                    dbuf_put(&db, (uint8_t *)"-kn-", 4);
                    dbuf_putstr(&db, v);
                    js_free(ctx, v);
                } else {
                    dbuf_put(&db, (uint8_t *)"-kn", 3);
                }
            }
            if (preserve_kf) {
                char *v = js_intl_locale_get_unicode_key(ctx, s->locale_canonical, "kf");
                if (v) {
                    dbuf_put(&db, (uint8_t *)"-kf-", 4);
                    dbuf_putstr(&db, v);
                    js_free(ctx, v);
                } else {
                    dbuf_put(&db, (uint8_t *)"-kf", 3);
                }
            }
            if (preserve_co) {
                char *v = js_intl_locale_get_unicode_key(ctx, s->locale_canonical, "co");
                if (v) {
                    dbuf_put(&db, (uint8_t *)"-co-", 4);
                    dbuf_putstr(&db, v);
                    js_free(ctx, v);
                }
            }
            dbuf_put(&db, (uint8_t *)"", 1);
            if (dbuf_error(&db)) {
                dbuf_free(&db);
                return JS_EXCEPTION;
            }
            js_free(ctx, s->locale);
            s->locale = js_strdup(ctx, (char *)db.buf);
            dbuf_free(&db);
        }
    }

    obj = JS_CreateFromConstructor(ctx, new_target, js_collator_class_id);
    if (JS_IsException(obj))
        goto fail;
    JS_SetOpaque(obj, s);
    return obj;
 fail:
    JS_FreeValue(ctx, options);
    if (s->locale) js_free(ctx, s->locale);
    if (s->locale_canonical) js_free(ctx, s->locale_canonical);
    if (s->collation) js_free(ctx, s->collation);
    js_free(ctx, s);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

/* Check if character is a Unicode combining mark */
static int is_combining_mark(uint32_t c)
{
    return (c >= 0x0300 && c <= 0x036F) ||
           (c >= 0x1AB0 && c <= 0x1AFF) ||
           (c >= 0x1DC0 && c <= 0x1DFF) ||
           (c >= 0x20D0 && c <= 0x20FF) ||
           (c >= 0xFE20 && c <= 0xFE2F);
}

/* Check if character is significant when ignorePunctuation is true */
static int is_significant(uint32_t c)
{
    if (c >= '0' && c <= '9')
        return 1;
    if (c < 128)
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    return lre_is_id_start(c);
}

/* Compare two characters with caseFirst ordering */
static int compare_case_order(uint32_t c1, uint32_t c2, int caseFirst)
{
    int upper1 = (c1 >= 'A' && c1 <= 'Z');
    int upper2 = (c2 >= 'A' && c2 <= 'Z');
    int lower1 = (c1 >= 'a' && c1 <= 'z');
    int lower2 = (c2 >= 'a' && c2 <= 'z');

    if (upper1 && lower2) {
        if (caseFirst == COLLATOR_CASEFIRST_UPPER)
            return -1;
        else
            return 1;
    }
    if (lower1 && upper2) {
        if (caseFirst == COLLATOR_CASEFIRST_UPPER)
            return 1;
        else
            return -1;
    }
    if (c1 < c2) return -1;
    if (c1 > c2) return 1;
    return 0;
}

/* Transform a string for collation comparison.
   level: 0=base, 1=accent/secondary, 2=case, 3=tertiary */
static uint32_t *transform_string(JSContext *ctx, JSCollatorData *s,
                                  const uint32_t *src, int src_len, int *out_len,
                                  int level)
{
    uint32_t *nfd = NULL;
    int nfd_len;
    uint32_t *result;
    int result_cap, result_len = 0;
    int i;
    int phonebook = 0;
    int german_sort = 0;
    int turkish = 0;

    if (s->collation && !strcmp(s->collation, "phonebk"))
        phonebook = 1;
    if (s->usage == COLLATOR_USAGE_SEARCH && s->locale &&
        (!strcmp(s->locale, "de") || !strncmp(s->locale, "de-", 3)))
        phonebook = 1;
    if (s->usage == COLLATOR_USAGE_SORT && s->locale &&
        (!strcmp(s->locale, "de") || !strncmp(s->locale, "de-", 3)) &&
        !phonebook)
        german_sort = 1;
    if (s->locale &&
        (!strcmp(s->locale, "tr") || !strncmp(s->locale, "tr-", 3) ||
         !strcmp(s->locale, "az") || !strncmp(s->locale, "az-", 3)))
        turkish = 1;

    nfd_len = unicode_normalize(&nfd, src, src_len, UNICODE_NFD,
                                JS_GetRuntime(ctx), (DynBufReallocFunc *)js_realloc_rt);
    if (nfd_len < 0) return NULL;

    result_cap = nfd_len * 4 + 4;
    result = js_malloc(ctx, sizeof(uint32_t) * (size_t)result_cap);
    if (!result) {
        js_free(ctx, nfd);
        return NULL;
    }

    for (i = 0; i < nfd_len; i++) {
        uint32_t c = nfd[i];
        uint32_t folded[3];
        int flen;

        if (s->ignorePunctuation && !is_significant(c))
            continue;

        if ((level == 0 || level == 2) && is_combining_mark(c))
            continue;

        if (level == 0 || level == 1) {
            /* Casefold for base and accent/secondary */
            int mapped = 0;

            /* Handle NFD decomposed umlauts: base + combining diaeresis */
            if (i + 1 < nfd_len && nfd[i + 1] == 0x0308) {
                if (phonebook) {
                    if (c == 'A' || c == 'a') {
                        result[result_len++] = 'a';
                        result[result_len++] = 'e';
                        mapped = 1;
                    } else if (c == 'O' || c == 'o') {
                        result[result_len++] = 'o';
                        result[result_len++] = 'e';
                        mapped = 1;
                    } else if (c == 'U' || c == 'u') {
                        result[result_len++] = 'u';
                        result[result_len++] = 'e';
                        mapped = 1;
                    }
                } else if (german_sort) {
                    if (c == 'A' || c == 'a') {
                        result[result_len++] = 'a';
                        result[result_len++] = 1;
                        mapped = 1;
                    } else if (c == 'O' || c == 'o') {
                        result[result_len++] = 'o';
                        result[result_len++] = 1;
                        mapped = 1;
                    } else if (c == 'U' || c == 'u') {
                        result[result_len++] = 'u';
                        result[result_len++] = 1;
                        mapped = 1;
                    }
                }
                if (mapped) {
                    i++; /* skip combining diaeresis */
                    continue;
                }
            }

            if (phonebook) {
                switch (c) {
                case 0x00DF:
                    result[result_len++] = 's';
                    result[result_len++] = 's';
                    continue;
                }
            }

            flen = lre_case_conv(folded, c, 2);
            /* Turkish: map dotless i (ı) to sort before dotted i */
            if (turkish && flen == 1 && folded[0] == 0x0131) {
                if (result_len + 2 > result_cap) {
                    int new_cap = result_cap * 2;
                    uint32_t *new_result = js_realloc(ctx, result, sizeof(uint32_t) * (size_t)new_cap);
                    if (!new_result) {
                        js_free(ctx, nfd);
                        js_free(ctx, result);
                        return NULL;
                    }
                    result = new_result;
                    result_cap = new_cap;
                }
                result[result_len++] = 0x0068; /* h */
                result[result_len++] = 0xFFFF; /* synthetic weight between h and i */
                continue;
            }
            if (result_len + flen > result_cap) {
                int new_cap = result_cap * 2;
                uint32_t *new_result = js_realloc(ctx, result, sizeof(uint32_t) * (size_t)new_cap);
                if (!new_result) {
                    js_free(ctx, nfd);
                    js_free(ctx, result);
                    return NULL;
                }
                result = new_result;
                result_cap = new_cap;
            }
            memcpy(&result[result_len], folded, (size_t)flen * sizeof(uint32_t));
            result_len += flen;
        } else {
            /* Preserve case for case and tertiary */
            if (result_len >= result_cap) {
                int new_cap = result_cap * 2;
                uint32_t *new_result = js_realloc(ctx, result, sizeof(uint32_t) * (size_t)new_cap);
                if (!new_result) {
                    js_free(ctx, nfd);
                    js_free(ctx, result);
                    return NULL;
                }
                result = new_result;
                result_cap = new_cap;
            }
            result[result_len++] = c;
        }
    }

    js_free(ctx, nfd);
    *out_len = result_len;
    return result;
}

static int js_UTF32_compare_len(const uint32_t *buf1, int len1,
                                 const uint32_t *buf2, int len2)
{
    int i, len, c;
    len = len1 < len2 ? len1 : len2;
    for (i = 0; i < len; i++) {
        c = (int)buf1[i] - (int)buf2[i];
        if (c != 0)
            return c;
    }
    if (len1 == len2)
        return 0;
    else if (len1 < len2)
        return -1;
    else
        return 1;
}

/* bound compare function */
static int js_string_normalize1(JSContext *ctx, uint32_t **pout_buf,
                                 JSValueConst val, int n_type)
{
    int buf_len, out_len;
    uint32_t *buf, *out_buf;

    buf_len = JS_ToUTF32String(ctx, &buf, val);
    if (buf_len < 0)
        return -1;
    out_len = unicode_normalize(&out_buf, buf, buf_len, n_type,
                                JS_GetRuntime(ctx), (DynBufReallocFunc *)js_realloc_rt);
    js_free(ctx, buf);
    if (out_len < 0)
        return -1;
    *pout_buf = out_buf;
    return out_len;
}

static JSValue js_collator_bound_compare(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv,
                                          int magic, JSValue *func_data)
{
    JSValue a, b;
    int cmp = 0, a_len, b_len;
    uint32_t *a_buf, *b_buf;
    uint32_t *a_key = NULL, *b_key = NULL;
    int a_key_len = 0, b_key_len = 0;
    uint32_t *a_key2 = NULL, *b_key2 = NULL;
    int a_key2_len = 0, b_key2_len = 0;
    JSCollatorData *s = JS_GetOpaque2(ctx, func_data[0], js_collator_class_id);
    if (!s)
        return JS_EXCEPTION;

    a = JS_ToString(ctx, argv[0]);
    if (JS_IsException(a))
        return JS_EXCEPTION;
    b = JS_ToString(ctx, argv[1]);
    if (JS_IsException(b)) {
        JS_FreeValue(ctx, a);
        return JS_EXCEPTION;
    }

    a_len = js_string_normalize1(ctx, &a_buf, a, UNICODE_NFD);
    JS_FreeValue(ctx, a);
    if (a_len < 0) {
        JS_FreeValue(ctx, b);
        return JS_EXCEPTION;
    }

    b_len = js_string_normalize1(ctx, &b_buf, b, UNICODE_NFD);
    JS_FreeValue(ctx, b);
    if (b_len < 0) {
        js_free(ctx, a_buf);
        return JS_EXCEPTION;
    }

    switch (s->sensitivity) {
    case COLLATOR_SENSITIVITY_BASE:
        a_key = transform_string(ctx, s, a_buf, a_len, &a_key_len, 0);
        b_key = transform_string(ctx, s, b_buf, b_len, &b_key_len, 0);
        if (a_key && b_key)
            cmp = js_UTF32_compare_len(a_key, a_key_len, b_key, b_key_len);
        break;
    case COLLATOR_SENSITIVITY_ACCENT:
        a_key = transform_string(ctx, s, a_buf, a_len, &a_key_len, 1);
        b_key = transform_string(ctx, s, b_buf, b_len, &b_key_len, 1);
        if (a_key && b_key)
            cmp = js_UTF32_compare_len(a_key, a_key_len, b_key, b_key_len);
        break;
    case COLLATOR_SENSITIVITY_CASE:
        a_key = transform_string(ctx, s, a_buf, a_len, &a_key_len, 2);
        b_key = transform_string(ctx, s, b_buf, b_len, &b_key_len, 2);
        if (a_key && b_key)
            cmp = js_UTF32_compare_len(a_key, a_key_len, b_key, b_key_len);
        break;
    case COLLATOR_SENSITIVITY_VARIANT:
        a_key = transform_string(ctx, s, a_buf, a_len, &a_key_len, 1);
        b_key = transform_string(ctx, s, b_buf, b_len, &b_key_len, 1);
        if (a_key && b_key)
            cmp = js_UTF32_compare_len(a_key, a_key_len, b_key, b_key_len);
        if (cmp == 0) {
            int i;
            a_key2 = transform_string(ctx, s, a_buf, a_len, &a_key2_len, 3);
            b_key2 = transform_string(ctx, s, b_buf, b_len, &b_key2_len, 3);
            if (a_key2 && b_key2) {
                int min_len = a_key2_len < b_key2_len ? a_key2_len : b_key2_len;
                for (i = 0; i < min_len; i++) {
                    cmp = compare_case_order(a_key2[i], b_key2[i], s->caseFirst);
                    if (cmp != 0)
                        break;
                }
                if (cmp == 0) {
                    if (a_key2_len < b_key2_len) cmp = -1;
                    else if (a_key2_len > b_key2_len) cmp = 1;
                }
            }
        }
        break;
    }

    js_free(ctx, a_buf);
    js_free(ctx, b_buf);
    js_free(ctx, a_key);
    js_free(ctx, b_key);
    js_free(ctx, a_key2);
    js_free(ctx, b_key2);
    return JS_NewInt32(ctx, cmp);
}

/* getter for Intl.Collator.prototype.compare */
static JSValue js_collator_get_compare(JSContext *ctx, JSValueConst this_val)
{
    JSCollatorData *s = JS_GetOpaque2(ctx, this_val, js_collator_class_id);
    if (!s)
        return JS_EXCEPTION;

    JSValueConst data[1] = { this_val };
    return JS_NewCFunctionData(ctx, js_collator_bound_compare,
                               2, 0, 1, data);
}

static JSValue js_collator_resolvedOptions(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv)
{
    JSValue obj;
    JSCollatorData *s = JS_GetOpaque2(ctx, this_val, js_collator_class_id);
    if (!s)
        return JS_EXCEPTION;

    obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        return JS_EXCEPTION;

    JS_DefinePropertyValueStr(ctx, obj, "locale",
                               JS_NewString(ctx, s->locale), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "usage",
                               JS_NewString(ctx, collator_usage_names[s->usage]), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "sensitivity",
                               JS_NewString(ctx, collator_sensitivity_names[s->sensitivity]), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "ignorePunctuation",
                               JS_NewBool(ctx, s->ignorePunctuation), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "collation",
                               JS_NewString(ctx, s->collation), JS_PROP_C_W_E);
    if (s->hasNumeric)
        JS_DefinePropertyValueStr(ctx, obj, "numeric",
                                   JS_NewBool(ctx, s->numeric), JS_PROP_C_W_E);
    if (s->hasCaseFirst)
        JS_DefinePropertyValueStr(ctx, obj, "caseFirst",
                                   JS_NewString(ctx, collator_casefirst_names[s->caseFirst]), JS_PROP_C_W_E);
    return obj;
}

static JSValue js_collator_supportedLocalesOf(JSContext *ctx, JSValueConst this_val,
                                               int argc, JSValueConst *argv)
{
    return js_intl_supportedLocalesOf(ctx, this_val, argc, argv);
}

static const JSCFunctionListEntry js_collator_proto_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Intl.Collator", JS_PROP_CONFIGURABLE),
    JS_CGETSET_DEF("compare", js_collator_get_compare, NULL),
    JS_CFUNC_DEF("resolvedOptions", 0, js_collator_resolvedOptions),
};

static const JSCFunctionListEntry js_collator_static_funcs[] = {
    JS_CFUNC_DEF("supportedLocalesOf", 1, js_collator_supportedLocalesOf),
};

int js_add_collator(JSContext *ctx, JSValue intl_obj)
{
    JS_NewClassID(&js_collator_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_collator_class_id, &js_collator_class);

    JSCFunctionType ft = { .constructor_or_func_flags = js_collator_constructor };
    
    if (JS_IsException(js_intl_new_constructor(ctx, intl_obj,
                                                JS_INTL_CTOR_COLLATOR,
                                                js_collator_class_id, "Collator",
                                                ft.generic, 0,
                                                JS_CFUNC_constructor_or_func_flags, 0,
                                                js_collator_static_funcs, countof(js_collator_static_funcs),
                                                js_collator_proto_funcs, countof(js_collator_proto_funcs))))
        return -1;

    return 0;
}

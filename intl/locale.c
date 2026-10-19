/*
 * QuickJS Intl.Locale implementation (ECMA-402)
 */

#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "../quickjs.h"
#include "../cutils.h"
#include "../temporal/temporal_private.h"
#include "intl_private.h"

static JSClassID js_locale_class_id;

typedef struct {
    char *locale;
    char *baseName;
    char *language;
    char *script;
    char *region;
    char *variants;
    char *calendar;
    char *collation;
    char *hourCycle;
    char *numberingSystem;
    char *caseFirst;
    char *firstDayOfWeek;
    int numeric; /* -1 = undefined, 0 = false, 1 = true */
} JSLocaleData;

static void js_locale_finalizer(JSRuntime *rt, JSValue val)
{
    JSLocaleData *s = JS_GetOpaque(val, js_locale_class_id);
    if (s) {
# define FREE_FIELD(f) if (s->f) js_free_rt(rt, s->f)
        FREE_FIELD(locale);
        FREE_FIELD(baseName);
        FREE_FIELD(language);
        FREE_FIELD(script);
        FREE_FIELD(region);
        FREE_FIELD(variants);
        FREE_FIELD(calendar);
        FREE_FIELD(collation);
        FREE_FIELD(hourCycle);
        FREE_FIELD(numberingSystem);
        FREE_FIELD(caseFirst);
        FREE_FIELD(firstDayOfWeek);
#undef FREE_FIELD
        js_free_rt(rt, s);
    }
}

static JSClassDef js_locale_class = {
    "Locale",
    .finalizer = js_locale_finalizer,
};

/* --------------------------------------------------------------------- */
/* Locale subtag validation helpers                                      */
/* --------------------------------------------------------------------- */

static int is_valid_language_subtag(const char *s)
{
    int len = (int)strlen(s);
    int i;
    if (len < 2 || len > 8) return 0;
    if (len == 4) return 0; /* 4-letter is script, not language */
    for (i = 0; i < len; i++)
        if (!isalpha((unsigned char)s[i])) return 0;
    return 1;
}

static int is_valid_script_subtag(const char *s)
{
    int len = (int)strlen(s);
    int i;
    if (len != 4) return 0;
    for (i = 0; i < len; i++)
        if (!isalpha((unsigned char)s[i])) return 0;
    return 1;
}

static int is_valid_region_subtag(const char *s)
{
    int len = (int)strlen(s);
    int i;
    if (len == 2) {
        for (i = 0; i < len; i++)
            if (!isalpha((unsigned char)s[i])) return 0;
        return 1;
    }
    if (len == 3) {
        for (i = 0; i < len; i++)
            if (!isdigit((unsigned char)s[i])) return 0;
        return 1;
    }
    return 0;
}

static int is_valid_variant_subtag(const char *s)
{
    int len = (int)strlen(s);
    int i;
    if (len >= 5 && len <= 8) {
        for (i = 0; i < len; i++)
            if (!isalnum((unsigned char)s[i])) return 0;
        return 1;
    }
    if (len == 4) {
        if (!isdigit((unsigned char)s[0])) return 0;
        for (i = 1; i < 4; i++)
            if (!isalnum((unsigned char)s[i])) return 0;
        return 1;
    }
    return 0;
}

static void to_title_case(char *s)
{
    if (s && s[0])
        s[0] = (char)toupper((unsigned char)s[0]);
    if (s) {
        int i;
        for (i = 1; s[i]; i++)
            s[i] = (char)tolower((unsigned char)s[i]);
    }
}

static void to_upper_case(char *s)
{
    int i;
    if (s) {
        for (i = 0; s[i]; i++)
            s[i] = (char)toupper((unsigned char)s[i]);
    }
}

static void to_lower_case(char *s)
{
    int i;
    if (s) {
        for (i = 0; s[i]; i++)
            s[i] = (char)tolower((unsigned char)s[i]);
    }
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

/* Validate a Unicode locale "type" nonterminal: one or more 3-8 alphanum segments */
static int is_valid_type(const char *s)
{
    int len = (int)strlen(s);
    int i = 0;
    if (len == 0) return 0;
    while (i < len) {
        int seg_len = 0;
        while (i < len && s[i] != '-') {
            if (!isalnum((unsigned char)s[i])) return 0;
            seg_len++;
            i++;
        }
        if (seg_len < 3 || seg_len > 8) return 0;
        if (s[i] == '-') {
            i++;
            if (i >= len) return 0;
        }
    }
    return 1;
}

static int is_valid_hour_cycle(const char *s)
{
    return !strcmp(s, "h11") || !strcmp(s, "h12") ||
           !strcmp(s, "h23") || !strcmp(s, "h24");
}

static int is_valid_case_first(const char *s)
{
    return !strcmp(s, "upper") || !strcmp(s, "lower") || !strcmp(s, "false");
}

static const char *weekday_to_u_value(const char *s)
{
    if (!strcasecmp(s, "0") || !strcasecmp(s, "7")) return "sun";
    if (!strcasecmp(s, "1")) return "mon";
    if (!strcasecmp(s, "2")) return "tue";
    if (!strcasecmp(s, "3")) return "wed";
    if (!strcasecmp(s, "4")) return "thu";
    if (!strcasecmp(s, "5")) return "fri";
    if (!strcasecmp(s, "6")) return "sat";
    return NULL;
}

/* --------------------------------------------------------------------- */
/* Locale option application                                             */
/* --------------------------------------------------------------------- */

typedef struct {
    char *key;
    char *value;
} UKeyword;

static int cmp_ukeyword(const void *a, const void *b)
{
    const UKeyword *ka = a;
    const UKeyword *kb = b;
    return strcmp(ka->key, kb->key);
}

static int locale_find_u_extension(const char *tag, int *u_start, int *u_end)
{
    int i = 0, len = (int)strlen(tag);
    int in_ext = 0;
    char ext_char = 0;
    int saw_other_ext = 0;
    *u_start = -1;
    *u_end = -1;
    while (i < len) {
        int j = i;
        while (j < len && tag[j] != '-') j++;
        int slen = j - i;
        if (slen == 1 && isalpha((unsigned char)tag[i])) {
            if (in_ext && ext_char == 'u' && *u_start >= 0) {
                *u_end = i - 1;
                return 1;
            }
            ext_char = tag[i];
            in_ext = 1;
            if (ext_char == 'u' && !saw_other_ext)
                *u_start = i;
            else
                saw_other_ext = 1;
        }
        i = j + 1;
    }
    if (in_ext && ext_char == 'u' && *u_start >= 0) {
        *u_end = len;
        return 1;
    }
    return 0;
}

static const char *canonicalize_keyword_value(const char *key, const char *value)
{
    if (!strcasecmp(key, "ca")) {
        if (!strcasecmp(value, "islamicc")) return "islamic-civil";
        if (!strcasecmp(value, "ethiopic-amete-alem")) return "ethioaa";
        if (!strcasecmp(value, "gregorian")) return "gregory";
    }
    return value;
}

/* CanonicalizeUValue (ecma402): ASCII-lowercase the value, then canonicalize
   it as a value of key per UTS35 Annex C. A value which canonicalizes to
   "true" is removed, i.e. the empty String is returned.
   Returns a newly allocated string. */
static char *canonicalize_u_value(JSContext *ctx, const char *key, const char *value)
{
    char *lower = js_strdup(ctx, value);
    const char *canon;
    if (!lower)
        return NULL;
    to_lower_case(lower);
    canon = canonicalize_keyword_value(key, lower);
    if (canon != lower) {
        js_free(ctx, lower);
        lower = js_strdup(ctx, canon);
        if (!lower)
            return NULL;
    }
    if (!strcmp(lower, "true")) {
        js_free(ctx, lower);
        lower = js_strdup(ctx, "");
    }
    return lower;
}

static int locale_parse_u_keywords(JSContext *ctx, const char *u_ext, int u_len,
                                   UKeyword *keywords, int max_kw,
                                   char **attributes)
{
    int i = 2; /* skip "u-" */
    int num_kw = 0;
    *attributes = NULL;
    while (i < u_len) {
        int j = i;
        while (j < u_len && u_ext[j] != '-') j++;
        int slen = j - i;
        if (slen == 2 && isalnum((unsigned char)u_ext[i]) &&
            isalpha((unsigned char)u_ext[i+1])) {
            /* keyword key */
            char key[3] = {u_ext[i], u_ext[i+1], '\0'};
            i = j + 1;
            /* read value(s) */
            int vstart = i;
            while (i < u_len) {
                int k = i;
                while (k < u_len && u_ext[k] != '-') k++;
                int vlen = k - i;
                if (vlen == 2 && isalpha((unsigned char)u_ext[i]) &&
                    (isalpha((unsigned char)u_ext[i+1]) || isdigit((unsigned char)u_ext[i+1])))
                    break; /* next keyword key */
                i = k + 1;
            }
            {
                int k;
                int already_seen = 0;
                for (k = 0; k < num_kw; k++) {
                    if (!strcmp(keywords[k].key, key)) {
                        already_seen = 1;
                        break;
                    }
                }
                if (!already_seen && num_kw < max_kw) {
                    keywords[num_kw].key = js_strdup(ctx, key);
                    {
                        int vlen = i - vstart - (i < u_len ? 1 : 0);
                        if (vlen < 0) vlen = 0;
                        if (vlen > 0 && u_ext[vstart + vlen - 1] == '-') vlen--;
                        keywords[num_kw].value = js_malloc(ctx, (size_t)vlen + 1);
                        if (keywords[num_kw].value) {
                            const char *cval;
                            memcpy(keywords[num_kw].value, u_ext + vstart, (size_t)vlen);
                            keywords[num_kw].value[vlen] = '\0';
                            cval = canonicalize_keyword_value(key, keywords[num_kw].value);
                            if (cval != keywords[num_kw].value) {
                                js_free(ctx, keywords[num_kw].value);
                                keywords[num_kw].value = js_strdup(ctx, cval);
                            }
                        }
                    }
                    num_kw++;
                }
            }
        } else if (slen >= 3 && slen <= 8) {
            /* attribute */
            int alen = j - i;
            if (!*attributes) {
                *attributes = js_malloc(ctx, (size_t)alen + 1);
                if (*attributes) {
                    memcpy(*attributes, u_ext + i, (size_t)alen);
                    (*attributes)[alen] = '\0';
                }
            } else {
                int old_len = (int)strlen(*attributes);
                char *new_attr = js_malloc(ctx, (size_t)old_len + 1 + (size_t)alen + 1);
                if (new_attr) {
                    memcpy(new_attr, *attributes, (size_t)old_len);
                    new_attr[old_len] = '-';
                    memcpy(new_attr + old_len + 1, u_ext + i, (size_t)alen);
                    new_attr[old_len + 1 + alen] = '\0';
                    js_free(ctx, *attributes);
                    *attributes = new_attr;
                }
            }
            i = j + 1;
        } else {
            i = j + 1;
        }
    }
    return num_kw;
}

/* Manual split of base name by '-' without strtok */
static int locale_split_subtags(char *buf, char **subtags, int max_subtags)
{
    int n = 0;
    char *p = buf;
    while (*p && n < max_subtags) {
        char *start = p;
        while (*p && *p != '-') p++;
        subtags[n] = start;
        n++;
        if (*p == '-') {
            *p = '\0';
            p++;
        }
    }
    return n;
}

static char *locale_build_locale_from_options(JSContext *ctx, const char *canonical,
                                               JSValueConst options, JSLocaleData *out)
{
    DynBuf db;
    char *baseName = NULL;
    char *result = NULL;
    char *language = NULL, *script = NULL, *region = NULL, *variants = NULL;
    char *u_ext_str = NULL;
    char *u_attributes = NULL;
    char *ext_suffix = NULL;
    char *subtags[16];
    int n_subtags = 0;
    UKeyword u_keywords[16];
    int num_u_keywords = 0;
    int u_start = -1, u_end = -1;
    int i, has_u = 0;
    JSValue val;
    const char *str;

    js_intl_dbuf_init(ctx, &db);

    memset(out, 0, sizeof(*out));
    out->numeric = -1;
    memset(u_keywords, 0, sizeof(u_keywords));

    /* 1. Extract base name and existing u-extension */
    has_u = locale_find_u_extension(canonical, &u_start, &u_end);
    if (has_u) {
        baseName = js_malloc(ctx, (size_t)u_start);
        if (baseName) {
            memcpy(baseName, canonical, (size_t)u_start - 1);
            baseName[u_start - 1] = '\0';
        }
        u_ext_str = js_malloc(ctx, (size_t)(u_end - u_start) + 1);
        if (u_ext_str) {
            memcpy(u_ext_str, canonical + u_start, (size_t)(u_end - u_start));
            u_ext_str[u_end - u_start] = '\0';
        }
        if (u_ext_str)
            num_u_keywords = locale_parse_u_keywords(ctx, u_ext_str, (int)strlen(u_ext_str),
                                                      u_keywords, 16, &u_attributes);
    } else {
        baseName = js_strdup(ctx, canonical);
    }

    if (!baseName)
        goto fail;

    /* 2. Parse baseName into components and set defaults */
    {
        char buf[256];
        int idx;
        int ext_start = -1;
        pstrcpy(buf, sizeof(buf), baseName);
        n_subtags = locale_split_subtags(buf, subtags, 16);
        for (i = 0; i < n_subtags; i++)
            subtags[i] = js_strdup(ctx, subtags[i]);
        /* find first extension singleton (length 1 alpha) */
        for (i = 1; i < n_subtags; i++) {
            if (strlen(subtags[i]) == 1 && isalpha((unsigned char)subtags[i][0])) {
                ext_start = i;
                break;
            }
        }
        /* Save extension suffix from canonical */
        if (ext_start >= 0 && !has_u) {
            /* find position of first extension in canonical */
            int canon_len = (int)strlen(canonical);
            int pos = 0, subtag_idx = 0;
            while (pos < canon_len && subtag_idx < ext_start) {
                int dash = pos;
                while (dash < canon_len && canonical[dash] != '-') dash++;
                subtag_idx++;
                pos = dash + 1;
            }
            if (pos <= canon_len) {
                ext_suffix = js_strdup(ctx, canonical + pos - 1); /* include leading dash */
            }
        }
        if (n_subtags > 0) language = js_strdup(ctx, subtags[0]);
        idx = 1;
        if (idx < n_subtags && (ext_start < 0 || idx < ext_start) && strlen(subtags[idx]) == 4 &&
            isalpha((unsigned char)subtags[idx][0]) &&
            isalpha((unsigned char)subtags[idx][1]) &&
            isalpha((unsigned char)subtags[idx][2]) &&
            isalpha((unsigned char)subtags[idx][3])) {
            script = js_strdup(ctx, subtags[idx]);
            to_title_case(script);
            idx++;
        }
        if (idx < n_subtags && (ext_start < 0 || idx < ext_start) && (strlen(subtags[idx]) == 2 || strlen(subtags[idx]) == 3)) {
            int is_region = 1;
            int j;
            for (j = 0; j < (int)strlen(subtags[idx]); j++) {
                if (strlen(subtags[idx]) == 2 && !isalpha((unsigned char)subtags[idx][j])) is_region = 0;
                if (strlen(subtags[idx]) == 3 && !isdigit((unsigned char)subtags[idx][j])) is_region = 0;
            }
            if (is_region) {
                region = js_strdup(ctx, subtags[idx]);
                to_upper_case(region);
                idx++;
            }
        }
        if (idx < n_subtags && (ext_start < 0 || idx < ext_start)) {
            int vlen = 0;
            int j;
            int num_variants = ext_start < 0 ? n_subtags - idx : ext_start - idx;
            /* sort variants lexicographically */
            if (num_variants > 1)
                qsort(&subtags[idx], (size_t)num_variants, sizeof(char *), cmp_str);
            for (j = idx; j < idx + num_variants; j++) vlen += (int)strlen(subtags[j]) + 1;
            variants = js_malloc(ctx, (size_t)vlen);
            if (variants) {
                variants[0] = '\0';
                for (j = idx; j < idx + num_variants; j++) {
                    if (j > idx) pstrcat(variants, vlen, "-");
                    pstrcat(variants, vlen, subtags[j]);
                }
                to_lower_case(variants);
            }
        }
        for (i = 0; i < n_subtags; i++)
            js_free(ctx, subtags[i]);
        n_subtags = 0;
    }

    /* 3. Read language option */
    val = JS_GetPropertyStr(ctx, options, "language");
    if (JS_IsException(val)) goto fail;
    if (!JS_IsUndefined(val)) {
        str = JS_ToCStringNZ(ctx, val);
        JS_FreeValue(ctx, val);
        if (!str) goto fail;
        if (!is_valid_language_subtag(str)) {
            JS_FreeCString(ctx, str);
            JS_ThrowRangeError(ctx, "invalid language subtag");
            goto fail;
        }
        js_free(ctx, language);
        language = js_strdup(ctx, str);
        JS_FreeCString(ctx, str);
    }

    /* 4. Read script option */
    val = JS_GetPropertyStr(ctx, options, "script");
    if (JS_IsException(val)) goto fail;
    if (!JS_IsUndefined(val)) {
        str = JS_ToCStringNZ(ctx, val);
        JS_FreeValue(ctx, val);
        if (!str) goto fail;
        if (!is_valid_script_subtag(str)) {
            JS_FreeCString(ctx, str);
            JS_ThrowRangeError(ctx, "invalid script subtag");
            goto fail;
        }
        js_free(ctx, script);
        script = js_strdup(ctx, str);
        to_title_case(script);
        JS_FreeCString(ctx, str);
    }

    /* 5. Read region option */
    val = JS_GetPropertyStr(ctx, options, "region");
    if (JS_IsException(val)) goto fail;
    if (!JS_IsUndefined(val)) {
        str = JS_ToCStringNZ(ctx, val);
        JS_FreeValue(ctx, val);
        if (!str) goto fail;
        if (!is_valid_region_subtag(str)) {
            JS_FreeCString(ctx, str);
            JS_ThrowRangeError(ctx, "invalid region subtag");
            goto fail;
        }
        js_free(ctx, region);
        region = js_strdup(ctx, str);
        to_upper_case(region);
        JS_FreeCString(ctx, str);
    }

    /* 6. Read variants option */
    val = JS_GetPropertyStr(ctx, options, "variants");
    if (JS_IsException(val)) goto fail;
    if (!JS_IsUndefined(val)) {
        str = JS_ToCStringNZ(ctx, val);
        JS_FreeValue(ctx, val);
        if (!str) goto fail;
        if (strlen(str) == 0) {
            JS_FreeCString(ctx, str);
            JS_ThrowRangeError(ctx, "invalid variants");
            goto fail;
        }
        {
            char vbuf[256];
            char *vsubtags[16];
            int vn;
            pstrcpy(vbuf, sizeof(vbuf), str);
            vn = locale_split_subtags(vbuf, vsubtags, 16);
            {
                int dash_count = 0;
                for (i = 0; str[i]; i++) if (str[i] == '-') dash_count++;
                if ((vn == 0 && dash_count > 0) || (vn > 0 && dash_count != vn - 1)) {
                    JS_FreeCString(ctx, str);
                    JS_ThrowRangeError(ctx, "invalid variant subtag");
                    goto fail;
                }
            }
            for (i = 0; i < vn; i++) {
                if (!is_valid_variant_subtag(vsubtags[i])) {
                    JS_FreeCString(ctx, str);
                    JS_ThrowRangeError(ctx, "invalid variant subtag");
                    goto fail;
                }
            }
            /* check duplicates (case-insensitive) */
            for (i = 0; i < vn; i++) {
                int j;
                for (j = i + 1; j < vn; j++) {
                    if (!strcasecmp(vsubtags[i], vsubtags[j])) {
                        JS_FreeCString(ctx, str);
                        JS_ThrowRangeError(ctx, "duplicate variant subtag");
                        goto fail;
                    }
                }
            }
            /* sort variants lexicographically */
            qsort(vsubtags, (size_t)vn, sizeof(char *), cmp_str);
            {
                int j, vlen = 0;
                for (j = 0; j < vn; j++) vlen += (int)strlen(vsubtags[j]) + 1;
                js_free(ctx, variants);
                variants = js_malloc(ctx, (size_t)vlen);
                if (variants) {
                    variants[0] = '\0';
                    for (j = 0; j < vn; j++) {
                        if (j > 0) pstrcat(variants, vlen, "-");
                        pstrcat(variants, vlen, vsubtags[j]);
                    }
                    to_lower_case(variants);
                }
            }
        }
        JS_FreeCString(ctx, str);
    }

    /* 7. Rebuild baseName */
    if (language) dbuf_putstr(&db, language);
    if (script) {
        dbuf_putc(&db, (uint8_t)'-');
        dbuf_putstr(&db, script);
    }
    if (region) {
        dbuf_putc(&db, (uint8_t)'-');
        dbuf_putstr(&db, region);
    }
    if (variants) {
        dbuf_putc(&db, (uint8_t)'-');
        dbuf_putstr(&db, variants);
    }
    dbuf_putc(&db, '\0');
    {
        char *new_base = js_strdup(ctx, (const char *)db.buf);
        js_free(ctx, baseName);
        baseName = new_base;
    }
    db.size = 0;

    /* 8. Read and validate unicode extension options */
    {
        /* calendar */
        val = JS_GetPropertyStr(ctx, options, "calendar");
        if (JS_IsException(val)) goto fail;
        if (!JS_IsUndefined(val)) {
            str = JS_ToCStringNZ(ctx, val);
JS_FreeValue(ctx, val);
if (!str) goto fail;
            if (!is_valid_type(str)) {
                JS_FreeCString(ctx, str);
                JS_ThrowRangeError(ctx, "invalid calendar");
                goto fail;
            }
            {
                const char *cal_val_raw = canonicalize_keyword_value("ca", str);
                char *cal_val = canonicalize_u_value(ctx, "ca", cal_val_raw);
                if (!cal_val) {
                    JS_FreeCString(ctx, str);
                    goto fail;
                }
                /* replace or add keyword */
                for (i = 0; i < num_u_keywords; i++) {
                    if (!strcmp(u_keywords[i].key, "ca")) {
                        js_free(ctx, u_keywords[i].value);
                        u_keywords[i].value = js_strdup(ctx, cal_val);
                        break;
                    }
                }
                if (i == num_u_keywords && num_u_keywords < 16) {
                    u_keywords[num_u_keywords].key = js_strdup(ctx, "ca");
                    u_keywords[num_u_keywords].value = js_strdup(ctx, cal_val);
                    num_u_keywords++;
                }
                js_free(ctx, out->calendar);
                out->calendar = js_strdup(ctx, cal_val);
                js_free(ctx, cal_val);
                JS_FreeCString(ctx, str);
            }
        }

        /* collation */
        val = JS_GetPropertyStr(ctx, options, "collation");
        if (JS_IsException(val)) goto fail;
        if (!JS_IsUndefined(val)) {
            str = JS_ToCStringNZ(ctx, val);
JS_FreeValue(ctx, val);
if (!str) goto fail;
            if (!is_valid_type(str)) {
                JS_FreeCString(ctx, str);
                JS_ThrowRangeError(ctx, "invalid collation");
                goto fail;
            }
            if (!is_valid_type(str)) {
                JS_FreeCString(ctx, str);
                JS_ThrowRangeError(ctx, "invalid collation");
                goto fail;
            }
            {
                char *co_val = canonicalize_u_value(ctx, "co", str);
                if (!co_val) {
                    JS_FreeCString(ctx, str);
                    goto fail;
                }
                for (i = 0; i < num_u_keywords; i++) {
                    if (!strcmp(u_keywords[i].key, "co")) {
                        js_free(ctx, u_keywords[i].value);
                        u_keywords[i].value = js_strdup(ctx, co_val);
                        break;
                    }
                }
                if (i == num_u_keywords && num_u_keywords < 16) {
                    u_keywords[num_u_keywords].key = js_strdup(ctx, "co");
                    u_keywords[num_u_keywords].value = js_strdup(ctx, co_val);
                    num_u_keywords++;
                }
                js_free(ctx, out->collation);
                out->collation = js_strdup(ctx, co_val);
                js_free(ctx, co_val);
            }
            JS_FreeCString(ctx, str);
        }

        /* firstDayOfWeek */
        val = JS_GetPropertyStr(ctx, options, "firstDayOfWeek");
        if (JS_IsException(val)) goto fail;
        if (!JS_IsUndefined(val)) {
            str = JS_ToCStringNZ(ctx, val);
JS_FreeValue(ctx, val);
if (!str) goto fail;
            {
                const char *fw_val = weekday_to_u_value(str);
                char *fw_canon = NULL;
                if (!fw_val) {
                    /* not a weekday name, validate as type */
                    if (!is_valid_type(str)) {
                        JS_FreeCString(ctx, str);
                        JS_ThrowRangeError(ctx, "invalid firstDayOfWeek");
                        goto fail;
                    }
                    fw_canon = canonicalize_u_value(ctx, "fw", str);
                    if (!fw_canon) {
                        JS_FreeCString(ctx, str);
                        goto fail;
                    }
                    fw_val = fw_canon;
                }
                for (i = 0; i < num_u_keywords; i++) {
                    if (!strcmp(u_keywords[i].key, "fw")) {
                        js_free(ctx, u_keywords[i].value);
                        u_keywords[i].value = js_strdup(ctx, fw_val);
                        break;
                    }
                }
                if (i == num_u_keywords && num_u_keywords < 16) {
                    u_keywords[num_u_keywords].key = js_strdup(ctx, "fw");
                    u_keywords[num_u_keywords].value = js_strdup(ctx, fw_val);
                    num_u_keywords++;
                }
                js_free(ctx, out->firstDayOfWeek);
                out->firstDayOfWeek = js_strdup(ctx, fw_val);
                js_free(ctx, fw_canon);
            }
            JS_FreeCString(ctx, str);
        }

        /* hourCycle */
        val = JS_GetPropertyStr(ctx, options, "hourCycle");
        if (JS_IsException(val)) goto fail;
        if (!JS_IsUndefined(val)) {
            str = JS_ToCStringNZ(ctx, val);
JS_FreeValue(ctx, val);
if (!str) goto fail;
            if (!is_valid_hour_cycle(str)) {
                JS_FreeCString(ctx, str);
                JS_ThrowRangeError(ctx, "invalid hourCycle");
                goto fail;
            }
            for (i = 0; i < num_u_keywords; i++) {
                if (!strcmp(u_keywords[i].key, "hc")) {
                    js_free(ctx, u_keywords[i].value);
                    u_keywords[i].value = js_strdup(ctx, str);
                    break;
                }
            }
            if (i == num_u_keywords && num_u_keywords < 16) {
                u_keywords[num_u_keywords].key = js_strdup(ctx, "hc");
                u_keywords[num_u_keywords].value = js_strdup(ctx, str);
                num_u_keywords++;
            }
            out->hourCycle = js_strdup(ctx, str);
            JS_FreeCString(ctx, str);
        }

        /* caseFirst */
        val = JS_GetPropertyStr(ctx, options, "caseFirst");
        if (JS_IsException(val)) goto fail;
        if (!JS_IsUndefined(val)) {
            str = JS_ToCStringNZ(ctx, val);
JS_FreeValue(ctx, val);
if (!str) goto fail;
            if (!is_valid_case_first(str)) {
                JS_FreeCString(ctx, str);
                JS_ThrowRangeError(ctx, "invalid caseFirst");
                goto fail;
            }
            for (i = 0; i < num_u_keywords; i++) {
                if (!strcmp(u_keywords[i].key, "kf")) {
                    js_free(ctx, u_keywords[i].value);
                    u_keywords[i].value = js_strdup(ctx, str);
                    break;
                }
            }
            if (i == num_u_keywords && num_u_keywords < 16) {
                u_keywords[num_u_keywords].key = js_strdup(ctx, "kf");
                u_keywords[num_u_keywords].value = js_strdup(ctx, str);
                num_u_keywords++;
            }
            out->caseFirst = js_strdup(ctx, str);
            JS_FreeCString(ctx, str);
        }

        /* numeric */
        val = JS_GetPropertyStr(ctx, options, "numeric");
        if (JS_IsException(val)) goto fail;
        if (!JS_IsUndefined(val)) {
            int bool_val = JS_ToBool(ctx, val);
            JS_FreeValue(ctx, val);
            {
                const char *num_str = bool_val ? "true" : "false";
                for (i = 0; i < num_u_keywords; i++) {
                    if (!strcmp(u_keywords[i].key, "kn")) {
                        js_free(ctx, u_keywords[i].value);
                        u_keywords[i].value = js_strdup(ctx, num_str);
                        break;
                    }
                }
                if (i == num_u_keywords && num_u_keywords < 16) {
                    u_keywords[num_u_keywords].key = js_strdup(ctx, "kn");
                    u_keywords[num_u_keywords].value = js_strdup(ctx, num_str);
                    num_u_keywords++;
                }
                out->numeric = bool_val;
            }
        }

        /* numberingSystem */
        val = JS_GetPropertyStr(ctx, options, "numberingSystem");
        if (JS_IsException(val)) goto fail;
        if (!JS_IsUndefined(val)) {
            str = JS_ToCStringNZ(ctx, val);
JS_FreeValue(ctx, val);
if (!str) goto fail;
            if (!is_valid_type(str)) {
                JS_FreeCString(ctx, str);
                JS_ThrowRangeError(ctx, "invalid numberingSystem");
                goto fail;
            }
            if (!is_valid_type(str)) {
                JS_FreeCString(ctx, str);
                JS_ThrowRangeError(ctx, "invalid numberingSystem");
                goto fail;
            }
            {
                char *nu_val = canonicalize_u_value(ctx, "nu", str);
                if (!nu_val) {
                    JS_FreeCString(ctx, str);
                    goto fail;
                }
                for (i = 0; i < num_u_keywords; i++) {
                    if (!strcmp(u_keywords[i].key, "nu")) {
                        js_free(ctx, u_keywords[i].value);
                        u_keywords[i].value = js_strdup(ctx, nu_val);
                        break;
                    }
                }
                if (i == num_u_keywords && num_u_keywords < 16) {
                    u_keywords[num_u_keywords].key = js_strdup(ctx, "nu");
                    u_keywords[num_u_keywords].value = js_strdup(ctx, nu_val);
                    num_u_keywords++;
                }
                js_free(ctx, out->numberingSystem);
                out->numberingSystem = js_strdup(ctx, nu_val);
                js_free(ctx, nu_val);
            }
            JS_FreeCString(ctx, str);
        }
    }

    /* 9. Extract values from existing u-extension for slots not set by options */
    for (i = 0; i < num_u_keywords; i++) {
        if (!out->calendar && !strcmp(u_keywords[i].key, "ca"))
            out->calendar = js_strdup(ctx, u_keywords[i].value);
        if (!out->collation && !strcmp(u_keywords[i].key, "co"))
            out->collation = js_strdup(ctx, u_keywords[i].value);
        if (!out->firstDayOfWeek && !strcmp(u_keywords[i].key, "fw"))
            out->firstDayOfWeek = js_strdup(ctx, u_keywords[i].value);
        if (!out->hourCycle && !strcmp(u_keywords[i].key, "hc"))
            out->hourCycle = js_strdup(ctx, u_keywords[i].value);
        if (!out->caseFirst && !strcmp(u_keywords[i].key, "kf")) {
            if (!strcmp(u_keywords[i].value, "true") || u_keywords[i].value[0] == '\0')
                out->caseFirst = js_strdup(ctx, "");
            else
                out->caseFirst = js_strdup(ctx, u_keywords[i].value);
        }
        if (out->numeric < 0 && !strcmp(u_keywords[i].key, "kn")) {
            if (!strcmp(u_keywords[i].value, "true") || u_keywords[i].value[0] == '\0')
                out->numeric = 1;
            else
                out->numeric = 0;
        }
        if (!out->numberingSystem && !strcmp(u_keywords[i].key, "nu"))
            out->numberingSystem = js_strdup(ctx, u_keywords[i].value);
    }
    /* Default numeric to false when kn keyword is absent */
    if (out->numeric < 0)
        out->numeric = 0;

    /* 10. Rebuild full locale string: baseName + other extensions + u-extension */
    dbuf_putstr(&db, baseName);

    /* Append non-u extensions from original locale */
    if (has_u) {
        /* extensions before u */
        if (u_start > 0) {
            int before_u = 0;
            /* find start of first extension in canonical */
            for (i = 0; canonical[i]; i++) {
                if (canonical[i] == '-' && canonical[i+1] && strlen(canonical+i+1) == 1 &&
                    isalpha((unsigned char)canonical[i+1])) {
                    before_u = i;
                    break;
                }
            }
            if (before_u > 0 && before_u < u_start) {
                dbuf_putc(&db, (uint8_t)'-');
                dbuf_put(&db, (const uint8_t *)(canonical + before_u + 1), (size_t)(u_start - before_u - 2));
            }
        }
    }

    /* Sort u-extension keywords alphabetically by key */
    if (num_u_keywords > 1) {
        qsort(u_keywords, (size_t)num_u_keywords, sizeof(UKeyword), cmp_ukeyword);
    }
    /* Sort u-extension attributes alphabetically */
    if (u_attributes && u_attributes[0]) {
        char attr_buf[256];
        char *attr_subtags[16];
        int attr_n;
        pstrcpy(attr_buf, sizeof(attr_buf), u_attributes);
        attr_n = locale_split_subtags(attr_buf, attr_subtags, 16);
        if (attr_n > 1) {
            int alen = 0;
            qsort(attr_subtags, (size_t)attr_n, sizeof(char *), cmp_str);
            for (i = 0; i < attr_n; i++) alen += (int)strlen(attr_subtags[i]) + 1;
            {
                char *new_attr = js_malloc(ctx, (size_t)alen);
                if (new_attr) {
                    new_attr[0] = '\0';
                    for (i = 0; i < attr_n; i++) {
                        if (i > 0) pstrcat(new_attr, alen, "-");
                        pstrcat(new_attr, alen, attr_subtags[i]);
                    }
                    js_free(ctx, u_attributes);
                    u_attributes = new_attr;
                }
            }
        }
    }

    /* Build u-extension */
    if (num_u_keywords > 0 || u_attributes) {
        dbuf_putc(&db, (uint8_t)'-');
        dbuf_putc(&db, 'u');
        if (u_attributes && u_attributes[0]) {
            dbuf_putc(&db, (uint8_t)'-');
            dbuf_putstr(&db, u_attributes);
        }
        for (i = 0; i < num_u_keywords; i++) {
            dbuf_putc(&db, (uint8_t)'-');
            dbuf_putstr(&db, u_keywords[i].key);
            if (u_keywords[i].value && u_keywords[i].value[0] &&
                strcmp(u_keywords[i].value, "true") != 0) {
                dbuf_putc(&db, (uint8_t)'-');
                dbuf_putstr(&db, u_keywords[i].value);
            }
        }
    }

    /* Append extensions after u */
    if (has_u && u_end < (int)strlen(canonical)) {
        dbuf_put(&db, (const uint8_t *)(canonical + u_end), strlen(canonical) - (size_t)u_end);
    }
    /* Append non-u extensions from canonical */
    if (ext_suffix && ext_suffix[0]) {
        dbuf_putstr(&db, ext_suffix);
    }

    dbuf_putc(&db, '\0');
    if (dbuf_error(&db))
        goto fail;
    result = js_strdup(ctx, (const char *)db.buf);

    /* Re-canonicalize the rebuilt locale (ApplyOptionsToTag step 13) */
    {
        char *canonicalized = canonicalize_locale(ctx, result);
        if (canonicalized) {
            js_free(ctx, result);
            result = canonicalized;
            /* Re-extract baseName and components from re-canonicalized result */
            {
                int new_u_start = 0, new_u_end = 0;
                locale_find_u_extension(result, &new_u_start, &new_u_end);
                if (new_u_start > 0) {
                    char *new_base = js_malloc(ctx, (size_t)new_u_start);
                    if (new_base) {
                        memcpy(new_base, result, (size_t)(new_u_start - 1));
                        new_base[new_u_start - 1] = '\0';
                        js_free(ctx, baseName);
                        baseName = new_base;
                    }
                } else {
                    char *new_base = js_strdup(ctx, result);
                    if (new_base) {
                        js_free(ctx, baseName);
                        baseName = new_base;
                    }
                }
                /* Re-parse baseName components */
                js_free(ctx, language);
                js_free(ctx, script);
                js_free(ctx, region);
                js_free(ctx, variants);
                language = NULL; script = NULL; region = NULL; variants = NULL;
                {
                    char buf2[256];
                    char *subtags2[16];
                    int n2, idx;
                    pstrcpy(buf2, sizeof(buf2), baseName);
                    n2 = locale_split_subtags(buf2, subtags2, 16);
                    if (n2 > 0) language = js_strdup(ctx, subtags2[0]);
                    idx = 1;
                    if (idx < n2 && strlen(subtags2[idx]) == 4 &&
                        isalpha((unsigned char)subtags2[idx][0]) &&
                        isalpha((unsigned char)subtags2[idx][1]) &&
                        isalpha((unsigned char)subtags2[idx][2]) &&
                        isalpha((unsigned char)subtags2[idx][3])) {
                        script = js_strdup(ctx, subtags2[idx]);
                        to_title_case(script);
                        idx++;
                    }
                    if (idx < n2 && (strlen(subtags2[idx]) == 2 || strlen(subtags2[idx]) == 3)) {
                        int is_reg = 1;
                        int j;
                        for (j = 0; j < (int)strlen(subtags2[idx]); j++) {
                            if (strlen(subtags2[idx]) == 2 && !isalpha((unsigned char)subtags2[idx][j])) is_reg = 0;
                            if (strlen(subtags2[idx]) == 3 && !isdigit((unsigned char)subtags2[idx][j])) is_reg = 0;
                        }
                        if (is_reg) {
                            region = js_strdup(ctx, subtags2[idx]);
                            to_upper_case(region);
                            idx++;
                        }
                    }
                    if (idx < n2) {
                        int j, vlen = 0;
                        qsort(&subtags2[idx], (size_t)(n2 - idx), sizeof(char *), cmp_str);
                        for (j = idx; j < n2; j++) vlen += (int)strlen(subtags2[j]) + 1;
                        variants = js_malloc(ctx, (size_t)vlen);
                        if (variants) {
                            variants[0] = '\0';
                            for (j = idx; j < n2; j++) {
                                if (j > idx) pstrcat(variants, vlen, "-");
                                pstrcat(variants, vlen, subtags2[j]);
                            }
                            to_lower_case(variants);
                        }
                    }
                }
            }
        }
    }

    /* 11. Set remaining internal slots */
    out->locale = result;
    out->baseName = baseName;
    out->language = language;
    out->script = script;
    out->region = region;
    out->variants = variants;

    /* cleanup temporaries */
    js_free(ctx, ext_suffix);
    js_free(ctx, u_attributes);
    js_free(ctx, u_ext_str);
    for (i = 0; i < num_u_keywords; i++) {
        js_free(ctx, u_keywords[i].key);
        js_free(ctx, u_keywords[i].value);
    }
    dbuf_free(&db);

    return result;

fail:
    for (i = 0; i < n_subtags; i++)
        js_free(ctx, subtags[i]);
    js_free(ctx, language);
    js_free(ctx, script);
    js_free(ctx, region);
    js_free(ctx, variants);
    js_free(ctx, baseName);
    js_free(ctx, u_attributes);
    js_free(ctx, u_ext_str);
    js_free(ctx, ext_suffix);
    for (i = 0; i < num_u_keywords; i++) {
        js_free(ctx, u_keywords[i].key);
        js_free(ctx, u_keywords[i].value);
    }
    dbuf_free(&db);
    return NULL;
}

/* --------------------------------------------------------------------- */
/* Locale object detection                                               */
/* --------------------------------------------------------------------- */

JSValue js_locale_get_locale_string(JSContext *ctx, JSValueConst val)
{
    JSLocaleData *s = JS_GetOpaque(val, js_locale_class_id);
    if (s)
        return JS_NewString(ctx, s->locale);
    return JS_UNDEFINED;
}

/* --------------------------------------------------------------------- */
/* Intl.Locale constructor                                               */
/* --------------------------------------------------------------------- */

static JSValue js_locale_constructor(JSContext *ctx, JSValueConst new_target,
                                      int argc, JSValueConst *argv)
{
    JSValue obj = JS_UNDEFINED;
    JSLocaleData *s = NULL;
    const char *tag = "und";
    JSValue options = JS_UNDEFINED;
    char *canonical = NULL;

    /* Step 8: If tag is not a String and not an Object, throw TypeError */
    if (argc == 0 || (!JS_IsString(argv[0]) && !JS_IsObject(argv[0]))) {
        JS_ThrowTypeError(ctx, "tag must be a string or object");
        return JS_EXCEPTION;
    }
    if (JS_IsObject(argv[0])) {
        /* Step 9: If tag has [[InitializedLocale]], use tag.[[Locale]] */
        JSValue loc_str = js_locale_get_locale_string(ctx, argv[0]);
        if (JS_IsException(loc_str))
            return JS_EXCEPTION;
        if (!JS_IsUndefined(loc_str)) {
            tag = JS_ToCStringNZ(ctx, loc_str);
            JS_FreeValue(ctx, loc_str);
            if (!tag)
                return JS_EXCEPTION;
        } else {
            /* Step 10: Else, let tag be ? ToString(tag) */
            tag = JS_ToCStringNZ(ctx, argv[0]);
            if (!tag)
                return JS_EXCEPTION;
        }
    } else {
        tag = JS_ToCStringNZ(ctx, argv[0]);
        if (!tag)
            return JS_EXCEPTION;
    }

    /* Step 11: Coerce options to object */
    options = js_intl_get_options_object(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
    if (JS_IsException(options))
        goto fail;

    /* Step 12: If IsWellFormedLanguageTag(tag) is false, throw RangeError */
    if (!is_wellformed_language_tag(tag)) {
        JS_ThrowRangeError(ctx, "invalid language tag");
        goto fail;
    }

    /* Step 13: CanonicalizeUnicodeLocaleId */
    canonical = canonicalize_locale(ctx, tag);
    if (!canonical)
        goto fail;

    /* Apply options: UpdateLanguageId + MakeLocaleRecord */
    s = js_mallocz(ctx, sizeof(*s));
    if (!s)
        goto fail;
    s->numeric = -1;

    {
        char *new_locale = locale_build_locale_from_options(ctx, canonical, options, s);
        JS_FreeValue(ctx, options);
        options = JS_UNDEFINED;
        if (!new_locale) {
            /* exception already thrown */
            goto fail;
        }
        js_free(ctx, canonical);
        canonical = NULL;
    }

    obj = JS_CreateFromConstructor(ctx, new_target, js_locale_class_id);
    if (JS_IsException(obj))
        goto fail;

    JS_SetOpaque(obj, s);

    JS_FreeCString(ctx, tag);
    return obj;

fail:
    JS_FreeCString(ctx, tag);
    JS_FreeValue(ctx, options);
    js_free(ctx, canonical);
    if (s) {
        js_locale_finalizer(JS_GetRuntime(ctx), JS_UNDEFINED);
        /* Note: js_locale_finalizer needs a JSValue to get opaque, but we don't have one.
         * We'll free manually. */
# define FREE_FIELD(f) if (s->f) js_free(ctx, s->f)
        FREE_FIELD(locale);
        FREE_FIELD(baseName);
        FREE_FIELD(language);
        FREE_FIELD(script);
        FREE_FIELD(region);
        FREE_FIELD(variants);
        FREE_FIELD(calendar);
        FREE_FIELD(collation);
        FREE_FIELD(hourCycle);
        FREE_FIELD(numberingSystem);
        FREE_FIELD(caseFirst);
        FREE_FIELD(firstDayOfWeek);
# undef FREE_FIELD
        js_free(ctx, s);
    }
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

/* --------------------------------------------------------------------- */
/* Intl.Locale prototype getters                                         */
/* --------------------------------------------------------------------- */

static JSValue js_locale_get_language(JSContext *ctx, JSValueConst this_val)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (s->language)
        return JS_NewString(ctx, s->language);
    return JS_UNDEFINED;
}

static JSValue js_locale_get_script(JSContext *ctx, JSValueConst this_val)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (s->script)
        return JS_NewString(ctx, s->script);
    return JS_UNDEFINED;
}

static JSValue js_locale_get_region(JSContext *ctx, JSValueConst this_val)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (s->region)
        return JS_NewString(ctx, s->region);
    return JS_UNDEFINED;
}

static JSValue js_locale_get_baseName(JSContext *ctx, JSValueConst this_val)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (s->baseName)
        return JS_NewString(ctx, s->baseName);
    return JS_UNDEFINED;
}

static JSValue js_locale_get_variants(JSContext *ctx, JSValueConst this_val)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (s->variants)
        return JS_NewString(ctx, s->variants);
    return JS_UNDEFINED;
}

static JSValue js_locale_get_calendar(JSContext *ctx, JSValueConst this_val)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (s->calendar)
        return JS_NewString(ctx, s->calendar);
    return JS_UNDEFINED;
}

static JSValue js_locale_get_collation(JSContext *ctx, JSValueConst this_val)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (s->collation)
        return JS_NewString(ctx, s->collation);
    return JS_UNDEFINED;
}

static JSValue js_locale_get_hourCycle(JSContext *ctx, JSValueConst this_val)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (s->hourCycle)
        return JS_NewString(ctx, s->hourCycle);
    return JS_UNDEFINED;
}

static JSValue js_locale_get_numberingSystem(JSContext *ctx, JSValueConst this_val)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (s->numberingSystem)
        return JS_NewString(ctx, s->numberingSystem);
    return JS_UNDEFINED;
}

static JSValue js_locale_get_caseFirst(JSContext *ctx, JSValueConst this_val)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (s->caseFirst)
        return JS_NewString(ctx, s->caseFirst);
    return JS_UNDEFINED;
}

static JSValue js_locale_get_numeric(JSContext *ctx, JSValueConst this_val)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (s->numeric >= 0)
        return JS_NewBool(ctx, s->numeric);
    return JS_UNDEFINED;
}

static JSValue js_locale_get_firstDayOfWeek(JSContext *ctx, JSValueConst this_val)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (s->firstDayOfWeek)
        return JS_NewString(ctx, s->firstDayOfWeek);
    return JS_UNDEFINED;
}

/* Split locale into base (before first extension singleton) and suffix */
static void locale_split_base_suffix(const char *locale, char *base, int base_size, char *suffix, int suffix_size)
{
    int len = (int)strlen(locale);
    int i = 0;
    int ext_pos = -1;
    base[0] = '\0';
    suffix[0] = '\0';
    while (i < len) {
        int j = i;
        while (j < len && locale[j] != '-') j++;
        int slen = j - i;
        if (slen == 1 && isalpha((unsigned char)locale[i]) && i > 0) {
            ext_pos = i - 1; /* include leading dash */
            break;
        }
        i = j + 1;
    }
    if (ext_pos >= 0) {
        pstrcpy(base, base_size, locale);
        base[ext_pos] = '\0';
        pstrcpy(suffix, suffix_size, locale + ext_pos);
    } else {
        pstrcpy(base, base_size, locale);
    }
}

/* --------------------------------------------------------------------- */
/* UTS 35 likely subtags                                                  */
/* --------------------------------------------------------------------- */

static BOOL is_script_subtag(const char *s)
{
    int i, len = (int)strlen(s);
    if (len != 4)
        return FALSE;
    for (i = 0; i < 4; i++) {
        if (!isalpha((unsigned char)s[i]))
            return FALSE;
    }
    return TRUE;
}

static BOOL is_region_subtag(const char *s)
{
    int i, len = (int)strlen(s);
    if (len == 2) {
        for (i = 0; i < 2; i++) {
            if (!isalpha((unsigned char)s[i]))
                return FALSE;
        }
        return TRUE;
    }
    if (len == 3) {
        for (i = 0; i < 3; i++) {
            if (!isdigit((unsigned char)s[i]))
                return FALSE;
        }
        return TRUE;
    }
    return FALSE;
}

static BOOL is_alphanum_len(const char *s, int min_len, int max_len)
{
    int i, len = (int)strlen(s);
    if (len < min_len || len > max_len)
        return FALSE;
    for (i = 0; i < len; i++) {
        if (!isalnum((unsigned char)s[i]))
            return FALSE;
    }
    return TRUE;
}

/* Look up a key in the likely subtags data. Returns the associated
   language-script-region value, or NULL if the key is unknown. */
static const char *likely_subtags_lookup(const char *key)
{
    int lo = 0, hi = likely_subtags_count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        const char *entry = likely_subtags_tab[mid];
        int c = strcmp(entry, key);
        if (c == 0)
            return entry + strlen(entry) + 1;
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

/* Add Likely Subtags (UTS 35). Store the maximized form of the language,
   script and region subtags in buf. The subtags which are present are kept,
   the missing ones are taken from the matched entry. Returns 0 on success,
   or -1 if no data matches (an error is signaled by the caller). */
static int locale_add_likely_subtags(const char *lang, const char *script,
                                     const char *region, char *buf, int buf_size)
{
    char key[64], value[64], *vsubtags[4];
    const char *found = NULL;
    const char *prefix, *sc, *rg;
    BOOL has_lang = lang && *lang && strcmp(lang, "und") != 0;
    BOOL has_script = script && *script && strcmp(script, "Zzzz") != 0;
    BOOL has_region = region && *region && strcmp(region, "ZZ") != 0;
    int n;

    prefix = has_lang ? lang : "und";
    if (has_lang && has_script && has_region) {
        /* already in maximal form */
        snprintf(buf, buf_size, "%s-%s-%s", lang, script, region);
        return 0;
    }
    /* Try the keys built from the subtags which are present, longest first */
    if (!has_lang && has_script && has_region) {
        snprintf(key, sizeof(key), "und-%s-%s", script, region);
        found = likely_subtags_lookup(key);
    }
    if (!found && has_script) {
        snprintf(key, sizeof(key), "%s-%s", prefix, script);
        found = likely_subtags_lookup(key);
    }
    if (!found && has_region) {
        snprintf(key, sizeof(key), "%s-%s", prefix, region);
        found = likely_subtags_lookup(key);
    }
    if (!found) {
        snprintf(key, sizeof(key), "%s", prefix);
        found = likely_subtags_lookup(key);
    }
    if (!found)
        return -1;
    pstrcpy(value, sizeof(value), found);
    n = locale_split_subtags(value, vsubtags, 4);
    if (n != 3)
        return -1;
    sc = has_script ? script : vsubtags[1];
    rg = has_region ? region : vsubtags[2];
    snprintf(buf, buf_size, "%s", has_lang ? lang : vsubtags[0]);
    if (*sc) {
        pstrcat(buf, buf_size, "-");
        pstrcat(buf, buf_size, sc);
    }
    if (*rg) {
        pstrcat(buf, buf_size, "-");
        pstrcat(buf, buf_size, rg);
    }
    return 0;
}

/* A locale base name split into its language, script, region and variant
   subtags. The subtag pointers point into storage. */
typedef struct {
    char storage[256];
    char *subtags[16];
    int count;
    const char *lang;
    const char *script;
    const char *region;
    int first_variant;
} LocaleNameParts;

static int locale_parse_name(const char *name, LocaleNameParts *p)
{
    memset(p, 0, sizeof(*p));
    pstrcpy(p->storage, sizeof(p->storage), name);
    p->count = locale_split_subtags(p->storage, p->subtags, 16);
    if (p->count == 0)
        return -1;
    p->lang = p->subtags[0];
    p->first_variant = 1;
    if (p->count > 1 && is_script_subtag(p->subtags[1])) {
        p->script = p->subtags[1];
        p->first_variant = 2;
    }
    if (p->count > p->first_variant &&
        is_region_subtag(p->subtags[p->first_variant])) {
        p->region = p->subtags[p->first_variant];
        p->first_variant++;
    }
    return 0;
}

/* Apply Add Likely Subtags to lang/script/region and append the variants of
   name. Returns 0 on success, or -1 if an error is signaled. */
static int locale_maximize_parts(const LocaleNameParts *name, const char *lang,
                                 const char *script, const char *region,
                                 char *out, int out_size)
{
    char res[256];
    int i;
    if (locale_add_likely_subtags(lang, script, region, res, sizeof(res)) < 0)
        return -1;
    pstrcpy(out, out_size, res);
    for (i = name->first_variant; i < name->count; i++) {
        pstrcat(out, out_size, "-");
        pstrcat(out, out_size, name->subtags[i]);
    }
    return 0;
}

/* Build a locale from the given subtags, appending the variants of name. */
static void locale_join_parts(const LocaleNameParts *name, const char *lang,
                              const char *script, const char *region,
                              char *out, int out_size)
{
    int i;
    pstrcpy(out, out_size, lang);
    if (script && *script) {
        pstrcat(out, out_size, "-");
        pstrcat(out, out_size, script);
    }
    if (region && *region) {
        pstrcat(out, out_size, "-");
        pstrcat(out, out_size, region);
    }
    for (i = name->first_variant; i < name->count; i++) {
        pstrcat(out, out_size, "-");
        pstrcat(out, out_size, name->subtags[i]);
    }
}

/* Maximize a locale base name. Returns 0 and stores the result in out, or
   -1 if the Add Likely Subtags algorithm signaled an error. */
static int locale_maximize_to(const char *base, char *out, int out_size)
{
    LocaleNameParts name;
    if (locale_parse_name(base, &name) < 0)
        return -1;
    return locale_maximize_parts(&name, name.lang, name.script, name.region,
                                 out, out_size);
}

static char *locale_maximize_base(JSContext *ctx, const char *base)
{
    char res[256];
    if (locale_maximize_to(base, res, sizeof(res)) == 0)
        return js_strdup(ctx, res);
    return js_strdup(ctx, base);
}

static JSValue js_locale_maximize(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    const char *locale;
    char base[256], suffix[256];
    char *new_base = NULL;
    DynBuf db;
    if (!s)
        return JS_EXCEPTION;
    locale = s->locale;
    locale_split_base_suffix(locale, base, sizeof(base), suffix, sizeof(suffix));
    new_base = locale_maximize_base(ctx, base);
    if (new_base) {
        JSValue ctor, args[1], result;
        js_intl_dbuf_init(ctx, &db);
        dbuf_putstr(&db, new_base);
        if (suffix[0]) dbuf_putstr(&db, suffix);
        dbuf_putc(&db, '\0');
        if (dbuf_error(&db)) {
            dbuf_free(&db);
            js_free(ctx, new_base);
            return JS_EXCEPTION;
        }
        ctor = JS_GetContextValue(ctx, JS_INTL_CTOR_LOCALE);
        args[0] = JS_NewString(ctx, (const char *)db.buf);
        result = JS_CallConstructor(ctx, ctor, 1, args);
        JS_FreeValue(ctx, args[0]);
        JS_FreeValue(ctx, ctor);
        js_free(ctx, new_base);
        dbuf_free(&db);
        return result;
    }
    return JS_DupValue(ctx, this_val);
}

static char *locale_minimize_base(JSContext *ctx, const char *base)
{
    LocaleNameParts name, maximal;
    char max[256], test[256], res[256];

    if (locale_parse_name(base, &name) < 0)
        return js_strdup(ctx, base);
    if (locale_maximize_parts(&name, name.lang, name.script, name.region,
                              max, sizeof(max)) < 0)
        return js_strdup(ctx, base);
    if (locale_parse_name(max, &maximal) < 0)
        return js_strdup(ctx, max);
    /* Remove Likely Subtags: try to remove the script and the region first,
       then the script only, then the region only. A subtag is removed only
       if the result still maximizes back to the maximal form. */
    if (locale_maximize_parts(&maximal, maximal.lang, NULL, NULL,
                              test, sizeof(test)) == 0 &&
        !strcmp(test, max)) {
        locale_join_parts(&maximal, maximal.lang, NULL, NULL, res, sizeof(res));
        return js_strdup(ctx, res);
    }
    if (maximal.region &&
        locale_maximize_parts(&maximal, maximal.lang, NULL, maximal.region,
                              test, sizeof(test)) == 0 &&
        !strcmp(test, max)) {
        locale_join_parts(&maximal, maximal.lang, NULL, maximal.region,
                          res, sizeof(res));
        return js_strdup(ctx, res);
    }
    if (maximal.script &&
        locale_maximize_parts(&maximal, maximal.lang, maximal.script, NULL,
                              test, sizeof(test)) == 0 &&
        !strcmp(test, max)) {
        locale_join_parts(&maximal, maximal.lang, maximal.script, NULL,
                          res, sizeof(res));
        return js_strdup(ctx, res);
    }
    return js_strdup(ctx, max);
}

static JSValue js_locale_minimize(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    const char *locale;
    char base[256], suffix[256];
    char *new_base = NULL;
    DynBuf db;
    if (!s)
        return JS_EXCEPTION;
    locale = s->locale;
    locale_split_base_suffix(locale, base, sizeof(base), suffix, sizeof(suffix));
    new_base = locale_minimize_base(ctx, base);
    if (new_base) {
        JSValue ctor, args[1], result;
        js_intl_dbuf_init(ctx, &db);
        dbuf_putstr(&db, new_base);
        if (suffix[0]) dbuf_putstr(&db, suffix);
        dbuf_putc(&db, '\0');
        if (dbuf_error(&db)) {
            dbuf_free(&db);
            js_free(ctx, new_base);
            return JS_EXCEPTION;
        }
        ctor = JS_GetContextValue(ctx, JS_INTL_CTOR_LOCALE);
        args[0] = JS_NewString(ctx, (const char *)db.buf);
        result = JS_CallConstructor(ctx, ctor, 1, args);
        JS_FreeValue(ctx, args[0]);
        JS_FreeValue(ctx, ctor);
        js_free(ctx, new_base);
        dbuf_free(&db);
        return result;
    }
    return JS_DupValue(ctx, this_val);
}

/* --------------------------------------------------------------------- */
/* Intl.Locale info methods (ECMA-402)                                    */
/* --------------------------------------------------------------------- */

/* Create an Array holding the n first strings of items. */
static JSValue locale_new_string_array(JSContext *ctx, const char *const *items, int n)
{
    JSValue arr = JS_NewArray(ctx);
    int i;
    if (JS_IsException(arr))
        return arr;
    for (i = 0; i < n; i++) {
        if (JS_DefinePropertyValueUint32(ctx, arr, (uint32_t)i,
                                          JS_NewString(ctx, items[i]),
                                          JS_PROP_C_W_E) < 0) {
            JS_FreeValue(ctx, arr);
            return JS_EXCEPTION;
        }
    }
    return arr;
}

/* Split a space separated list, modifying buf. */
static int locale_split_list(char *buf, char **items, int max_items)
{
    int n = 0;
    char *p = buf;
    while (*p && n < max_items) {
        char *start = p;
        while (*p && *p != ' ')
            p++;
        items[n++] = start;
        if (*p == ' ') {
            *p = '\0';
            p++;
        }
    }
    return n;
}

static int cmp_string(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Find the entry matching key in a table sorted by key. */
static const IntlPrefData *locale_find_pref(const IntlPrefData *tab, int count,
                                            const char *key)
{
    int lo = 0, hi = count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int c = strcmp(tab[mid].key, key);
        if (c == 0)
            return &tab[mid];
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

static const IntlWeekData *locale_find_week(const char *region)
{
    int lo = 0, hi = week_data_count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int c = strcmp(week_data_tab[mid].region, region);
        if (c == 0)
            return &week_data_tab[mid];
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

/* LookupMatchingLocaleByPrefix: return the locale data matching the
   unicode_language_id of locale, or NULL if there is no match. */
static const LocaleData *locale_match_data(const char *locale)
{
    char base[256], suffix[256];
    int i;
    locale_split_base_suffix(locale, base, sizeof(base), suffix, sizeof(suffix));
    for (i = 0; i < locale_data_count; i++) {
        if (!strcmp(locale_data_table[i].locale, base))
            return &locale_data_table[i];
    }
    for (i = 0; i < locale_data_count; i++) {
        const char *p = locale_data_table[i].locale;
        int len = (int)strlen(p);
        if (!strncmp(base, p, (size_t)len) && (base[len] == '\0' || base[len] == '-'))
            return &locale_data_table[i];
    }
    return NULL;
}

/* CanonicalUnicodeSubdivision (ECMA-402): return the region carried by the
   "sd" or "rg" keyword of the locale, or NULL. */
static char *locale_canonical_subdivision(JSContext *ctx, const char *locale,
                                          const char *key)
{
    char *value = js_intl_locale_get_unicode_key(ctx, locale, key);
    char buf[64], *parts[3], rbuf[8];
    int n, i, len;

    if (!value)
        return NULL;
    pstrcpy(buf, sizeof(buf), value);
    js_free(ctx, value);
    /* unicode_subdivision_id = [unicode_region_subtag "-"] unicode_subdivision_subtag */
    n = locale_split_subtags(buf, parts, 3);
    rbuf[0] = '\0';
    if (n == 2) {
        if (is_region_subtag(parts[0]) && is_alphanum_len(parts[1], 4, 8))
            pstrcpy(rbuf, sizeof(rbuf), parts[0]);
    } else if (n == 1 && is_alphanum_len(parts[0], 4, 8)) {
        /* the region is the longest prefix matching unicode_region_subtag */
        len = (int)strlen(parts[0]);
        for (i = 3; i >= 2; i--) {
            if (len < i)
                continue;
            pstrcpy(rbuf, sizeof(rbuf), parts[0]);
            rbuf[i] = '\0';
            if (is_region_subtag(rbuf))
                break;
            rbuf[0] = '\0';
        }
    }
    if (!rbuf[0])
        return NULL;
    for (i = 0; rbuf[i]; i++)
        rbuf[i] = toupper((unsigned char)rbuf[i]);
    return js_strdup(ctx, rbuf);
}

/* RegionPreference (ECMA-402): compute the region used to look up the
   calendar, time and week preference data, and the "rg" region override.
   The override must be freed by the caller. */
static void locale_region_preference(JSContext *ctx, JSLocaleData *s,
                                     char *region, int region_size, char **override)
{
    char base[256], suffix[256], max[256];
    char *sd;
    LocaleNameParts name;

    region[0] = '\0';
    *override = locale_canonical_subdivision(ctx, s->locale, "rg");
    if (s->region)
        pstrcpy(region, region_size, s->region);
    if (!region[0]) {
        sd = locale_canonical_subdivision(ctx, s->locale, "sd");
        if (sd) {
            pstrcpy(region, region_size, sd);
            js_free(ctx, sd);
        }
    }
    if (!region[0]) {
        locale_split_base_suffix(s->locale, base, sizeof(base), suffix, sizeof(suffix));
        if (locale_maximize_to(base, max, sizeof(max)) == 0 &&
            locale_parse_name(max, &name) == 0 && name.region)
            pstrcpy(region, region_size, name.region);
    }
    if (!region[0])
        pstrcpy(region, region_size, "001");
}

static BOOL calendar_is_available(const char *name)
{
    int i;
    for (i = 0; i < CAL_COUNT; i++) {
        const char *c = ti_calendar_enum_to_string(i);
        if (c && !strcmp(c, name))
            return TRUE;
    }
    return FALSE;
}

/* WeekdayUValueToNumber (ECMA-402): convert a Unicode First Day Identifier
   to an ISO 8601 day of week, or -1. */
static int weekday_u_value_to_number(const char *s)
{
    static const char *const names[7] = {
        "mon", "tue", "wed", "thu", "fri", "sat", "sun"
    };
    int i;
    for (i = 0; i < 7; i++) {
        if (!strcmp(s, names[i]))
            return i + 1;
    }
    return -1;
}

static JSValue js_locale_getCalendars(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    const char *language, *items[16];
    const IntlPrefData *entry = NULL;
    const char *regions[2];
    char region[16], *override, key[64], list[128], id[32];
    char *parts[16], ids[16][32];
    int i, j, n = 0, nregions, nparts;

    if (!s)
        return JS_EXCEPTION;
    if (s->calendar) {
        items[0] = s->calendar;
        return locale_new_string_array(ctx, items, 1);
    }
    locale_region_preference(ctx, s, region, sizeof(region), &override);
    language = s->language ? s->language : "und";
    nregions = 0;
    if (override && locale_find_pref(cal_pref_tab, cal_pref_count, override))
        regions[nregions++] = override;
    regions[nregions++] = region;
    for (i = 0; i < nregions && !entry; i++) {
        snprintf(key, sizeof(key), "%s-%s", language, regions[i]);
        entry = locale_find_pref(cal_pref_tab, cal_pref_count, key);
        if (!entry)
            entry = locale_find_pref(cal_pref_tab, cal_pref_count, regions[i]);
    }
    js_free(ctx, override);
    if (entry) {
        pstrcpy(list, sizeof(list), entry->value);
        nparts = locale_split_list(list, parts, 16);
        for (i = 0; i < nparts; i++) {
            const char *canon;
            pstrcpy(id, sizeof(id), parts[i]);
            to_lower_case(id);
            canon = canonicalize_keyword_value("ca", id);
            if (!calendar_is_available(canon))
                continue;
            for (j = 0; j < n; j++) {
                if (!strcmp(items[j], canon))
                    break;
            }
            if (j == n && n < 16) {
                pstrcpy(ids[n], sizeof(ids[0]), canon);
                items[n] = ids[n];
                n++;
            }
        }
    }
    if (n == 0)
        items[n++] = "gregory";
    return locale_new_string_array(ctx, items, n);
}

/* The canonical zone identifiers commonly used in region, sorted. */
static JSValue locale_region_time_zones(JSContext *ctx, const char *region)
{
    JSValue arr;
    const char *p;
    int lo = 0, hi = ti_tz_region_count - 1, idx = -1, n = 0;

    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int c = strcmp(ti_tz_region_tab[mid].region, region);
        if (c == 0) {
            idx = mid;
            break;
        }
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    arr = JS_NewArray(ctx);
    if (JS_IsException(arr))
        return arr;
    if (idx >= 0) {
        for (p = ti_tz_region_tab[idx].zones; *p; ) {
            const char *end = p + strlen(p);
            if (JS_DefinePropertyValueUint32(ctx, arr, (uint32_t)n,
                                             JS_NewStringLen(ctx, p, end - p),
                                             JS_PROP_C_W_E) < 0) {
                JS_FreeValue(ctx, arr);
                return JS_EXCEPTION;
            }
            n++;
            if (!*end)
                break;
            p = end + 1;
        }
    }
    return arr;
}

static JSValue js_locale_getTimeZones(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    if (!s)
        return JS_EXCEPTION;
    /* TimeZonesOfLocale returns undefined if the locale has no region subtag */
    if (!s->region)
        return JS_UNDEFINED;
    return locale_region_time_zones(ctx, s->region);
}

static JSValue js_locale_getTextInfo(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    const char *script;
    char base[256], suffix[256], max[256];
    LocaleNameParts name;
    JSValue obj;
    int i;

    if (!s)
        return JS_EXCEPTION;
    script = s->script;
    obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        return JS_EXCEPTION;
    if (!script) {
        /* the script comes from the Add Likely Subtags algorithm */
        locale_split_base_suffix(s->locale, base, sizeof(base), suffix, sizeof(suffix));
        if (locale_maximize_to(base, max, sizeof(max)) == 0 &&
            locale_parse_name(max, &name) == 0)
            script = name.script;
    }
    for (i = 0; script && i < script_dir_count; i++) {
        if (!strcmp(script_dir_tab[i].script, script)) {
            JS_DefinePropertyValueStr(ctx, obj, "direction",
                                      JS_NewString(ctx, script_dir_tab[i].dir ? "rtl" : "ltr"),
                                      JS_PROP_C_W_E);
            return obj;
        }
    }
    /* the direction of an unknown script cannot be determined */
    JS_DefinePropertyValueStr(ctx, obj, "direction", JS_UNDEFINED, JS_PROP_C_W_E);
    return obj;
}

static JSValue js_locale_getCollations(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    const LocaleData *ld;
    const char *items[8];
    int i, n = 0;

    if (!s)
        return JS_EXCEPTION;
    if (s->collation) {
        items[0] = s->collation;
        return locale_new_string_array(ctx, items, 1);
    }
    /* [[SortLocaleData]].[[co]] starts with the default collation, which is
       null and not part of the returned list, followed by the collations
       available for the locale. The root collations are inherited by every
       locale; if the locale does not match any locale data, only the root
       collations are returned. */
    ld = locale_match_data(s->locale);
    items[n++] = "emoji";
    items[n++] = "eor";
    for (i = 0; ld && i < intl_supported_collations_count; i++) {
        const char *c = intl_supported_collations[i];
        int j;
        if (!js_intl_collation_is_supported(c, ld->locale))
            continue;
        for (j = 0; j < n; j++) {
            if (!strcmp(items[j], c))
                break;
        }
        if (j == n && n < 8)
            items[n++] = c;
    }
    qsort(items, n, sizeof(items[0]), cmp_string);
    return locale_new_string_array(ctx, items, n);
}

static JSValue js_locale_getHourCycles(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    const IntlPrefData *entry = NULL;
    const char *language, *regions[2], *items[8];
    char region[16], *override, key[64], list[64];
    char *parts[8];
    int i, n = 0, nregions, nparts;

    if (!s)
        return JS_EXCEPTION;
    if (s->hourCycle) {
        items[0] = s->hourCycle;
        return locale_new_string_array(ctx, items, 1);
    }
    locale_region_preference(ctx, s, region, sizeof(region), &override);
    language = s->language ? s->language : "und";
    nregions = 0;
    if (override && locale_find_pref(time_data_tab, time_data_count, override))
        regions[nregions++] = override;
    regions[nregions++] = region;
    for (i = 0; i < nregions && !entry; i++) {
        snprintf(key, sizeof(key), "%s-%s", language, regions[i]);
        entry = locale_find_pref(time_data_tab, time_data_count, key);
        if (!entry)
            entry = locale_find_pref(time_data_tab, time_data_count, regions[i]);
    }
    js_free(ctx, override);
    if (!entry) {
        items[n++] = "h23";
    } else {
        pstrcpy(list, sizeof(list), entry->value);
        nparts = locale_split_list(list, parts, 8);
        for (i = 0; i < nparts && n < 8; i++)
            items[n++] = parts[i];
    }
    return locale_new_string_array(ctx, items, n);
}

static JSValue js_locale_getNumberingSystems(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    const char *items[1];

    if (!s)
        return JS_EXCEPTION;
    if (s->numberingSystem) {
        items[0] = s->numberingSystem;
        return locale_new_string_array(ctx, items, 1);
    }
    /* The first numbering system ([[nu]][0]) of the matched NumberFormat
       locale data, or "latn" if there is no match. Our number formatter uses
       "latn" as the default numbering system of every locale. */
    items[0] = "latn";
    return locale_new_string_array(ctx, items, 1);
}

static JSValue js_locale_getWeekInfo(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    const IntlWeekData *wd = NULL;
    char region[16], *override;
    int firstDay, weekendStart, weekendEnd, days[7], ndays;
    int i;
    JSValue obj, weekend;

    if (!s)
        return JS_EXCEPTION;
    locale_region_preference(ctx, s, region, sizeof(region), &override);
    if (override)
        wd = locale_find_week(override);
    if (!wd)
        wd = locale_find_week(region);
    if (!wd)
        wd = locale_find_week("001");
    js_free(ctx, override);
    firstDay = wd ? wd->firstDay : 1;
    weekendStart = wd ? wd->weekendStart : 6;
    weekendEnd = wd ? wd->weekendEnd : 7;
    if (s->firstDayOfWeek) {
        int d = weekday_u_value_to_number(s->firstDayOfWeek);
        if (d > 0)
            firstDay = d;
    }
    obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        return JS_EXCEPTION;
    JS_DefinePropertyValueStr(ctx, obj, "firstDay", JS_NewInt32(ctx, firstDay),
                              JS_PROP_C_W_E);
    /* the weekend runs from weekendStart to weekendEnd, wrapping around */
    ndays = 0;
    for (;;) {
        days[ndays++] = weekendStart;
        if (weekendStart == weekendEnd)
            break;
        weekendStart = weekendStart % 7 + 1;
    }
    /* sorted in ascending numeric order */
    for (i = 0; i < ndays; i++) {
        int j, v = days[i];
        for (j = i; j > 0 && days[j - 1] > v; j--)
            days[j] = days[j - 1];
        days[j] = v;
    }
    weekend = JS_NewArray(ctx);
    if (JS_IsException(weekend)) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    for (i = 0; i < ndays; i++) {
        JS_DefinePropertyValueUint32(ctx, weekend, (uint32_t)i,
                                     JS_NewInt32(ctx, days[i]), JS_PROP_C_W_E);
    }
    JS_DefinePropertyValueStr(ctx, obj, "weekend", weekend, JS_PROP_C_W_E);
    return obj;
}

static JSValue js_locale_toString(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewString(ctx, s->locale);
}

static const JSCFunctionListEntry js_locale_proto_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Intl.Locale", JS_PROP_CONFIGURABLE),
    JS_CGETSET_DEF("language", js_locale_get_language, NULL),
    JS_CGETSET_DEF("script", js_locale_get_script, NULL),
    JS_CGETSET_DEF("region", js_locale_get_region, NULL),
    JS_CGETSET_DEF("baseName", js_locale_get_baseName, NULL),
    JS_CGETSET_DEF("variants", js_locale_get_variants, NULL),
    JS_CGETSET_DEF("calendar", js_locale_get_calendar, NULL),
    JS_CGETSET_DEF("collation", js_locale_get_collation, NULL),
    JS_CGETSET_DEF("hourCycle", js_locale_get_hourCycle, NULL),
    JS_CGETSET_DEF("numberingSystem", js_locale_get_numberingSystem, NULL),
    JS_CGETSET_DEF("caseFirst", js_locale_get_caseFirst, NULL),
    JS_CGETSET_DEF("numeric", js_locale_get_numeric, NULL),
    JS_CGETSET_DEF("firstDayOfWeek", js_locale_get_firstDayOfWeek, NULL),
    JS_CFUNC_DEF("getCalendars", 0, js_locale_getCalendars),
    JS_CFUNC_DEF("getTimeZones", 0, js_locale_getTimeZones),
    JS_CFUNC_DEF("getTextInfo", 0, js_locale_getTextInfo),
    JS_CFUNC_DEF("getWeekInfo", 0, js_locale_getWeekInfo),
    JS_CFUNC_DEF("getCollations", 0, js_locale_getCollations),
    JS_CFUNC_DEF("getHourCycles", 0, js_locale_getHourCycles),
    JS_CFUNC_DEF("getNumberingSystems", 0, js_locale_getNumberingSystems),
    JS_CFUNC_DEF("maximize", 0, js_locale_maximize),
    JS_CFUNC_DEF("minimize", 0, js_locale_minimize),
    JS_CFUNC_DEF("toString", 0, js_locale_toString),
};

/* --------------------------------------------------------------------- */
/* Intl.Locale properties                                                */
/* --------------------------------------------------------------------- */

int js_add_locale(JSContext *ctx, JSValue intl_obj)
{
    JS_NewClassID(&js_locale_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_locale_class_id, &js_locale_class);

    if (JS_IsException(js_intl_new_constructor(ctx, intl_obj,
                                                JS_INTL_CTOR_LOCALE,
                                                js_locale_class_id, "Locale",
                                                (JSCFunction *)js_locale_constructor, 1,
                                                JS_CFUNC_constructor, 0,
                                                NULL, 0,
                                                js_locale_proto_funcs, countof(js_locale_proto_funcs))))
        return -1;

    return 0;
}

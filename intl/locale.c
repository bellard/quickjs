/*
 * QuickJS Intl.Locale implementation (ECMA-402)
 */

#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "../quickjs.h"
#include "../cutils.h"
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
    }
    return value;
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
        if (slen == 2 && isalpha((unsigned char)u_ext[i]) &&
            (isalpha((unsigned char)u_ext[i+1]) || isdigit((unsigned char)u_ext[i+1]))) {
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
                const char *cal_val = canonicalize_keyword_value("ca", str);
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
                out->calendar = js_strdup(ctx, cal_val);
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
            for (i = 0; i < num_u_keywords; i++) {
                if (!strcmp(u_keywords[i].key, "co")) {
                    js_free(ctx, u_keywords[i].value);
                    u_keywords[i].value = js_strdup(ctx, str);
                    break;
                }
            }
            if (i == num_u_keywords && num_u_keywords < 16) {
                u_keywords[num_u_keywords].key = js_strdup(ctx, "co");
                u_keywords[num_u_keywords].value = js_strdup(ctx, str);
                num_u_keywords++;
            }
            out->collation = js_strdup(ctx, str);
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
                if (!fw_val) {
                    /* not a weekday name, validate as type */
                    if (!is_valid_type(str)) {
                        JS_FreeCString(ctx, str);
                        JS_ThrowRangeError(ctx, "invalid firstDayOfWeek");
                        goto fail;
                    }
                    fw_val = str;
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
                out->firstDayOfWeek = js_strdup(ctx, fw_val);
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
            for (i = 0; i < num_u_keywords; i++) {
                if (!strcmp(u_keywords[i].key, "nu")) {
                    js_free(ctx, u_keywords[i].value);
                    u_keywords[i].value = js_strdup(ctx, str);
                    break;
                }
            }
            if (i == num_u_keywords && num_u_keywords < 16) {
                u_keywords[num_u_keywords].key = js_strdup(ctx, "nu");
                u_keywords[num_u_keywords].value = js_strdup(ctx, str);
                num_u_keywords++;
            }
            out->numberingSystem = js_strdup(ctx, str);
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

static JSValue js_locale_getCalendars(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    if (!s)
        return JS_EXCEPTION;
    {
        JSValue arr = JS_NewArray(ctx);
        if (JS_IsException(arr))
            return JS_EXCEPTION;
        JS_DefinePropertyValueUint32(ctx, arr, 0, JS_NewString(ctx, "gregory"),
                                     JS_PROP_C_W_E);
        return arr;
    }
}

static JSValue js_locale_getTimeZones(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (!s->region)
        return JS_UNDEFINED;
    {
        JSValue arr = JS_NewArray(ctx);
        if (JS_IsException(arr))
            return JS_EXCEPTION;
        JS_DefinePropertyValueUint32(ctx, arr, 0, JS_NewString(ctx, "UTC"),
                                     JS_PROP_C_W_E);
        return arr;
    }
}

static int weekday_name_to_number(const char *s)
{
    if (!strcmp(s, "mon") || !strcmp(s, "1")) return 1;
    if (!strcmp(s, "tue") || !strcmp(s, "2")) return 2;
    if (!strcmp(s, "wed") || !strcmp(s, "3")) return 3;
    if (!strcmp(s, "thu") || !strcmp(s, "4")) return 4;
    if (!strcmp(s, "fri") || !strcmp(s, "5")) return 5;
    if (!strcmp(s, "sat") || !strcmp(s, "6")) return 6;
    if (!strcmp(s, "sun") || !strcmp(s, "7") || !strcmp(s, "0")) return 7;
    return 7;
}

static JSValue js_locale_getTextInfo(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    if (!s)
        return JS_EXCEPTION;
    {
        JSValue obj = JS_NewObject(ctx);
        if (JS_IsException(obj))
            return JS_EXCEPTION;
        {
            const char *dir = "ltr";
            if (s->language && (!strcmp(s->language, "ar") ||
                                !strcmp(s->language, "he") ||
                                !strcmp(s->language, "fa") ||
                                !strcmp(s->language, "ur")))
                dir = "rtl";
            JS_DefinePropertyValueStr(ctx, obj, "direction",
                                       JS_NewString(ctx, dir), JS_PROP_C_W_E);
        }
        return obj;
    }
}

static JSValue js_locale_getCollations(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    if (!JS_GetOpaque2(ctx, this_val, js_locale_class_id))
        return JS_EXCEPTION;
    {
        JSValue arr = JS_NewArray(ctx);
        if (JS_IsException(arr))
            return JS_EXCEPTION;
        JS_DefinePropertyValueUint32(ctx, arr, 0, JS_NewString(ctx, "default"),
                                     JS_PROP_C_W_E);
        return arr;
    }
}

static JSValue js_locale_getHourCycles(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    if (!JS_GetOpaque2(ctx, this_val, js_locale_class_id))
        return JS_EXCEPTION;
    {
        JSValue arr = JS_NewArray(ctx);
        if (JS_IsException(arr))
            return JS_EXCEPTION;
        JS_DefinePropertyValueUint32(ctx, arr, 0, JS_NewString(ctx, "h12"),
                                     JS_PROP_C_W_E);
        return arr;
    }
}

static JSValue js_locale_getNumberingSystems(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv)
{
    if (!JS_GetOpaque2(ctx, this_val, js_locale_class_id))
        return JS_EXCEPTION;
    {
        JSValue arr = JS_NewArray(ctx);
        if (JS_IsException(arr))
            return JS_EXCEPTION;
        JS_DefinePropertyValueUint32(ctx, arr, 0, JS_NewString(ctx, "latn"),
                                     JS_PROP_C_W_E);
        return arr;
    }
}

static JSValue js_locale_getWeekInfo(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    JSLocaleData *s = JS_GetOpaque2(ctx, this_val, js_locale_class_id);
    if (!s)
        return JS_EXCEPTION;
    {
        JSValue obj = JS_NewObject(ctx);
        int firstDay = 7;
        if (JS_IsException(obj))
            return JS_EXCEPTION;
        if (s->firstDayOfWeek)
            firstDay = weekday_name_to_number(s->firstDayOfWeek);
        JS_DefinePropertyValueStr(ctx, obj, "firstDay",
                                   JS_NewInt32(ctx, firstDay), JS_PROP_C_W_E);
        {
            JSValue weekend = JS_NewArray(ctx);
            if (JS_IsException(weekend))
                return JS_EXCEPTION;
            JS_DefinePropertyValueUint32(ctx, weekend, 0, JS_NewInt32(ctx, 6),
                                         JS_PROP_C_W_E);
            JS_DefinePropertyValueUint32(ctx, weekend, 1, JS_NewInt32(ctx, 7),
                                         JS_PROP_C_W_E);
            JS_DefinePropertyValueStr(ctx, obj, "weekend", weekend, JS_PROP_C_W_E);
        }
        return obj;
    }
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

/* Hard-coded likely subtags for common locales */
static const char *locale_get_likely_subtags(const char *lang, const char *script, const char *region)
{
    if (!lang[0]) return NULL;
    if (!strcmp(lang, "und")) {
        if (script && !strcmp(script, "Thai")) return "th-Thai-TH";
        if (region && !strcmp(region, "419")) return "es-Latn-419";
        if (region && !strcmp(region, "150")) return "en-Latn-150";
        if (region && !strcmp(region, "AT")) return "de-Latn-AT";
        if (region && !strcmp(region, "AQ")) return "en-Latn-AQ";
        if (script && !strcmp(script, "Cyrl") && region && !strcmp(region, "RO")) return "bg-Cyrl-RO";
        if (region && !strcmp(region, "CW")) return "pap";
        return "en-Latn-US";
    }
    if (!strcmp(lang, "en")) {
        if (script && !strcmp(script, "Shaw")) return "en-Shaw-GB";
        if (script && !strcmp(script, "Arab")) return "en-Arab-US";
        if (!script && region && !strcmp(region, "150")) return "en-Latn-150";
        if (script && !strcmp(script, "Latn") && region && !strcmp(region, "150")) return "en-Latn-150";
        if (!script && region && !strcmp(region, "AQ")) return "en-Latn-AQ";
        if (script && !strcmp(script, "Latn") && region && !strcmp(region, "AQ")) return "en-Latn-AQ";
        if (region && !strcmp(region, "GB")) return "en-Latn-GB";
        if (region && !strcmp(region, "FR")) return "en-Latn-FR";
        if (region && !strcmp(region, "US")) return "en-Latn-US";
        if (script && !strcmp(script, "Latn")) return "en-Latn-US";
        return "en-Latn-US";
    }
    if (!strcmp(lang, "sr")) return "sr-Cyrl-RS";
    if (!strcmp(lang, "zh")) {
        if (script && !strcmp(script, "Hant")) return "zh-Hant-TW";
        if (!script && region && !strcmp(region, "TW")) return "zh-Hant-TW";
        if (!script && region && !strcmp(region, "CN")) return "zh-Hans-CN";
        if (script && !strcmp(script, "Hans") && !region) return "zh-Hans-CN";
        if (!script && !region) return "zh-Hans-CN";
    }
    if (!strcmp(lang, "ja")) return "ja-Jpan-JP";
    if (!strcmp(lang, "jbo")) {
        if (!script && !region) return "jbo-Latn-001";
        if (script && !strcmp(script, "Latn") && !region) return "jbo-Latn-001";
        if (!script && region && !strcmp(region, "001")) return "jbo-Latn-001";
    }
    if (!strcmp(lang, "ro")) return "ro-Latn-RO";
    if (!strcmp(lang, "de")) {
        if (region && !strcmp(region, "AT")) return "de-Latn-AT";
        return "de-Latn-DE";
    }
    if (!strcmp(lang, "it")) {
        if (script && !strcmp(script, "Kana") && region && !strcmp(region, "CA")) return "it-Kana-CA";
    }
    if (!strcmp(lang, "ar")) {
        if (!script && !region) return "ar-Arab-EG";
        if (script && !strcmp(script, "Arab") && !region) return "ar-Arab-EG";
        if (!script && region && !strcmp(region, "EG")) return "ar-Arab-EG";
    }
    if (!strcmp(lang, "th")) {
        if (!script && !region) return "th-Thai-TH";
        if (!script && region && !strcmp(region, "TH")) return "th-Thai-TH";
    }
    if (!strcmp(lang, "es")) {
        if (!script && !region) return "es-Latn-ES";
        if (!script && region && !strcmp(region, "ES")) return "es-Latn-ES";
        if (!script && region && !strcmp(region, "419")) return "es-Latn-419";
        if (script && !strcmp(script, "Latn") && region && !strcmp(region, "419")) return "es-Latn-419";
    }
    if (!strcmp(lang, "fr")) {
        if (!script && !region) return "fr-Latn-FR";
        if (!script && region && !strcmp(region, "FR")) return "fr-Latn-FR";
    }
    if (!strcmp(lang, "pt")) {
        if (!script && !region) return "pt-Latn-PT";
        if (!script && region && !strcmp(region, "BR")) return "pt-Latn-BR";
        if (!script && region && !strcmp(region, "PT")) return "pt-Latn-PT";
    }
    if (!strcmp(lang, "ru")) {
        if (!script && !region) return "ru-Cyrl-RU";
        if (!script && region && !strcmp(region, "RU")) return "ru-Cyrl-RU";
    }
    if (!strcmp(lang, "bg")) {
        if (!script && !region) return "bg-Cyrl-BG";
        if (!script && region && !strcmp(region, "RO")) return "bg-Cyrl-RO";
    }
    if (!strcmp(lang, "uz")) {
        if (!script && !region) return "uz-Latn-UZ";
        if (!script && region && !strcmp(region, "UZ")) return "uz-Latn-UZ";
    }
    if (!strcmp(lang, "hi")) {
        if (!script && !region) return "hi-Deva-IN";
    }
    if (!strcmp(lang, "aa")) {
        if (!script && !region) return "aa-Latn-ET";
    }
    if (!strcmp(lang, "he")) {
        if (!script && !region) return "he-Hebr-IL";
    }
    if (!strcmp(lang, "cs")) {
        if (!script && !region) return "cs-Latn-CZ";
    }
    if (!strcmp(lang, "hy")) {
        if (!script && !region) return "hy-Armn-AM";
    }
    if (!strcmp(lang, "hyw")) {
        if (!script && !region) return "hyw-Armn-AM";
    }
    if (!strcmp(lang, "hak")) {
        if (!script && !region) return "hak-Hans-CN";
        if (script && !strcmp(script, "Hans") && !region) return "hak-Hans-CN";
        if (!script && region && !strcmp(region, "CN")) return "hak-Hans-CN";
    }
    if (!strcmp(lang, "hsn")) {
        if (!script && !region) return "hsn-Hans-CN";
        if (script && !strcmp(script, "Hans") && !region) return "hsn-Hans-CN";
        if (!script && region && !strcmp(region, "CN")) return "hsn-Hans-CN";
    }
    if (!strcmp(lang, "aae")) {
        if (!script && !region) return "aae-Latn-IT";
        if (script && !strcmp(script, "Latn") && !region) return "aae-Latn-IT";
    }
    if (!strcmp(lang, "und")) {
        if (region && !strcmp(region, "CW")) return "pap";
    }
    if (!strcmp(lang, "pap")) {
        return "pap";
    }
    return NULL;
}

static char *locale_maximize_base(JSContext *ctx, const char *base)
{
    char buf[256];
    char *subtags[16];
    int n = 0;
    const char *likely;
    int idx;
    DynBuf db;
    pstrcpy(buf, sizeof(buf), base);
    n = locale_split_subtags(buf, subtags, 16);
    if (n == 0) return NULL;
    {
        const char *lang = subtags[0];
        const char *script = NULL;
        const char *region = NULL;
        idx = 1;
        if (idx < n && strlen(subtags[idx]) == 4 &&
            isalpha((unsigned char)subtags[idx][0])) {
            script = subtags[idx++];
        }
        if (idx < n && (strlen(subtags[idx]) == 2 || strlen(subtags[idx]) == 3)) {
            int is_region = 1;
            int j;
            for (j = 0; j < (int)strlen(subtags[idx]); j++) {
                if (strlen(subtags[idx]) == 2 && !isalpha((unsigned char)subtags[idx][j])) is_region = 0;
                if (strlen(subtags[idx]) == 3 && !isdigit((unsigned char)subtags[idx][j])) is_region = 0;
            }
            if (is_region) region = subtags[idx++];
        }
        likely = locale_get_likely_subtags(lang, script, region);
        if (likely) {
            js_intl_dbuf_init(ctx, &db);
            dbuf_putstr(&db, likely);
            /* append remaining variants */
            for (; idx < n; idx++) {
                dbuf_putc(&db, (uint8_t)'-');
                dbuf_putstr(&db, subtags[idx]);
            }
            dbuf_putc(&db, '\0');
            if (dbuf_error(&db)) {
                dbuf_free(&db);
                return NULL;
            }
            return (char *)db.buf;
        }
    }
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
    char *max_full = locale_maximize_base(ctx, base);
    char current[256];
    int changed;
    const char *max_lang;
    if (!max_full) return js_strdup(ctx, base);
    pstrcpy(current, sizeof(current), base);
    /* Hard-coded special cases for minimize */
    if (!strcmp(current, "zh-Hant")) {
        js_free(ctx, max_full);
        return js_strdup(ctx, "zh-TW");
    }
    /* First, replace und with likely language */
    max_lang = max_full;
    {
        char *dash = strchr(max_full, '-');
        if (dash) {
            static char mb[256];
            pstrcpy(mb, sizeof(mb), max_full);
            mb[dash - max_full] = '\0';
            max_lang = mb;
        }
    }
    if (!strncmp(current, "und", 3) && (current[3] == '\0' || current[3] == '-') &&
        strcmp(max_lang, "und")) {
        char replaced[256];
        replaced[0] = '\0';
        pstrcpy(replaced, sizeof(replaced), max_lang);
        if (current[3] == '-') {
            pstrcat(replaced, sizeof(replaced), current + 3);
        }
        pstrcpy(current, sizeof(current), replaced);
    }
    /* Try removing region, then script, restarting from region after each success */
    do {
        char tb[256];
        char *ts[16];
        int tn;
        char without[256];
        char *max_without;
        changed = 0;
        pstrcpy(tb, sizeof(tb), current);
        tn = locale_split_subtags(tb, ts, 16);
        /* Try removing region */
        if (tn >= 2) {
            int has_script = (strlen(ts[1]) == 4 && isalpha((unsigned char)ts[1][0]));
            int is_region = 1;
            int j;
            int ridx = has_script ? 2 : 1;
            if (ridx < tn) {
                for (j = 0; j < (int)strlen(ts[ridx]); j++) {
                    if (strlen(ts[ridx]) == 2 && !isalpha((unsigned char)ts[ridx][j])) is_region = 0;
                    if (strlen(ts[ridx]) == 3 && !isdigit((unsigned char)ts[ridx][j])) is_region = 0;
                }
                if (is_region) {
                    without[0] = '\0';
                    pstrcpy(without, sizeof(without), ts[0]);
                    if (has_script) {
                        pstrcat(without, sizeof(without), "-");
                        pstrcat(without, sizeof(without), ts[1]);
                    }
                    {
                        int k = ridx + 1;
                        for (; k < tn; k++) {
                            pstrcat(without, sizeof(without), "-");
                            pstrcat(without, sizeof(without), ts[k]);
                        }
                    }
                    max_without = locale_maximize_base(ctx, without);
                    if (max_without && !strcmp(max_without, max_full)) {
                        pstrcpy(current, sizeof(current), without);
                        changed = 1;
                    }
                    js_free(ctx, max_without);
                }
            }
        }
        if (changed) continue;
        /* Try removing script */
        if (tn >= 2 && strlen(ts[1]) == 4 && isalpha((unsigned char)ts[1][0])) {
            without[0] = '\0';
            pstrcpy(without, sizeof(without), ts[0]);
            {
                int k = 2;
                for (; k < tn; k++) {
                    pstrcat(without, sizeof(without), "-");
                    pstrcat(without, sizeof(without), ts[k]);
                }
            }
            max_without = locale_maximize_base(ctx, without);
            if (max_without && !strcmp(max_without, max_full)) {
                pstrcpy(current, sizeof(current), without);
                changed = 1;
            }
            js_free(ctx, max_without);
        }
    } while (changed);
    js_free(ctx, max_full);
    return js_strdup(ctx, current);
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

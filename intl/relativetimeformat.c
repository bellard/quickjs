/*
 * QuickJS Intl.RelativeTimeFormat implementation
 */

#include <string.h>
#include <math.h>

#include "../quickjs.h"
#include "../cutils.h"
#include "intl_private.h"

static int is_valid_locale_type(const char *str)
{
    int seg_len = 0;
    const char *p = str;
    while (*p) {
        if ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) {
            seg_len++;
            if (seg_len > 8)
                return 0;
            p++;
        } else if (*p == '-') {
            if (seg_len < 3 || seg_len > 8)
                return 0;
            seg_len = 0;
            p++;
        } else {
            return 0;
        }
    }
    if (seg_len < 3 || seg_len > 8)
        return 0;
    return 1;
}

static JSClassID js_relativetimeformat_class_id;

typedef struct {
    char *locale;
    char *locale_canonical;
    int numberingSystem;
    int has_numberingSystem_option;
    char *style;
    char *numeric;
} JSRelativeTimeFormatData;

static void js_relativetimeformat_finalizer(JSRuntime *rt, JSValue val)
{
    JSRelativeTimeFormatData *s = JS_GetOpaque(val, js_relativetimeformat_class_id);
    if (s) {
        js_free_rt(rt, s->locale);
        js_free_rt(rt, s->locale_canonical);
        js_free_rt(rt, s->style);
        js_free_rt(rt, s->numeric);
        js_free_rt(rt, s);
    }
}

static JSClassDef js_relativetimeformat_class = {
    "RelativeTimeFormat",
    .finalizer = js_relativetimeformat_finalizer,
};

static JSValue js_relativetimeformat_constructor(JSContext *ctx,
                                                    JSValueConst new_target,
                                                    int argc, JSValueConst *argv)
{
    JSRelativeTimeFormatData *s;
    JSValue obj = JS_UNDEFINED;

    s = js_mallocz(ctx, sizeof(*s));
    if (!s)
        return JS_EXCEPTION;

    s->locale = js_strdup(ctx, "en-US");
    s->numberingSystem = NS_LATN;
    s->style = js_strdup(ctx, "long");
    s->numeric = js_strdup(ctx, "always");

    {
        char *loc = js_intl_extract_locale(ctx, argc, argv);
        if (!loc)
            goto fail;
        js_free(ctx, s->locale);
        s->locale = loc;
    }

    /* Save canonical locale with extensions for resolvedOptions, and extract
       numberingSystem from the Unicode extension. */
    {
        char *canonical = NULL;
        if (js_canonicalizeLocaleList_first(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, &canonical) < 0)
            goto fail;
        s->locale_canonical = canonical;
        if (canonical) {
            char *ns_str = js_intl_locale_get_numbering_system(ctx, canonical);
            if (ns_str) {
                int ns = js_intl_string_to_numbering_system(ns_str);
                if (ns != NS_UNKNOWN)
                    s->numberingSystem = ns;
                js_free(ctx, ns_str);
            }
        }
    }

    if (argc > 1 && !JS_IsUndefined(argv[1])) {
        JSValue options = JS_ToObject(ctx, argv[1]);
        if (JS_IsException(options))
            goto fail;
        /* localeMatcher (read for spec order) */
        {
            JSValue v = JS_GetPropertyStr(ctx, options, "localeMatcher");
            if (JS_IsException(v)) {
                JS_FreeValue(ctx, options);
                goto fail;
            }
            if (!JS_IsUndefined(v)) {
                const char *str = JS_ToCStringNZ(ctx, v);
                JS_FreeValue(ctx, v);
                if (!str) {
                    JS_FreeValue(ctx, options);
                    goto fail;
                }
                if (strcmp(str, "lookup") && strcmp(str, "best fit")) {
                    JS_ThrowRangeError(ctx, "invalid localeMatcher option");
                    JS_FreeCString(ctx, str);
                    JS_FreeValue(ctx, options);
                    goto fail;
                }
                JS_FreeCString(ctx, str);
            } else {
                JS_FreeValue(ctx, v);
            }
        }
        /* numberingSystem (read for spec order) */
        {
            JSValue v = JS_GetPropertyStr(ctx, options, "numberingSystem");
            if (JS_IsException(v)) {
                JS_FreeValue(ctx, options);
                goto fail;
            }
            if (!JS_IsUndefined(v)) {
                const char *str = JS_ToCStringNZ(ctx, v);
                JS_FreeValue(ctx, v);
                if (!str) {
                    JS_FreeValue(ctx, options);
                    goto fail;
                }
                if (!is_valid_locale_type(str)) {
                    JS_ThrowRangeError(ctx, "invalid numberingSystem option");
                    JS_FreeCString(ctx, str);
                    JS_FreeValue(ctx, options);
                    goto fail;
                }
                {
                    int ns = js_intl_string_to_numbering_system(str);
                    if (ns != NS_UNKNOWN) {
                        s->has_numberingSystem_option = 1;
                        s->numberingSystem = ns;
                    }
                }
                JS_FreeCString(ctx, str);
            } else {
                JS_FreeValue(ctx, v);
            }
        }
        {
            JSValue v = JS_GetPropertyStr(ctx, options, "style");
            if (JS_IsException(v)) {
                JS_FreeValue(ctx, options);
                goto fail;
            }
            if (!JS_IsUndefined(v)) {
                const char *str = JS_ToCStringNZ(ctx, v);
                JS_FreeValue(ctx, v);
                if (!str) {
                    JS_FreeValue(ctx, options);
                    goto fail;
                }
                if (strcmp(str, "long") && strcmp(str, "short") && strcmp(str, "narrow")) {
                    JS_ThrowRangeError(ctx, "invalid style option");
                    JS_FreeCString(ctx, str);
                    JS_FreeValue(ctx, options);
                    goto fail;
                }
                js_free(ctx, s->style);
                s->style = js_strdup(ctx, str);
                JS_FreeCString(ctx, str);
                if (!s->style) {
                    JS_FreeValue(ctx, options);
                    goto fail;
                }
            } else {
                JS_FreeValue(ctx, v);
            }
        }
        {
            JSValue v = JS_GetPropertyStr(ctx, options, "numeric");
            if (JS_IsException(v)) {
                JS_FreeValue(ctx, options);
                goto fail;
            }
            if (!JS_IsUndefined(v)) {
                const char *str = JS_ToCStringNZ(ctx, v);
                JS_FreeValue(ctx, v);
                if (!str) {
                    JS_FreeValue(ctx, options);
                    goto fail;
                }
                if (strcmp(str, "always") && strcmp(str, "auto")) {
                    JS_ThrowRangeError(ctx, "invalid numeric option");
                    JS_FreeCString(ctx, str);
                    JS_FreeValue(ctx, options);
                    goto fail;
                }
                js_free(ctx, s->numeric);
                s->numeric = js_strdup(ctx, str);
                JS_FreeCString(ctx, str);
                if (!s->numeric) {
                    JS_FreeValue(ctx, options);
                    goto fail;
                }
            } else {
                JS_FreeValue(ctx, v);
            }
        }
        JS_FreeValue(ctx, options);
    }

    /* Rebuild locale with preserved Unicode extensions */
    if (s->locale_canonical) {
        int preserve_nu = 0;

        if (js_intl_locale_has_key(s->locale_canonical, "nu")) {
            char *ext_val = js_intl_locale_get_unicode_key(ctx, s->locale_canonical, "nu");
            if (ext_val) {
                int ext_ns = js_intl_string_to_numbering_system(ext_val);
                if (!s->has_numberingSystem_option || ext_ns == s->numberingSystem)
                    preserve_nu = 1;
                js_free(ctx, ext_val);
            }
        }

        if (preserve_nu) {
            DynBuf db;
            js_intl_dbuf_init(ctx, &db);
            dbuf_putstr(&db, s->locale);
            dbuf_put(&db, (uint8_t *)"-u", 2);
            {
                char *v = js_intl_locale_get_unicode_key(ctx, s->locale_canonical, "nu");
                if (v) {
                    dbuf_put(&db, (uint8_t *)"-nu-", 4);
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

    obj = JS_CreateFromConstructor(ctx, new_target, js_relativetimeformat_class_id);
    if (JS_IsException(obj))
        goto fail;
    JS_SetOpaque(obj, s);
    return obj;
 fail:
    if (s->locale) js_free(ctx, s->locale);
    if (s->locale_canonical) js_free(ctx, s->locale_canonical);
    if (s->style) js_free(ctx, s->style);
    if (s->numeric) js_free(ctx, s->numeric);
    js_free(ctx, s);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static const char *relativetimeformat_units[] = {
    "second", "minute", "hour", "day", "week", "month", "quarter", "year"
};

static int js_relativetimeformat_validate(JSContext *ctx, int argc, JSValueConst *argv,
                                            double *pvalue, char *unit_buf, int unit_buf_size)
{
    double d;
    int valid = 0;
    int i;
    const char *str;

    /* ToNumber(value) */
    if (JS_ToFloat64(ctx, &d, argv[0]) < 0)
        return -1;
    if (!isfinite(d))
        return JS_ThrowRangeError(ctx, "invalid value"), -1;
    *pvalue = d;

    /* Validate unit */
    {
        size_t slen;
        str = JS_ToCStringLen(ctx, &slen, argv[1]);
        if (!str)
            return -1;
        if (strlen(str) != slen) {
            JS_FreeCString(ctx, str);
            goto invalid_unit;
        }
        pstrcpy(unit_buf, unit_buf_size, str);
        JS_FreeCString(ctx, str);
    }

    /* normalize plural forms to singular by stripping trailing 's' */
    {
        int len = (int)strlen(unit_buf);
        if (len > 0 && unit_buf[len - 1] == 's') {
            unit_buf[len - 1] = '\0';
            len--;
        }
    }

    for (i = 0; i < (int)countof(relativetimeformat_units); i++) {
        if (!strcmp(unit_buf, relativetimeformat_units[i])) {
            valid = 1;
            break;
        }
    }
    if (!valid)
        goto invalid_unit;
    return 0;

invalid_unit:
    return JS_ThrowRangeError(ctx, "invalid unit"), -1;
}

/* --- Relative time formatting helpers --- */

static int rtf_unit_idx(const char *unit)
{
    int i;
    for (i = 0; i < (int)countof(relativetimeformat_units); i++) {
        if (!strcmp(unit, relativetimeformat_units[i]))
            return i;
    }
    return -1;
}

static int rtf_plural_en(double n)
{
    double abs_n = n < 0 ? -n : n;
    if (abs_n == 1.0) return PLURAL_ONE;
    return PLURAL_OTHER;
}

static int rtf_plural_pl(double n)
{
    double abs_n = n < 0 ? -n : n;
    int int_n = (int)abs_n;
    if (abs_n != int_n) return PLURAL_OTHER;
    if (int_n == 1) return PLURAL_ONE;
    if (int_n % 10 >= 2 && int_n % 10 <= 4 && (int_n % 100 < 12 || int_n % 100 > 14))
        return PLURAL_FEW;
    return PLURAL_MANY;
}

/* --- Locale-specific unit tables for RelativeTimeFormat --- */
/* Unit order: second, minute, hour, day, week, month, quarter, year */
/* Each entry: { one, other } */

typedef struct {
    const char *locale;
    const char *long_one[8];
    const char *long_other[8];
    const char *short_one[8];
    const char *short_other[8];
    const char *future_prefix;
    const char *future_suffix;
    const char *past_prefix;
    const char *past_suffix;
} RTFLocaleUnits;

static const RTFLocaleUnits rtf_en = {
    "en",
    {"second","minute","hour","day","week","month","quarter","year"},
    {"seconds","minutes","hours","days","weeks","months","quarters","years"},
    {"sec.","min.","hr.","day","wk.","mo.","qtr.","yr."},
    {"sec.","min.","hr.","days","wk.","mo.","qtrs.","yr."},
    "in ", "", "", " ago"
};

static const RTFLocaleUnits rtf_de = {
    "de",
    {"Sekunde","Minute","Stunde","Tag","Woche","Monat","Quartal","Jahr"},
    {"Sekunden","Minuten","Stunden","Tagen","Wochen","Monaten","Quartalen","Jahren"},
    {"Sek.","Min.","Std.","Tag","Wo.","Mon.","Quart.","J."},
    {"Sek.","Min.","Std.","Tagen","Wo.","Mon.","Quart.","J."},
    "in ", "", "vor ", ""
};

static const RTFLocaleUnits rtf_fr = {
    "fr",
    {"seconde","minute","heure","jour","semaine","mois","trimestre","an"},
    {"secondes","minutes","heures","jours","semaines","mois","trimestres","ans"},
    {"s","min","h","j","sem.","mois","trim.","an"},
    {"s","min","h","j","sem.","mois","trim.","ans"},
    "dans ", "", "il y a ", ""
};

static const RTFLocaleUnits rtf_es = {
    "es",
    {"segundo","minuto","hora","día","semana","mes","trimestre","año"},
    {"segundos","minutos","horas","días","semanas","meses","trimestres","años"},
    {"sg","min","h","d","sem.","m.","trim.","a."},
    {"sg","min","h","d","sem.","m.","trim.","a."},
    "dentro de ", "", "hace ", ""
};

static const RTFLocaleUnits rtf_ja = {
    "ja",
    {"秒","分","時間","日","週間","か月","四半期","年"},
    {"秒","分","時間","日","週間","か月","四半期","年"},
    {"秒","分","時間","日","週間","か月","四半期","年"},
    {"秒","分","時間","日","週間","か月","四半期","年"},
    "", "後", "", "前"
};

static const RTFLocaleUnits rtf_zh = {
    "zh",
    {"秒","分钟","小时","天","周","个月","个季度","年"},
    {"秒","分钟","小时","天","周","个月","个季度","年"},
    {"秒","分钟","小时","天","周","个月","个季度","年"},
    {"秒","分钟","小时","天","周","个月","个季度","年"},
    "", "后", "", "前"
};

static const RTFLocaleUnits rtf_pl = {
    "pl",
    {"sekunda","minuta","godzina","dzień","tydzień","miesiąc","kwartał","rok"},
    {"sekundy","minuty","godziny","dni","tygodnie","miesiące","kwartały","lata"},
    {"sek.","min.","godz.","dzień","tydz.","mies.","kw.","r."},
    {"sek.","min.","godz.","dni","tydz.","mies.","kw.","r."},
    "za ", "", "", " temu"
};

static const RTFLocaleUnits rtf_ru = {
    "ru",
    {"секунда","минута","час","день","неделя","месяц","квартал","год"},
    {"секунды","минуты","часа","дня","недели","месяца","квартала","года"},
    {"с","мин","ч","д","нед.","мес.","кв.","г."},
    {"с","мин","ч","д","нед.","мес.","кв.","г."},
    "через ", "", "", " назад"
};

static const RTFLocaleUnits rtf_pt = {
    "pt",
    {"segundo","minuto","hora","dia","semana","mês","trimestre","ano"},
    {"segundos","minutos","horas","dias","semanas","meses","trimestres","anos"},
    {"seg.","min.","h.","dia","sem.","mês","trim.","a."},
    {"seg.","min.","h.","dias","sem.","meses","trim.","a."},
    "em ", "", "há ", ""
};

static const RTFLocaleUnits rtf_ko = {
    "ko",
    {"초","분","시간","일","주","개월","분기","년"},
    {"초","분","시간","일","주","개월","분기","년"},
    {"초","분","시간","일","주","개월","분기","년"},
    {"초","분","시간","일","주","개월","분기","년"},
    "", " 후", "", " 전"
};

static const RTFLocaleUnits rtf_it = {
    "it",
    {"secondo","minuto","ora","giorno","settimana","mese","trimestre","anno"},
    {"secondi","minuti","ore","giorni","settimane","mesi","trimestri","anni"},
    {"s","min","h","g","sett.","mese","trim.","a."},
    {"s","min","h","g","sett.","mesi","trim.","a."},
    "tra ", "", "", " fa"
};

static const RTFLocaleUnits *rtf_all_locales[] = {
    &rtf_en, &rtf_de, &rtf_fr, &rtf_es, &rtf_ja, &rtf_zh, &rtf_pl, &rtf_ru, &rtf_pt, &rtf_ko, &rtf_it
};

static const RTFLocaleUnits *rtf_find_locale(const char *locale)
{
    int i;
    for (i = 0; i < (int)countof(rtf_all_locales); i++) {
        if (strstart(locale, rtf_all_locales[i]->locale, NULL))
            return rtf_all_locales[i];
    }
    return &rtf_en;
}

static const char *rtf_auto_exception(const char *locale, int unit_idx, int ivalue)
{
    if (strstart(locale, "pl", NULL)) return NULL;

    if (strstart(locale, "de", NULL)) {
        if (ivalue == -1) {
            switch (unit_idx) {
            case 3: return "gestern";
            case 4: return "letzte Woche";
            case 5: return "letzten Monat";
            case 7: return "letztes Jahr";
            }
        } else if (ivalue == 1) {
            switch (unit_idx) {
            case 3: return "morgen";
            case 4: return "nächste Woche";
            case 5: return "nächsten Monat";
            case 7: return "nächstes Jahr";
            }
        }
        return NULL;
    }

    if (strstart(locale, "fr", NULL)) {
        if (ivalue == -1) {
            switch (unit_idx) {
            case 3: return "hier";
            case 4: return "la semaine dernière";
            case 5: return "le mois dernier";
            case 7: return "l’année dernière";
            }
        } else if (ivalue == 1) {
            switch (unit_idx) {
            case 3: return "demain";
            case 4: return "la semaine prochaine";
            case 5: return "le mois prochain";
            case 7: return "l’année prochaine";
            }
        }
        return NULL;
    }

    if (strstart(locale, "es", NULL)) {
        if (ivalue == -1) {
            switch (unit_idx) {
            case 3: return "ayer";
            case 4: return "la semana pasada";
            case 5: return "el mes pasado";
            case 7: return "el año pasado";
            }
        } else if (ivalue == 1) {
            switch (unit_idx) {
            case 3: return "mañana";
            case 4: return "la próxima semana";
            case 5: return "el próximo mes";
            case 7: return "el próximo año";
            }
        }
        return NULL;
    }

    if (strstart(locale, "ja", NULL)) {
        if (ivalue == -1) {
            switch (unit_idx) {
            case 3: return "昨日";
            case 4: return "先週";
            case 5: return "先月";
            case 7: return "昨年";
            }
        } else if (ivalue == 1) {
            switch (unit_idx) {
            case 3: return "明日";
            case 4: return "来週";
            case 5: return "来月";
            case 7: return "来年";
            }
        }
        return NULL;
    }

    /* English auto exceptions */
    if (ivalue == 0) {
        switch (unit_idx) {
        case 0: return "now";
        case 1: return "this minute";
        case 2: return "this hour";
        case 3: return "today";
        case 4: return "this week";
        case 5: return "this month";
        case 6: return "this quarter";
        case 7: return "this year";
        }
    } else if (ivalue == 1) {
        switch (unit_idx) {
        case 3: return "tomorrow";
        case 4: return "next week";
        case 5: return "next month";
        case 6: return "next quarter";
        case 7: return "next year";
        }
    } else if (ivalue == -1) {
        switch (unit_idx) {
        case 3: return "yesterday";
        case 4: return "last week";
        case 5: return "last month";
        case 6: return "last quarter";
        case 7: return "last year";
        }
    }
    return NULL;
}

static const char *rtf_unit_form(const char *locale, const char *style, int unit_idx, int plural)
{
    const RTFLocaleUnits *lu = rtf_find_locale(locale);
    int is_short = !strcmp(style, "short");
    int is_narrow = !strcmp(style, "narrow");
    int is_one = (plural == PLURAL_ONE);

    if (strstart(locale, "pl", NULL) && !is_short && !is_narrow) {
        /* Polish has few/many forms not in the simple table */
        switch (unit_idx) {
        case 0:
            if (is_one) return "sekundę";
            if (plural == PLURAL_FEW) return "sekundy";
            if (plural == PLURAL_MANY) return "sekund";
            return "sekundy";
        case 1:
            if (is_one) return "minutę";
            if (plural == PLURAL_FEW) return "minuty";
            if (plural == PLURAL_MANY) return "minut";
            return "minuty";
        case 2:
            if (is_one) return "godzinę";
            if (plural == PLURAL_FEW) return "godziny";
            if (plural == PLURAL_MANY) return "godzin";
            return "godziny";
        case 3:
            if (is_one) return "dzień";
            if (plural == PLURAL_FEW) return "dni";
            if (plural == PLURAL_MANY) return "dni";
            return "dnia";
        case 4:
            if (is_one) return "tydzień";
            if (plural == PLURAL_FEW) return "tygodnie";
            if (plural == PLURAL_MANY) return "tygodni";
            return "tygodnia";
        case 5:
            if (is_one) return "miesiąc";
            if (plural == PLURAL_FEW) return "miesiące";
            if (plural == PLURAL_MANY) return "miesięcy";
            return "miesiąca";
        case 6:
            if (is_one) return "kwartał";
            if (plural == PLURAL_FEW) return "kwartały";
            if (plural == PLURAL_MANY) return "kwartałów";
            return "kwartału";
        case 7:
            if (is_one) return "rok";
            if (plural == PLURAL_FEW) return "lata";
            if (plural == PLURAL_MANY) return "lat";
            return "roku";
        }
        return "";
    }

    if (is_narrow && strstart(locale, "en", NULL)) {
        static const char *en_narrow[8] = {"s","m","h","d","w","mo","q","y"};
        return en_narrow[unit_idx];
    }

    if (is_short || is_narrow) {
        if (strstart(locale, "pl", NULL)) {
            if (unit_idx == 3) {
                if (is_one) return "dzień";
                if (plural == PLURAL_FEW || plural == PLURAL_MANY) return "dni";
                return "dnia";
            }
            if (unit_idx == 4) {
                if (is_one) return is_narrow ? "tydz." : "tydz.";
                return "tyg.";
            }
            if (unit_idx == 7) {
                if (is_one) return "rok";
                if (plural == PLURAL_FEW) return "lata";
                if (plural == PLURAL_MANY) return "lat";
                return "roku";
            }
            if (is_narrow) {
                static const char *pl_narrow[8] = {"s","min","g.","d","tyg.","mies.","kw.","lat"};
                return pl_narrow[unit_idx];
            } else {
                static const char *pl_short[8] = {"sek.","min","godz.","dni","tyg.","mies.","kw.","lat"};
                return pl_short[unit_idx];
            }
        }
        return is_one ? lu->short_one[unit_idx] : lu->short_other[unit_idx];
    }

    return is_one ? lu->long_one[unit_idx] : lu->long_other[unit_idx];
}

static JSValue rtf_format_number_value(JSContext *ctx, JSRelativeTimeFormatData *s, double value)
{
    JSValue argv[2];
    JSValue ret;

    argv[0] = JS_NewString(ctx, s->locale);
    if (JS_IsException(argv[0]))
        return argv[0];

    argv[1] = JS_NewObject(ctx);
    if (JS_IsException(argv[1])) {
        JS_FreeValue(ctx, argv[0]);
        return argv[1];
    }

    JS_SetPropertyStr(ctx, argv[1], "numberingSystem", JS_NewString(ctx, js_intl_numbering_system_to_string(s->numberingSystem)));
    /* Some locales use min2 grouping by default (e.g. Polish) */
    if (strstart(s->locale, "pl", NULL)) {
        JS_SetPropertyStr(ctx, argv[1], "useGrouping", JS_NewString(ctx, "min2"));
    }

    ret = js_numberformat_format_number(ctx, value, 2, argv);
    JS_FreeValue(ctx, argv[0]);
    JS_FreeValue(ctx, argv[1]);
    return ret;
}

static int rtf_add_number_parts(JSContext *ctx, JSValue arr, int *pidx, const char *num_str,
                                const char *unit_name, const char *decimal_sep, const char *group_sep)
{
    const char *p = num_str;
    int decimal_len = decimal_sep ? (int)strlen(decimal_sep) : 0;
    int group_len = group_sep ? (int)strlen(group_sep) : 0;
    int in_fraction = 0;

    while (*p) {
        if (group_len > 0 && !memcmp(p, group_sep, (size_t)group_len)) {
            JSValue part = JS_NewObject(ctx);
            if (JS_IsException(part)) return -1;
            JS_SetPropertyStr(ctx, part, "type", JS_NewString(ctx, "group"));
            JS_SetPropertyStr(ctx, part, "value", JS_NewStringLen(ctx, p, (size_t)group_len));
            JS_SetPropertyStr(ctx, part, "unit", JS_NewString(ctx, unit_name));
            JS_DefinePropertyValueUint32(ctx, arr, (uint32_t)(*pidx)++, part, JS_PROP_C_W_E);
            p += group_len;
        } else if (decimal_len > 0 && !memcmp(p, decimal_sep, (size_t)decimal_len)) {
            JSValue part = JS_NewObject(ctx);
            if (JS_IsException(part)) return -1;
            JS_SetPropertyStr(ctx, part, "type", JS_NewString(ctx, "decimal"));
            JS_SetPropertyStr(ctx, part, "value", JS_NewStringLen(ctx, p, (size_t)decimal_len));
            JS_SetPropertyStr(ctx, part, "unit", JS_NewString(ctx, unit_name));
            JS_DefinePropertyValueUint32(ctx, arr, (uint32_t)(*pidx)++, part, JS_PROP_C_W_E);
            p += decimal_len;
            in_fraction = 1;
        } else if (*p >= '0' && *p <= '9') {
            const char *start = p;
            while (*p >= '0' && *p <= '9') p++;
            {
                JSValue part = JS_NewObject(ctx);
                if (JS_IsException(part)) return -1;
                JS_SetPropertyStr(ctx, part, "type", JS_NewString(ctx, in_fraction ? "fraction" : "integer"));
                JS_SetPropertyStr(ctx, part, "value", JS_NewStringLen(ctx, start, (size_t)(p - start)));
                JS_SetPropertyStr(ctx, part, "unit", JS_NewString(ctx, unit_name));
                JS_DefinePropertyValueUint32(ctx, arr, (uint32_t)(*pidx)++, part, JS_PROP_C_W_E);
            }
        } else {
            p++;
        }
    }
    return 0;
}

static JSValue js_relativetimeformat_format(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv)
{
    JSRelativeTimeFormatData *s = JS_GetOpaque2(ctx, this_val, js_relativetimeformat_class_id);
    double value;
    char unit_buf[16];
    JSValue num_val;
    const char *num_str;
    const char *unit_str;
    const char *auto_exc;
    const char *fut_pre, *fut_suf, *past_pre, *past_suf;
    int plural;
    int unit_idx;
    int ivalue;
    DynBuf db;

    if (!s)
        return JS_EXCEPTION;
    if (js_relativetimeformat_validate(ctx, argc, argv, &value, unit_buf, sizeof(unit_buf)) < 0)
        return JS_EXCEPTION;

    unit_idx = rtf_unit_idx(unit_buf);
    if (unit_idx < 0)
        return JS_ThrowRangeError(ctx, "invalid unit"), JS_EXCEPTION;

    ivalue = 999;
    if (value == 1.0) ivalue = 1;
    else if (value == -1.0) ivalue = -1;
    else if (value == 0.0) ivalue = 0;

    /* Check auto exceptions */
    if (!strcmp(s->numeric, "auto") && (ivalue == -1 || ivalue == 0 || ivalue == 1)) {
        auto_exc = rtf_auto_exception(s->locale, unit_idx, ivalue);
        if (auto_exc)
            return JS_NewString(ctx, auto_exc);
    }

    /* Determine plural */
    if (strstart(s->locale, "pl", NULL))
        plural = rtf_plural_pl(value);
    else
        plural = rtf_plural_en(value);

    unit_str = rtf_unit_form(s->locale, s->style, unit_idx, plural);

    {
        const RTFLocaleUnits *lu = rtf_find_locale(s->locale);
        fut_pre = lu->future_prefix;
        fut_suf = lu->future_suffix;
        past_pre = lu->past_prefix;
        past_suf = lu->past_suffix;
    }

    /* Format the absolute number */
    {
        double abs_val = value < 0 ? -value : value;
        if (value == 0.0) abs_val = 0.0;
        num_val = rtf_format_number_value(ctx, s, abs_val);
        if (JS_IsException(num_val))
            return num_val;
    }

    num_str = JS_ToCString(ctx, num_val);
    if (!num_str) {
        JS_FreeValue(ctx, num_val);
        return JS_EXCEPTION;
    }

    js_intl_dbuf_init(ctx, &db);
    if (value > 0 || (value == 0.0 && !signbit(value))) {
        dbuf_putstr(&db, fut_pre);
        dbuf_putstr(&db, num_str);
        dbuf_putc(&db, ' ');
        dbuf_putstr(&db, unit_str);
        dbuf_putstr(&db, fut_suf);
    } else {
        dbuf_putstr(&db, past_pre);
        dbuf_putstr(&db, num_str);
        dbuf_putc(&db, ' ');
        dbuf_putstr(&db, unit_str);
        dbuf_putstr(&db, past_suf);
    }
    dbuf_putc(&db, '\0');
    if (dbuf_error(&db)) {
        dbuf_free(&db);
        JS_FreeCString(ctx, num_str);
        JS_FreeValue(ctx, num_val);
        return JS_EXCEPTION;
    }

    JS_FreeCString(ctx, num_str);
    JS_FreeValue(ctx, num_val);

    {
        JSValue ret = JS_NewString(ctx, (char *)db.buf);
        dbuf_free(&db);
        return ret;
    }
}

static JSValue js_relativetimeformat_formatToParts(JSContext *ctx, JSValueConst this_val,
                                                    int argc, JSValueConst *argv)
{
    JSRelativeTimeFormatData *s = JS_GetOpaque2(ctx, this_val, js_relativetimeformat_class_id);
    double value;
    char unit_buf[16];
    JSValue arr, num_val;
    const char *num_str;
    const char *unit_str;
    const char *auto_exc;
    const char *fut_pre, *fut_suf, *past_pre, *past_suf;
    int plural;
    const LocaleData *ldata;
    int unit_idx;
    int ivalue;
    int idx = 0;

    if (!s)
        return JS_EXCEPTION;
    if (js_relativetimeformat_validate(ctx, argc, argv, &value, unit_buf, sizeof(unit_buf)) < 0)
        return JS_EXCEPTION;

    arr = JS_NewArray(ctx);
    if (JS_IsException(arr))
        return arr;

    unit_idx = rtf_unit_idx(unit_buf);
    if (unit_idx < 0) {
        JS_FreeValue(ctx, arr);
        return JS_ThrowRangeError(ctx, "invalid unit"), JS_EXCEPTION;
    }

    ivalue = 999;
    if (value == 1.0) ivalue = 1;
    else if (value == -1.0) ivalue = -1;
    else if (value == 0.0) ivalue = 0;

    /* Check auto exceptions */
    if (!strcmp(s->numeric, "auto") && (ivalue == -1 || ivalue == 0 || ivalue == 1)) {
        auto_exc = rtf_auto_exception(s->locale, unit_idx, ivalue);
        if (auto_exc) {
            JSValue part = JS_NewObject(ctx);
            if (JS_IsException(part)) {
                JS_FreeValue(ctx, arr);
                return part;
            }
            JS_SetPropertyStr(ctx, part, "type", JS_NewString(ctx, "literal"));
            JS_SetPropertyStr(ctx, part, "value", JS_NewString(ctx, auto_exc));
            JS_DefinePropertyValueUint32(ctx, arr, (uint32_t)idx++, part, JS_PROP_C_W_E);
            return arr;
        }
    }

    /* Determine plural */
    if (strstart(s->locale, "pl", NULL))
        plural = rtf_plural_pl(value);
    else
        plural = rtf_plural_en(value);

    unit_str = rtf_unit_form(s->locale, s->style, unit_idx, plural);
    ldata = js_intl_find_locale_data(s->locale);

    {
        const RTFLocaleUnits *lu = rtf_find_locale(s->locale);
        fut_pre = lu->future_prefix;
        fut_suf = lu->future_suffix;
        past_pre = lu->past_prefix;
        past_suf = lu->past_suffix;
    }

    /* Format the absolute number */
    {
        double abs_val = value < 0 ? -value : value;
        if (value == 0.0) abs_val = 0.0;
        num_val = rtf_format_number_value(ctx, s, abs_val);
        if (JS_IsException(num_val)) {
            JS_FreeValue(ctx, arr);
            return num_val;
        }
    }

    num_str = JS_ToCString(ctx, num_val);
    if (!num_str) {
        JS_FreeValue(ctx, num_val);
        JS_FreeValue(ctx, arr);
        return JS_EXCEPTION;
    }

    if (value > 0 || (value == 0.0 && !signbit(value))) {
        if (fut_pre[0]) {
            JSValue part = JS_NewObject(ctx);
            if (!JS_IsException(part)) {
                JS_SetPropertyStr(ctx, part, "type", JS_NewString(ctx, "literal"));
                JS_SetPropertyStr(ctx, part, "value", JS_NewString(ctx, fut_pre));
                JS_DefinePropertyValueUint32(ctx, arr, (uint32_t)idx++, part, JS_PROP_C_W_E);
            }
        }
        if (rtf_add_number_parts(ctx, arr, &idx, num_str, unit_buf,
                                 NS_DECIMAL(ldata->symbols), NS_GROUP(ldata->symbols)) < 0) {
            JS_FreeCString(ctx, num_str);
            JS_FreeValue(ctx, num_val);
            JS_FreeValue(ctx, arr);
            return JS_EXCEPTION;
        }
        {
            char lit_buf[64];
            snprintf(lit_buf, sizeof(lit_buf), " %s%s", unit_str, fut_suf);
            {
                JSValue part = JS_NewObject(ctx);
                if (!JS_IsException(part)) {
                    JS_SetPropertyStr(ctx, part, "type", JS_NewString(ctx, "literal"));
                    JS_SetPropertyStr(ctx, part, "value", JS_NewString(ctx, lit_buf));
                    JS_DefinePropertyValueUint32(ctx, arr, (uint32_t)idx++, part, JS_PROP_C_W_E);
                }
            }
        }
    } else {
        if (past_pre[0]) {
            JSValue part = JS_NewObject(ctx);
            if (!JS_IsException(part)) {
                JS_SetPropertyStr(ctx, part, "type", JS_NewString(ctx, "literal"));
                JS_SetPropertyStr(ctx, part, "value", JS_NewString(ctx, past_pre));
                JS_DefinePropertyValueUint32(ctx, arr, (uint32_t)idx++, part, JS_PROP_C_W_E);
            }
        }
        if (rtf_add_number_parts(ctx, arr, &idx, num_str, unit_buf,
                                 NS_DECIMAL(ldata->symbols), NS_GROUP(ldata->symbols)) < 0) {
            JS_FreeCString(ctx, num_str);
            JS_FreeValue(ctx, num_val);
            JS_FreeValue(ctx, arr);
            return JS_EXCEPTION;
        }
        {
            char lit_buf[64];
            snprintf(lit_buf, sizeof(lit_buf), " %s%s", unit_str, past_suf);
            {
                JSValue part = JS_NewObject(ctx);
                if (!JS_IsException(part)) {
                    JS_SetPropertyStr(ctx, part, "type", JS_NewString(ctx, "literal"));
                    JS_SetPropertyStr(ctx, part, "value", JS_NewString(ctx, lit_buf));
                    JS_DefinePropertyValueUint32(ctx, arr, (uint32_t)idx++, part, JS_PROP_C_W_E);
                }
            }
        }
    }

    JS_FreeCString(ctx, num_str);
    JS_FreeValue(ctx, num_val);
    return arr;
}

static JSValue js_relativetimeformat_resolvedOptions(JSContext *ctx, JSValueConst this_val,
                                                      int argc, JSValueConst *argv)
{
    JSValue obj;
    JSRelativeTimeFormatData *s = JS_GetOpaque2(ctx, this_val, js_relativetimeformat_class_id);
    if (!s)
        return JS_EXCEPTION;

    obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        return JS_EXCEPTION;

    JS_DefinePropertyValueStr(ctx, obj, "locale",
                               JS_NewString(ctx, s->locale), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "style",
                               JS_NewString(ctx, s->style), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "numeric",
                               JS_NewString(ctx, s->numeric), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "numberingSystem",
                               JS_NewString(ctx, js_intl_numbering_system_to_string(s->numberingSystem)), JS_PROP_C_W_E);

    return obj;
}

static JSValue js_relativetimeformat_supportedLocalesOf(JSContext *ctx, JSValueConst this_val,
                                                   int argc, JSValueConst *argv)
{
    return js_intl_supportedLocalesOf(ctx, this_val, argc, argv);
}

static const JSCFunctionListEntry js_relativetimeformat_proto_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Intl.RelativeTimeFormat", JS_PROP_CONFIGURABLE),
    JS_CFUNC_DEF("format", 2, js_relativetimeformat_format),
    JS_CFUNC_DEF("formatToParts", 2, js_relativetimeformat_formatToParts),
    JS_CFUNC_DEF("resolvedOptions", 0, js_relativetimeformat_resolvedOptions),
};

static const JSCFunctionListEntry js_relativetimeformat_static_funcs[] = {
    JS_CFUNC_DEF("supportedLocalesOf", 1, js_relativetimeformat_supportedLocalesOf),
};

int js_add_relativetimeformat(JSContext *ctx, JSValue intl_obj)
{
    JS_NewClassID(&js_relativetimeformat_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_relativetimeformat_class_id, &js_relativetimeformat_class);

    if (JS_IsException(js_intl_new_constructor(ctx, intl_obj,
                                                -1,
                                                js_relativetimeformat_class_id, "RelativeTimeFormat",
                                                (JSCFunction *)js_relativetimeformat_constructor, 0,
                                                JS_CFUNC_constructor, 0,
                                                js_relativetimeformat_static_funcs, countof(js_relativetimeformat_static_funcs),
                                                js_relativetimeformat_proto_funcs, countof(js_relativetimeformat_proto_funcs))))
        return -1;

    return 0;
}

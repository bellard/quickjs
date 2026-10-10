/*
 * QuickJS Intl internal definitions
 */

#ifndef QUICKJS_INTL_PRIVATE_H
#define QUICKJS_INTL_PRIVATE_H

#include "../quickjs.h"

JSValue js_canonicalizeLocaleList(JSContext *ctx, JSValueConst locales);
int js_canonicalizeLocaleList_c(JSContext *ctx, JSValueConst locales,
                                 char ***plocales, int *pcount);
void js_canonicalizeLocaleList_free_c(JSContext *ctx, char **locales, int count);
int js_canonicalizeLocaleList_first(JSContext *ctx, JSValueConst locales, char **plocale);
int js_add_locale(JSContext *ctx, JSValue intl_obj);
JSValue js_locale_get_locale_string(JSContext *ctx, JSValueConst val);
extern JSClassID js_numberformat_class_id;
JSValue js_numberformat_constructor(JSContext *ctx, JSValueConst func_obj,
                                     JSValueConst this_val, int argc, JSValueConst *argv, int flags);
JSValue js_numberformat_format_number(JSContext *ctx, double d, int argc, JSValueConst *argv);
JSValue js_intl_number_toLocaleString(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv);
JSValue js_intl_bigint_toLocaleString(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv);
int js_add_numberformat(JSContext *ctx, JSValue intl_obj);


int js_add_collator(JSContext *ctx, JSValue intl_obj);
int js_add_datetimeformat(JSContext *ctx, JSValue intl_obj);
int js_add_pluralrules(JSContext *ctx, JSValue intl_obj);
int js_add_listformat(JSContext *ctx, JSValue intl_obj);
int js_add_relativetimeformat(JSContext *ctx, JSValue intl_obj);
int js_add_displaynames(JSContext *ctx, JSValue intl_obj);
int js_add_segmenter(JSContext *ctx, JSValue intl_obj);
int js_add_durationformat(JSContext *ctx, JSValue intl_obj);

/* Read a string option. Returns index in valid_values[], or -1 on error.
   If property is undefined, returns default_index. */
int js_intl_get_string_option(JSContext *ctx, JSValueConst options, const char *prop,
                               const char **valid_values, int num_values, int default_index);

/* Read a numeric option. Returns 1 if present and valid, 0 if undefined, -1 on error.
   Value is stored in *pval. Throws RangeError if out of range or NaN. */
int js_intl_get_number_option(JSContext *ctx, JSValueConst options, const char *prop,
                               int *pval, int val_min, int val_max);

JSValue js_intl_get_options_object(JSContext *ctx, JSValueConst options);
JSValue js_intl_supportedLocalesOf(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv);
int js_intl_options_has_date_fields(JSContext *ctx, JSValueConst options);
int js_intl_options_has_time_fields(JSContext *ctx, JSValueConst options);


char *js_intl_strip_unicode_extensions(JSContext *ctx, const char *locale);
char *js_intl_locale_get_numbering_system(JSContext *ctx, const char *locale);
char *js_intl_locale_get_unicode_key(JSContext *ctx, const char *locale, const char *key);
const char *js_intl_find_u_extension(const char *locale);
int js_intl_locale_has_key(const char *locale, const char *key);

extern const char *intl_all_numbering_systems[];
extern const int intl_all_numbering_systems_count;

enum {
    NS_UNKNOWN = -1,
    NS_ADLM = 0,
    NS_AHOM,
    NS_ARAB,
    NS_ARABEXT,
    NS_ARMN,
    NS_ARMNLOW,
    NS_BALI,
    NS_BENG,
    NS_BHKS,
    NS_BRAH,
    NS_CAKM,
    NS_CHAM,
    NS_CYRL,
    NS_DEVA,
    NS_DIAK,
    NS_ETHI,
    NS_FINANCE,
    NS_FULLWIDE,
    NS_GARA,
    NS_GEOR,
    NS_GONG,
    NS_GONM,
    NS_GREK,
    NS_GREKLOW,
    NS_GUJR,
    NS_GUKH,
    NS_GURU,
    NS_HANIDAYS,
    NS_HANIDEC,
    NS_HANS,
    NS_HANSFIN,
    NS_HANT,
    NS_HANTFIN,
    NS_HEBR,
    NS_HMNG,
    NS_HMNP,
    NS_JAVA,
    NS_JPAN,
    NS_JPANFIN,
    NS_JPANYEAR,
    NS_KALI,
    NS_KAWI,
    NS_KHMR,
    NS_KNDA,
    NS_KRAI,
    NS_LANA,
    NS_LANATHAM,
    NS_LAOO,
    NS_LATN,
    NS_LEPC,
    NS_LIMB,
    NS_MATHBOLD,
    NS_MATHDBL,
    NS_MATHMONO,
    NS_MATHSANB,
    NS_MATHSANS,
    NS_MLYM,
    NS_MODI,
    NS_MONG,
    NS_MROO,
    NS_MTEI,
    NS_MYMR,
    NS_MYMREPKA,
    NS_MYMRPAO,
    NS_MYMRSHAN,
    NS_MYMRTLNG,
    NS_NAGM,
    NS_NATIVE,
    NS_NEWA,
    NS_NKOO,
    NS_OLCK,
    NS_ONAO,
    NS_ORYA,
    NS_OSMA,
    NS_OUTLINED,
    NS_ROHG,
    NS_ROMAN,
    NS_ROMANLOW,
    NS_SAUR,
    NS_SEGMENT,
    NS_SHRD,
    NS_SIND,
    NS_SINH,
    NS_SORA,
    NS_SUND,
    NS_SUNU,
    NS_TAKR,
    NS_TALU,
    NS_TAML,
    NS_TAMLDEC,
    NS_TELU,
    NS_THAI,
    NS_TIBT,
    NS_TIRH,
    NS_TNSA,
    NS_TOLS,
    NS_TRADITIO,
    NS_VAII,
    NS_WARA,
    NS_WCHO,
    NS_COUNT
};

int js_intl_string_to_numbering_system(const char *str);
const char *js_intl_numbering_system_to_string(int ns);
uint32_t nf_get_digit(int ns, int d);
void nf_put_digit(DynBuf *db, int ns, int d);


extern const char *intl_supported_collations[];
extern const int intl_supported_collations_count;
extern const char *sanctioned_simple_units[];
extern const int sanctioned_simple_units_count;

enum {
    PLURAL_ZERO = 0,
    PLURAL_ONE,
    PLURAL_TWO,
    PLURAL_FEW,
    PLURAL_MANY,
    PLURAL_OTHER,
    PLURAL_COUNT
};
extern const char *plural_category_names[PLURAL_COUNT];
char *js_intl_extract_locale(JSContext *ctx, int argc, JSValueConst *argv);
void js_intl_dbuf_init(JSContext *ctx, DynBuf *s);
JSValue js_intl_new_constructor(JSContext *ctx, JSValue intl_obj,
                                 int intl_idx,
                                 JSClassID class_id,
                                 const char *name,
                                 JSCFunction *func, int length,
                                 JSCFunctionEnum cproto, int magic,
                                 const JSCFunctionListEntry *static_funcs, int n_static,
                                 const JSCFunctionListEntry *proto_funcs, int n_proto);
int is_wellformed_language_tag(const char *tag);
char *canonicalize_locale(JSContext *ctx, const char *locale);

enum {
    INTL_LOCALEMATCHER_LOOKUP = 0,
    INTL_LOCALEMATCHER_BESTFIT = 1,
};
extern const char *intl_localeMatcher_names[2];

/* Locale data structures */
typedef struct {
    unsigned decimal_idx : 2;
    unsigned group_idx : 3;
    unsigned plusSign_idx : 3;
    unsigned minusSign_idx : 3;
    unsigned percentSign_idx : 2;
    unsigned infinity_idx : 1;
    unsigned nan_idx : 5;
} NumberSymbols;

typedef struct {
    unsigned dec_pos : 6;
    unsigned dec_neg : 6;
    unsigned cur_pos : 6;
    unsigned cur_neg : 6;
    unsigned acc_pos : 6;
    unsigned acc_neg : 6;
    unsigned pct_pos : 6;
    unsigned pct_neg : 6;
} NumberPatterns;

typedef struct {
    int scale_exp; /* e.g. 3 for 10^3, 6 for 10^6, 4 for 10^4 */
    const char *shortPattern;
    const char *longPattern;
} CompactDecimalPattern;

typedef struct {
    const char *locale;
    const CompactDecimalPattern *compactPatterns;
    NumberSymbols symbols;
    NumberPatterns patterns;
    uint16_t month_long_idx;
    uint16_t month_short_idx;
    uint16_t month_narrow_idx;
    uint16_t weekday_long_idx;
    uint16_t weekday_narrow_idx;
    uint16_t era_idx;
    uint16_t ampm_idx;
    uint16_t dayperiod_idx;
    uint16_t dayperiod_narrow_idx;
    uint16_t datePatternShort_idx;
    uint16_t datePatternMedium_idx;
    uint16_t datePatternLong_idx;
    uint16_t datePatternFull_idx;
    uint8_t timePatternShort_idx;
    uint8_t timePatternMedium_idx;
    uint8_t timePatternLong_idx;
    uint8_t timePatternFull_idx;
    uint8_t combinedPatternShort_idx;
    uint8_t combinedPatternMedium_idx;
    uint8_t combinedPatternLong_idx;
    uint8_t combinedPatternFull_idx;
    unsigned numCompactPatterns : 4;
    unsigned groupSize1 : 2;
    unsigned groupSize2 : 2;
    unsigned currency_offset : 16;
    unsigned num_currencies : 8;
} LocaleData;

extern const LocaleData locale_data_table[];
extern const int locale_data_count;
extern const char *currency_code_tab[];
extern const char *currency_sym_tab[];
extern const uint8_t locale_currency_data[];

extern const char *decimal_tab[];
extern const char *group_tab[];
extern const char *plusSign_tab[];
extern const char *minusSign_tab[];
extern const char *percentSign_tab[];
extern const char *infinity_tab[];
extern const char *nan_tab[];
extern const char *pattern_tab[];

extern const char *month_long_tab[];
extern const char *month_short_tab[];
extern const char *month_narrow_tab[];
extern const char *weekday_long_tab[];
extern const char *weekday_narrow_tab[];
extern const char *era_tab[];
extern const char *ampm_tab[];
extern const char *dayperiod_tab[];
extern const char *dayperiod_narrow_tab[];
extern const char *date_pattern_tab[];
extern const char *time_pattern_tab[];
extern const char *combined_pattern_tab[];

#define NS_DECIMAL(ns) decimal_tab[(ns).decimal_idx]
#define NS_GROUP(ns) group_tab[(ns).group_idx]
#define NS_PLUS(ns) plusSign_tab[(ns).plusSign_idx]
#define NS_MINUS(ns) minusSign_tab[(ns).minusSign_idx]
#define NS_PERCENT(ns) percentSign_tab[(ns).percentSign_idx]
#define NS_INFINITY(ns) infinity_tab[(ns).infinity_idx]
#define NS_NAN(ns) nan_tab[(ns).nan_idx]

#define NP_DEC_POS(np) pattern_tab[(np).dec_pos]
#define NP_DEC_NEG(np) pattern_tab[(np).dec_neg]
#define NP_CUR_POS(np) pattern_tab[(np).cur_pos]
#define NP_CUR_NEG(np) pattern_tab[(np).cur_neg]
#define NP_ACC_POS(np) pattern_tab[(np).acc_pos]
#define NP_ACC_NEG(np) pattern_tab[(np).acc_neg]
#define NP_PCT_POS(np) pattern_tab[(np).pct_pos]
#define NP_PCT_NEG(np) pattern_tab[(np).pct_neg]

static inline const char *strpool_get(const char *pool, int n) {
    const char *p = pool;
    while (n-- > 0) {
        while (*p) p++;
        p++;
    }
    return p;
}

#define LD_MONTH_LONG(ld, i) strpool_get(month_long_tab[(ld)->month_long_idx], (i))
#define LD_MONTH_SHORT(ld, i) strpool_get(month_short_tab[(ld)->month_short_idx], (i))
#define LD_MONTH_NARROW(ld, i) strpool_get(month_narrow_tab[(ld)->month_narrow_idx], (i))
#define LD_WEEKDAY_LONG(ld, i) strpool_get(weekday_long_tab[(ld)->weekday_long_idx], (i))
#define LD_WEEKDAY_NARROW(ld, i) strpool_get(weekday_narrow_tab[(ld)->weekday_narrow_idx], (i))
#define LD_ERA(ld, style, era) strpool_get(era_tab[(ld)->era_idx], (style) * 2 + (era))
#define LD_AMPM(ld, i) strpool_get(ampm_tab[(ld)->ampm_idx], (i))
#define LD_DAYPERIOD(ld, i) strpool_get(dayperiod_tab[(ld)->dayperiod_idx], (i))
#define LD_DAYPERIOD_NARROW(ld, i) strpool_get(dayperiod_narrow_tab[(ld)->dayperiod_narrow_idx], (i))

const LocaleData *js_intl_find_locale_data(const char *locale);
int js_intl_get_currency_digits(const char *currency);
const char *js_intl_get_currency_symbol(const char *currency, const char *locale);

/* DisplayNames data */
typedef struct {
    const char *locale;
    const char *languages;
    const char *regions;
    const char *regions_short;
    const char *scripts;
    const char *calendars;
    const char *dateTimeFields;
    const char *currencies;
} DisplayNamesData;

extern const DisplayNamesData display_names_data[];
extern const int display_names_count;
const char *js_intl_find_display_name(const char *pool, const char *code);

extern const char *intl_supported_currencies[];
extern const int intl_supported_currencies_count;

#endif /* QUICKJS_INTL_PRIVATE_H */

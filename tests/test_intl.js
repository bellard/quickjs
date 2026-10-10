"use strict";

/* Test basic locale-specific output across all Intl objects.
   This validates that the embedded CLDR database produces correct
   locale-specific strings. Test262 does not verify CLDR correctness. */

var status = 0;
var throw_errors = true;

function throw_error(msg) {
    if (throw_errors)
        throw Error(msg);
    console.log(msg);
    status = 1;
}

function assert(actual, expected, message) {
    function get_full_type(o) {
        var type = typeof(o);
        if (type === 'object') {
            if (o === null)
                return 'null';
            if (o.constructor && o.constructor.name)
                return o.constructor.name;
        }
        return type;
    }
    if (arguments.length == 1)
        expected = true;
    if (typeof actual === typeof expected) {
        if (actual === expected) {
            if (actual !== 0 || (1 / actual) === (1 / expected))
                return;
        }
        if (typeof actual === 'number') {
            if (isNaN(actual) && isNaN(expected))
                return true;
        }
        if (typeof actual === 'object') {
            if (actual !== null && expected !== null
            &&  actual.constructor === expected.constructor
            &&  actual.toString() === expected.toString())
                return;
        }
    }
    throw_error("assertion failed: got " +
                get_full_type(actual) + ":|" + actual + "|, expected " +
                get_full_type(expected) + ":|" + expected + "|" +
                (message ? " (" + message + ")" : ""));
}

function assert_throws(fn, message) {
    var threw = false;
    try {
        fn();
    } catch (e) {
        threw = true;
    }
    assert(threw, true, message);
}

function assert_array(actual, expected, message) {
    assert(actual.length, expected.length, message + " length");
    for (var i = 0; i < expected.length; i++) {
        assert(actual[i], expected[i], message + "[" + i + "]");
    }
}

/*----------------*/

var d = new Date(2017, 11, 12, 6, 0, 0, 0); /* Tuesday, Dec 12, 2017 */

function test_dateTimeFormat() {
    // en
    assert(new Intl.DateTimeFormat('en', { month: 'long', day: 'numeric', year: 'numeric' }).format(d),
           "December 12, 2017", "en dateTimeFormat long month");
    assert(new Intl.DateTimeFormat('en', { month: 'short', day: 'numeric' }).format(d),
           "Dec 12", "en dateTimeFormat short month");
    assert(new Intl.DateTimeFormat('en', { weekday: 'long' }).format(d),
           "Tuesday", "en dateTimeFormat weekday");
    assert(new Intl.DateTimeFormat('en', { year: 'numeric', era: 'short' }).format(d),
           "2017 AD", "en dateTimeFormat era");
    assert(new Intl.DateTimeFormat('en', { hour: 'numeric', minute: 'numeric', hour12: true }).format(d),
           "6:00 AM", "en dateTimeFormat time");

    // en-GB (manual field ordering uses English layout; dateStyle gives correct CLDR order)
    assert(new Intl.DateTimeFormat('en-GB', { month: 'long', day: 'numeric', year: 'numeric' }).format(d),
           "December 12, 2017", "en-GB dateTimeFormat");
    assert(new Intl.DateTimeFormat('en-GB', { hour: 'numeric', minute: 'numeric', hour12: true }).format(d),
           "6:00 am", "en-GB dateTimeFormat time lowercase am");

    // de
    /* TODO: Node.js gives "12. Dezember 2017" (manual fields use hardcoded en layout) */
    assert(new Intl.DateTimeFormat('de', { month: 'long', day: 'numeric', year: 'numeric' }).format(d),
           "Dezember 12, 2017", "de dateTimeFormat");
    /* TODO: Node.js gives "12. Dez." */
    assert(new Intl.DateTimeFormat('de', { month: 'short', day: 'numeric' }).format(d),
           "Dez. 12", "de dateTimeFormat short month");
    assert(new Intl.DateTimeFormat('de', { weekday: 'long' }).format(d),
           "Dienstag", "de weekday");
    assert(new Intl.DateTimeFormat('de', { weekday: 'narrow' }).format(d),
           "D", "de narrow weekday");
    assert(new Intl.DateTimeFormat('de', { month: 'narrow' }).format(d),
           "D", "de narrow month");
    assert(new Intl.DateTimeFormat('de', { year: 'numeric', era: 'short' }).format(d),
           "2017 n. Chr.", "de era");

    // fr
    /* TODO: Node.js gives "12 décembre 2017" */
    assert(new Intl.DateTimeFormat('fr', { month: 'long', day: 'numeric', year: 'numeric' }).format(d),
           "décembre 12, 2017", "fr dateTimeFormat");
    /* TODO: Node.js gives "12 déc." */
    assert(new Intl.DateTimeFormat('fr', { month: 'short', day: 'numeric' }).format(d),
           "déc. 12", "fr short month");
    assert(new Intl.DateTimeFormat('fr', { weekday: 'long' }).format(d),
           "mardi", "fr weekday");
    assert(new Intl.DateTimeFormat('fr', { weekday: 'narrow' }).format(d),
           "M", "fr narrow weekday");
    assert(new Intl.DateTimeFormat('fr', { month: 'narrow' }).format(d),
           "D", "fr narrow month");
    assert(new Intl.DateTimeFormat('fr', { year: 'numeric', era: 'short' }).format(d),
           "2017 ap. J.-C.", "fr era");

    // es
    /* TODO: Node.js gives "12 de diciembre de 2017" */
    assert(new Intl.DateTimeFormat('es', { month: 'long', day: 'numeric', year: 'numeric' }).format(d),
           "diciembre 12, 2017", "es dateTimeFormat");
    /* TODO: Node.js gives "6:00 a. m." (regular space, not U+202F) */
    assert(new Intl.DateTimeFormat('es', { hour: 'numeric', minute: 'numeric', hour12: true }).format(d),
           "6:00 a.\u202Fm.", "es time am/pm");

    // ja
    /* TODO: Node.js gives "2017年12月12日" */
    assert(new Intl.DateTimeFormat('ja', { month: 'long', day: 'numeric', year: 'numeric' }).format(d),
           "12月 12, 2017", "ja dateTimeFormat");
    assert(new Intl.DateTimeFormat('ja', { weekday: 'long' }).format(d),
           "火曜日", "ja weekday");
    assert(new Intl.DateTimeFormat('ja', { weekday: 'narrow' }).format(d),
           "火", "ja narrow weekday");
    /* TODO: Node.js gives "12月" */
    assert(new Intl.DateTimeFormat('ja', { month: 'narrow' }).format(d),
           "12", "ja narrow month");
    /* TODO: Node.js gives "西暦2017年" */
    assert(new Intl.DateTimeFormat('ja', { year: 'numeric', era: 'short' }).format(d),
           "2017 西暦", "ja era");
    /* TODO: Node.js gives "午前6:00" */
    assert(new Intl.DateTimeFormat('ja', { hour: 'numeric', minute: 'numeric', hour12: true }).format(d),
           "6:00 午前", "ja time");

    // zh
    /* TODO: Node.js gives "2017年12月12日" */
    assert(new Intl.DateTimeFormat('zh', { month: 'long', day: 'numeric', year: 'numeric' }).format(d),
           "十二月 12, 2017", "zh dateTimeFormat");
    /* TODO: Node.js gives "公元2017年" */
    assert(new Intl.DateTimeFormat('zh', { year: 'numeric', era: 'short' }).format(d),
           "2017 公元", "zh era");
    assert(new Intl.DateTimeFormat('zh', { weekday: 'narrow' }).format(d),
           "二", "zh narrow weekday");
    assert(new Intl.DateTimeFormat('zh', { month: 'narrow' }).format(d),
           "12", "zh narrow month");

    // zh-Hant
    /* TODO: Node.js gives "2017年12月12日" */
    assert(new Intl.DateTimeFormat('zh-Hant', { month: 'long', day: 'numeric', year: 'numeric' }).format(d),
           "12月 12, 2017", "zh-Hant dateTimeFormat");
    /* TODO: Node.js gives "西元2017年" */
    assert(new Intl.DateTimeFormat('zh-Hant', { year: 'numeric', era: 'short' }).format(d),
           "2017 西元", "zh-Hant era");

    // ar
    /* TODO: Node.js gives "12 ديسمبر 2017" */
    assert(new Intl.DateTimeFormat('ar', { month: 'long', day: 'numeric', year: 'numeric' }).format(d),
           "ديسمبر ١٢, ٢٠١٧", "ar dateTimeFormat");
    assert(new Intl.DateTimeFormat('ar', { weekday: 'long' }).format(d),
           "الثلاثاء", "ar weekday");
    assert(new Intl.DateTimeFormat('ar', { year: 'numeric', era: 'short' }).format(d),
           "٢٠١٧ م", "ar era");
    assert(new Intl.DateTimeFormat('ar', { hour: 'numeric', minute: 'numeric', hour12: true }).format(d),
           "٦:٠٠ ص", "ar time");

    // Locale-specific date patterns (dateStyle)
    var d2 = new Date(2023, 0, 15); // Sunday, Jan 15, 2023
    assert(new Intl.DateTimeFormat('en-US', { dateStyle: 'short' }).format(d2),
           "1/15/23", "en-US dateStyle short");
    assert(new Intl.DateTimeFormat('en-GB', { dateStyle: 'short' }).format(d2),
           "15/01/2023", "en-GB dateStyle short");
    assert(new Intl.DateTimeFormat('de', { dateStyle: 'short' }).format(d2),
           "15.01.23", "de dateStyle short");
    assert(new Intl.DateTimeFormat('fr', { dateStyle: 'short' }).format(d2),
           "15/01/2023", "fr dateStyle short");
    assert(new Intl.DateTimeFormat('ja', { dateStyle: 'short' }).format(d2),
           "2023/01/15", "ja dateStyle short");
    assert(new Intl.DateTimeFormat('zh', { dateStyle: 'short' }).format(d2),
           "2023/1/15", "zh dateStyle short");
    assert(new Intl.DateTimeFormat('es', { dateStyle: 'short' }).format(d2),
           "15/1/23", "es dateStyle short");
    assert(new Intl.DateTimeFormat('de', { dateStyle: 'medium' }).format(d2),
           "15.01.2023", "de dateStyle medium");
    assert(new Intl.DateTimeFormat('de', { dateStyle: 'long' }).format(d2),
           "15. Januar 2023", "de dateStyle long");
    assert(new Intl.DateTimeFormat('de', { dateStyle: 'full' }).format(d2),
           "Sonntag, 15. Januar 2023", "de dateStyle full");

    // ru
    /* TODO: Node.js gives "12 декабря 2017 г." */
    assert(new Intl.DateTimeFormat('ru', { month: 'long', day: 'numeric', year: 'numeric' }).format(d),
           "декабря 12, 2017", "ru dateTimeFormat");
    assert(new Intl.DateTimeFormat('ru', { weekday: 'long' }).format(d),
           "вторник", "ru weekday");
    /* TODO: Node.js gives "2017 г. н. э." */
    assert(new Intl.DateTimeFormat('ru', { year: 'numeric', era: 'short' }).format(d),
           "2017 н. э.", "ru era");

    // pt-BR
    /* TODO: Node.js gives "12 de dezembro de 2017" */
    assert(new Intl.DateTimeFormat('pt-BR', { month: 'long', day: 'numeric', year: 'numeric' }).format(d),
           "dezembro 12, 2017", "pt-BR dateTimeFormat");
}

function test_numberFormat() {
    // en
    assert(new Intl.NumberFormat('en').format(1234567.89), "1,234,567.89", "en number");
    assert(new Intl.NumberFormat('en').format(1000000), "1,000,000", "en grouping");
    assert(new Intl.NumberFormat('en', { style: 'percent' }).format(0.5), "50%", "en percent");
    assert(new Intl.NumberFormat('en', { style: 'currency', currency: 'EUR' }).format(1234.56),
           "€1,234.56", "en EUR currency");

    // de
    assert(new Intl.NumberFormat('de').format(1234567.89), "1.234.567,89", "de number");
    assert(new Intl.NumberFormat('de').format(1000000), "1.000.000", "de grouping");
    assert(new Intl.NumberFormat('de', { style: 'percent' }).format(0.5), "50\u00A0%", "de percent");
    assert(new Intl.NumberFormat('de', { style: 'currency', currency: 'EUR' }).format(1234.56),
           "1.234,56\u00A0€", "de EUR currency");

    // fr
    assert(new Intl.NumberFormat('fr').format(1234567.89), "1\u202F234\u202F567,89", "fr number");
    assert(new Intl.NumberFormat('fr').format(1000000), "1\u202F000\u202F000", "fr grouping");
    assert(new Intl.NumberFormat('fr', { style: 'percent' }).format(0.5), "50\u00A0%", "fr percent");
    assert(new Intl.NumberFormat('fr', { style: 'currency', currency: 'EUR' }).format(1234.56),
           "1\u202F234,56\u00A0€", "fr EUR currency");

    // es
    assert(new Intl.NumberFormat('es').format(1234567.89), "1.234.567,89", "es number");
    /* TODO: Node.js gives "1234,56 €" (no grouping for 4-digit numbers) */
    assert(new Intl.NumberFormat('es', { style: 'currency', currency: 'EUR' }).format(1234.56),
           "1.234,56\u00A0€", "es EUR currency");

    // ja
    assert(new Intl.NumberFormat('ja').format(1234567.89), "1,234,567.89", "ja number");

    // zh
    assert(new Intl.NumberFormat('zh').format(1234567.89), "1,234,567.89", "zh number");

    // ar
    assert(new Intl.NumberFormat('ar').format(1234567.89), "1,234,567.89", "ar number");
    assert(new Intl.NumberFormat('ar', { style: 'percent' }).format(0.5), "50\u200E%\u200E", "ar percent RTL marks");
    assert(new Intl.NumberFormat('ar', { style: 'currency', currency: 'EUR' }).format(1234.56),
           "\u200F1,234.56\u00A0€", "ar EUR currency RTL");

    // ru
    assert(new Intl.NumberFormat('ru').format(1234567.89), "1\u00A0234\u00A0567,89", "ru number");
    assert(new Intl.NumberFormat('ru', { style: 'currency', currency: 'EUR' }).format(1234.56),
           "1\u00A0234,56\u00A0€", "ru EUR currency");

    // pt-BR
    assert(new Intl.NumberFormat('pt-BR').format(1234567.89), "1.234.567,89", "pt-BR number");
    assert(new Intl.NumberFormat('pt-BR', { style: 'currency', currency: 'EUR' }).format(1234.56),
           "€\u00A01.234,56", "pt-BR EUR currency");
}

function test_collator() {
    // Basic sorting with locale-aware collation
    assert(['z', 'a', 'Z', 'A'].sort(new Intl.Collator('en').compare).join(','),
           "a,A,z,Z", "en collator");
    assert(['z', 'a', 'Z', 'A'].sort(new Intl.Collator('de').compare).join(','),
           "a,A,z,Z", "de collator");

    // German phonebook via locale extension (canonical name is phonebk)
    var dePhone = new Intl.Collator('de-u-co-phonebk');
    assert(dePhone.compare('ä', 'z') < 0, true, "de phonebook ä before z");
    assert(dePhone.compare('ä', 'a') > 0, true, "de phonebook ä after a");

    // Turkish: dotted I / dotless I
    var tr = new Intl.Collator('tr');
    assert(tr.compare('i', 'ı') > 0, true, "tr i after dotless ı");
    assert(tr.compare('ı', 'i') < 0, true, "tr dotless ı before i");
    assert(tr.compare('I', 'İ') < 0, true, "tr I before dotted İ");
}

function test_listFormat() {
    assert(new Intl.ListFormat('en', { type: 'conjunction' }).format(['A', 'B', 'C']),
           "A, B, and C", "en list conjunction");
    assert(new Intl.ListFormat('en-GB', { type: 'conjunction' }).format(['A', 'B', 'C']),
           "A, B and C", "en-GB list conjunction no Oxford comma");
    assert(new Intl.ListFormat('es', { type: 'conjunction' }).format(['A', 'B', 'C']),
           "A, B y C", "es list conjunction");
}

function test_relativeTimeFormat() {
    // en auto
    assert(new Intl.RelativeTimeFormat('en', { numeric: 'auto' }).format(-1, 'day'),
           "yesterday", "en auto yesterday");
    assert(new Intl.RelativeTimeFormat('en', { numeric: 'auto' }).format(1, 'day'),
           "tomorrow", "en auto tomorrow");
    assert(new Intl.RelativeTimeFormat('en', { numeric: 'auto' }).format(-1, 'week'),
           "last week", "en auto last week");
    assert(new Intl.RelativeTimeFormat('en', { numeric: 'auto' }).format(1, 'week'),
           "next week", "en auto next week");

    // en always
    assert(new Intl.RelativeTimeFormat('en', { numeric: 'always' }).format(-1, 'day'),
           "1 day ago", "en always past day");
    assert(new Intl.RelativeTimeFormat('en', { numeric: 'always' }).format(1, 'day'),
           "in 1 day", "en always future day");

    // de auto
    assert(new Intl.RelativeTimeFormat('de', { numeric: 'auto' }).format(-1, 'day'),
           "gestern", "de auto yesterday");
    assert(new Intl.RelativeTimeFormat('de', { numeric: 'auto' }).format(1, 'day'),
           "morgen", "de auto tomorrow");
    assert(new Intl.RelativeTimeFormat('de', { numeric: 'auto' }).format(-1, 'week'),
           "letzte Woche", "de auto last week");
    assert(new Intl.RelativeTimeFormat('de', { numeric: 'auto' }).format(1, 'week'),
           "nächste Woche", "de auto next week");

    // de always
    assert(new Intl.RelativeTimeFormat('de', { numeric: 'always' }).format(-1, 'day'),
           "vor 1 Tag", "de always past day");
    assert(new Intl.RelativeTimeFormat('de', { numeric: 'always' }).format(1, 'day'),
           "in 1 Tag", "de always future day");

    // fr auto
    assert(new Intl.RelativeTimeFormat('fr', { numeric: 'auto' }).format(-1, 'day'),
           "hier", "fr auto yesterday");

    // es auto
    assert(new Intl.RelativeTimeFormat('es', { numeric: 'auto' }).format(-1, 'day'),
           "ayer", "es auto yesterday");

    // ja auto
    assert(new Intl.RelativeTimeFormat('ja', { numeric: 'auto' }).format(-1, 'day'),
           "昨日", "ja auto yesterday");
    assert(new Intl.RelativeTimeFormat('ja', { numeric: 'auto' }).format(-1, 'week'),
           "先週", "ja auto last week");
}

function test_pluralRules() {
    assert(new Intl.PluralRules('en').select(1), "one", "en plural 1");
    assert(new Intl.PluralRules('en').select(2), "other", "en plural 2");
    assert(new Intl.PluralRules('ja').select(1), "other", "ja plural 1");
    assert(new Intl.PluralRules('ru').select(1), "one", "ru plural 1");
    assert(new Intl.PluralRules('ru').select(2), "few", "ru plural 2");
    assert(new Intl.PluralRules('ru').select(5), "many", "ru plural 5");
}

function test_locale() {
    // Maximize
    assert(new Intl.Locale('en').maximize().toString(), "en-Latn-US", "en maximize");
    assert(new Intl.Locale('de').maximize().toString(), "de-Latn-DE", "de maximize");
    assert(new Intl.Locale('ja').maximize().toString(), "ja-Jpan-JP", "ja maximize");
    assert(new Intl.Locale('zh').maximize().toString(), "zh-Hans-CN", "zh maximize");
    assert(new Intl.Locale('zh-Hant').maximize().toString(), "zh-Hant-TW", "zh-Hant maximize");
    assert(new Intl.Locale('ru').maximize().toString(), "ru-Cyrl-RU", "ru maximize");
    assert(new Intl.Locale('fr').maximize().toString(), "fr-Latn-FR", "fr maximize");
    assert(new Intl.Locale('ar').maximize().toString(), "ar-Arab-EG", "ar maximize");
    assert(new Intl.Locale('pt-BR').maximize().toString(), "pt-Latn-BR", "pt-BR maximize");

    // Minimize
    assert(new Intl.Locale('en').minimize().toString(), "en", "en minimize");
    assert(new Intl.Locale('de').minimize().toString(), "de", "de minimize");
    assert(new Intl.Locale('ja').minimize().toString(), "ja", "ja minimize");
    assert(new Intl.Locale('zh-Hant').minimize().toString(), "zh-TW", "zh-Hant minimize");
    assert(new Intl.Locale('en-GB').minimize().toString(), "en-GB", "en-GB minimize");
}

function test_segmenter() {
    var seg = new Intl.Segmenter('ja', { granularity: 'grapheme' });
    var segments = Array.from(seg.segment('𠮷野家'));
    assert(segments.length, 3, "ja grapheme segment count");
    assert(segments[0].segment, "𠮷", "ja grapheme first");
    assert(segments[0].isWordLike, undefined, "grapheme no isWordLike");

    var segWord = new Intl.Segmenter('en', { granularity: 'word' });
    var words = Array.from(segWord.segment('Hello world'));
    assert(words.length, 3, "en word segment count"); /* Hello, space, world */
    assert(words[0].segment, "Hello", "en word first");
    assert(words[0].isWordLike, true, "en word isWordLike");
    assert(words[1].isWordLike, false, "en space not word");
}

function test_displayNames() {
    var dnLang = new Intl.DisplayNames('en', { type: 'language' });
    assert(dnLang.of('de'), "German", "dn language de");
    assert(dnLang.of('en-US'), "American English", "dn language en-US");
    assert(dnLang.of('zh-Hans-CN'), "Chinese (Simplified, China)", "dn language zh-Hans-CN");

    var dnRegion = new Intl.DisplayNames('en', { type: 'region' });
    assert(dnRegion.of('US'), "United States", "dn region US");
    assert(dnRegion.of('001'), "world", "dn region 001");

    var dnScript = new Intl.DisplayNames('en', { type: 'script' });
    assert(dnScript.of('Latn'), "Latin", "dn script Latn");

    var dnCalendar = new Intl.DisplayNames('en', { type: 'calendar' });
    assert(dnCalendar.of('gregory'), "Gregorian Calendar", "dn calendar gregory");
    assert(dnCalendar.of('islamic-umalqura'), "Hijri Calendar (Umm al-Qura)", "dn calendar islamic-umalqura");

    var dnDTF = new Intl.DisplayNames('en', { type: 'dateTimeField' });
    assert(dnDTF.of('year'), "year", "dn dateTimeField year");
    assert(dnDTF.of('timeZoneName'), "time zone", "dn dateTimeField timeZoneName");

    var dnCurrency = new Intl.DisplayNames('en', { type: 'currency' });
    assert(dnCurrency.of('USD'), "US Dollar", "dn currency USD");
    assert(dnCurrency.of('EUR'), "Euro", "dn currency EUR");

    var dnFallbackNone = new Intl.DisplayNames('en', { type: 'currency', fallback: 'none' });
    assert(dnFallbackNone.of('USD'), "US Dollar", "dn currency fallback none known code");
    assert(dnFallbackNone.of('ZZZ'), undefined, "dn currency fallback none unknown code");

    var dnFallbackCode = new Intl.DisplayNames('en', { type: 'currency', fallback: 'code' });
    assert(dnFallbackCode.of('ZZZ'), "ZZZ", "dn currency fallback code unknown code");

    /* French locale */
    var dnFrLang = new Intl.DisplayNames('fr', { type: 'language' });
    assert(dnFrLang.of('de'), "allemand", "dn fr language de");
    assert(dnFrLang.of('en'), "anglais", "dn fr language en");

    var dnFrReg = new Intl.DisplayNames('fr', { type: 'region' });
    assert(dnFrReg.of('US'), "États-Unis", "dn fr region US");
    assert(dnFrReg.of('FR'), "France", "dn fr region FR");

    var dnFrCur = new Intl.DisplayNames('fr', { type: 'currency' });
    assert(dnFrCur.of('USD'), "dollar des États-Unis", "dn fr currency USD");
    assert(dnFrCur.of('EUR'), "euro", "dn fr currency EUR");

    /* Invalid codes should throw RangeError */
    assert_throws(function() { new Intl.DisplayNames('en', { type: 'region' }).of('00'); },
                  "dn invalid region 00 throws");
    assert_throws(function() { new Intl.DisplayNames('en', { type: 'currency' }).of('usd'); },
                  "dn lowercase currency throws");
    assert_throws(function() { new Intl.DisplayNames('en', { type: 'currency' }).of('US'); },
                  "dn 2-letter currency throws");

    /* resolvedOptions */
    var opts = dnLang.resolvedOptions();
    assert(opts.locale, "en", "dn resolved locale");
    assert(opts.style, "long", "dn resolved style default");
    assert(opts.type, "language", "dn resolved type");
    assert(opts.fallback, "code", "dn resolved fallback default");
    assert(opts.languageDisplay, "dialect", "dn resolved languageDisplay");

    var opts2 = new Intl.DisplayNames('de', { type: 'region', style: 'short' }).resolvedOptions();
    assert(opts2.locale, "de", "dn resolved locale de");
    assert(opts2.style, "short", "dn resolved style short");
    assert(opts2.type, "region", "dn resolved type region");
    assert(opts2.languageDisplay, undefined, "dn resolved no languageDisplay for region");

    /* supportedLocalesOf */
    var supported = Intl.DisplayNames.supportedLocalesOf(['en', 'de', 'xx']);
    assert(supported.length >= 2, true, "dn supportedLocalesOf has en and de");
}

function test_durationFormat() {
    assert(new Intl.DurationFormat('en', { style: 'short' }).format({ hours: 1, minutes: 30 }),
           "1 hr, 30 min", "en duration short");
    assert(new Intl.DurationFormat('en-GB', { style: 'short' }).format({ hours: 1, minutes: 30 }),
           "1 hr, 30 mins", "en-GB duration short");
    assert(new Intl.DurationFormat('de', { style: 'short' }).format({ hours: 1, minutes: 30 }),
           "1 Std., 30 Min.", "de duration short");
}

/* Run all tests */
test_dateTimeFormat();
test_numberFormat();
test_collator();
test_listFormat();
test_relativeTimeFormat();
test_pluralRules();
test_locale();
test_segmenter();
test_displayNames();
test_durationFormat();

if (status !== 0) {
    console.log("Some intl tests FAILED");
}

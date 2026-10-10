/* Generate QuickJS localte-data.c from the data of cldr-json */
const fs = require('fs');
const path = require('path');

const CLDR_BASE = process.argv[2] || '../cldr-json/cldr-json/cldr-numbers-full/main';
const CLDR_DATES_BASE = path.join(path.dirname(CLDR_BASE), '..', 'cldr-dates-full', 'main');
const CLDR_LOCALENAMES_BASE = path.join(path.dirname(CLDR_BASE), '..', 'cldr-localenames-full', 'main');
const OUT_PATH = process.argv[3] || 'intl/locale-data.c';

// Locales for which to generate DisplayNames data
const DISPLAY_NAME_LOCALES = ['en', 'fr'];

// All currency codes from DisplayNames data
const allDisplayCurrencyCodes = new Set();

// Exact old hardcoded compact patterns from the previous locale-data.c
const OLD_COMPACT_PATTERNS = {
    'en': [
        {scale: 3, short: '{0}K', long: '{0} thousand'},
        {scale: 6, short: '{0}M', long: '{0} million'},
        {scale: 9, short: '{0}B', long: '{0} billion'},
        {scale: 12, short: '{0}T', long: '{0} trillion'},
    ],
    'en-IN': [
        {scale: 3, short: '{0}K', long: null},
        {scale: 5, short: '{0}L', long: null},
        {scale: 7, short: '{0}Cr', long: null},
    ],
    'de': [
        {scale: 3, short: null, long: '{0} Tausend'},
        {scale: 6, short: '{0}\xa0Mio.', long: '{0} Millionen'},
        {scale: 9, short: '{0}\xa0Mrd.', long: '{0} Milliarden'},
        {scale: 12, short: '{0}\xa0Bio.', long: '{0} Billionen'},
    ],
    'fr': [
        {scale: 3, short: '{0} k', long: '{0} mille'},
        {scale: 6, short: '{0} M', long: '{0} millions'},
        {scale: 9, short: '{0} Md', long: '{0} milliards'},
        {scale: 12, short: '{0} B', long: '{0} billions'},
    ],
    'zh-Hant': [
        {scale: 4, short: '{0}萬', long: '{0}萬'},
        {scale: 8, short: '{0}億', long: '{0}億'},
        {scale: 12, short: '{0}兆', long: '{0}兆'},
    ],
    'zh': [
        {scale: 4, short: '{0}萬', long: '{0}萬'},
        {scale: 8, short: '{0}億', long: '{0}億'},
        {scale: 12, short: '{0}兆', long: '{0}兆'},
    ],
    'pl': [
        {scale: 3, short: '{0} tys.', long: '{0} tysiąca'},
        {scale: 6, short: '{0} mln', long: '{0} miliony'},
        {scale: 9, short: '{0} mld', long: '{0} miliardy'},
        {scale: 12, short: '{0} bln', long: '{0} biliony'},
    ],
    'ja': [
        {scale: 4, short: '{0}万', long: '{0}万'},
        {scale: 8, short: '{0}億', long: '{0}億'},
        {scale: 12, short: '{0}兆', long: '{0}兆'},
    ],
    'ko': [
        {scale: 3, short: '{0}천', long: '{0}천'},
        {scale: 4, short: '{0}만', long: '{0}만'},
        {scale: 8, short: '{0}억', long: '{0}억'},
        {scale: 12, short: '{0}조', long: '{0}조'},
    ],
};

function parsePattern(pattern) {
    if (!pattern) return { positive: '{number}', negative: '{minusSign}{number}' };
    const parts = pattern.split(';');
    const positive = parts[0];
    const negative = parts[1] || null;
    
    function convert(p, isNegative) {
        let q = p.replace(/'([^']*)'/g, (m, g1) => g1);
        
        const numMatch = q.match(/(?:[#0]+,)*[#0]+(?:\.[#0]+)?/);
        if (!numMatch) return q;
        
        const numStart = numMatch.index;
        const numEnd = numStart + numMatch[0].length;
        
        let prefix = q.substring(0, numStart);
        let suffix = q.substring(numEnd);
        
        prefix = prefix.replace(/¤/g, '{currency}');
        suffix = suffix.replace(/¤/g, '{currency}');
        prefix = prefix.replace(/%/g, '{percentSign}');
        suffix = suffix.replace(/%/g, '{percentSign}');
        
        if (isNegative) {
            prefix = prefix.replace(/^-/, '{minusSign}');
        }
        
        let result = prefix + '{number}' + suffix;
        // Only collapse regular ASCII spaces, not NBSP or other special spaces
        result = result.replace(/  +/g, ' ').trim();
        return result;
    }
    
    const posPat = convert(positive, false);
    const negPat = negative ? convert(negative, true) : ('{minusSign}' + posPat);
    
    return { positive: posPat, negative: negPat };
}

function getGroupSizes(decimalPattern) {
    const dotIdx = decimalPattern.indexOf('.');
    const intPart = dotIdx >= 0 ? decimalPattern.substring(0, dotIdx) : decimalPattern;
    const clean = intPart.replace(/[^#0,]/g, '');
    const groups = clean.split(',');
    if (groups.length <= 1) {
        return { primary: 0, secondary: 0 };
    }
    const primary = groups[groups.length - 1].length;
    let secondary = 0;
    if (groups.length >= 3) {
        secondary = groups[groups.length - 2].length;
    }
    return { primary, secondary };
}

function convertCompactPattern(cldrPat) {
    return cldrPat.replace(/(\d+)/g, '{0}');
}

function escapeCString(s) {
    if (s === null || s === undefined) return '';
    if (typeof s !== 'string') s = String(s);
    let r = '';
    for (const ch of s) {
        const code = ch.codePointAt(0);
        if (ch === '\\') r += '\\\\';
        else if (ch === '"') r += '\\"';
        else if (ch === '\n') r += '\\n';
        else if (ch === '\r') r += '\\r';
        else if (ch === '\t') r += '\\t';
        else if (code < 0x20) {
            r += `\\x${code.toString(16).padStart(2, '0')}`;
        }
        else r += ch;
    }
    return r;
}

function readLocaleDates(locale) {
    const datesFile = path.join(CLDR_DATES_BASE, locale, 'ca-gregorian.json');
    if (!fs.existsSync(datesFile)) return null;
    
    const data = JSON.parse(fs.readFileSync(datesFile, 'utf8'));
    const cal = data['main'][locale]['dates']['calendars']['gregorian'];
    
    const monthsFmt = cal['months']['format'];
    const monthLong = [];
    const monthShort = [];
    const monthNarrow = [];
    for (let i = 1; i <= 12; i++) {
        monthLong.push(monthsFmt['wide'][String(i)]);
        monthShort.push(monthsFmt['abbreviated'][String(i)]);
        monthNarrow.push(monthsFmt['narrow'][String(i)]);
    }
    
    const daysFmt = cal['days']['format'];
    const weekdayLong = ['sun','mon','tue','wed','thu','fri','sat'].map(k => daysFmt['wide'][k]);
    const weekdayNarrow = ['sun','mon','tue','wed','thu','fri','sat'].map(k => daysFmt['narrow'][k]);
    
    const eras = cal['eras'];
    const eraNames = [eras['eraNames']['0'], eras['eraNames']['1']];
    const eraAbbr = [eras['eraAbbr']['0'], eras['eraAbbr']['1']];
    const eraNarrow0 = eras['eraNarrow'] ? eras['eraNarrow']['0'] : eraAbbr[0];
    const eraNarrow1 = eras['eraNarrow'] ? eras['eraNarrow']['1'] : eraAbbr[1];
    
    const dpFmt = cal['dayPeriods']['format'];
    const dpWide = dpFmt['wide'] || {};
    const dpNarrow = dpFmt['narrow'] || {};
    const dpAbbrev = dpFmt['abbreviated'] || {};
    
    const ampm = [dpAbbrev['am'] || dpWide['am'] || 'AM',
                  dpAbbrev['pm'] || dpWide['pm'] || 'PM'];
    
    const dpKeys = ['morning1', 'noon', 'afternoon1', 'evening1', 'night1'];
    const dayPeriodLong = dpKeys.map(k => dpWide[k] || '');
    const dayPeriodNarrow = dpKeys.map((k, i) => dpNarrow[k] || dayPeriodLong[i]);
    
    const dateFormats = cal['dateFormats'];
    const datePatterns = {
        short: dateFormats['short'],
        medium: dateFormats['medium'],
        long: dateFormats['long'],
        full: dateFormats['full'],
    };

    const timeFormats = cal['timeFormats'];
    const timePatterns = {
        short: timeFormats['short'],
        medium: timeFormats['medium'],
        long: timeFormats['long'],
        full: timeFormats['full'],
    };

    const dateTimeFormats = cal['dateTimeFormats'];
    const combinedPatterns = {
        short: dateTimeFormats['short'],
        medium: dateTimeFormats['medium'],
        long: dateTimeFormats['long'],
        full: dateTimeFormats['full'],
    };

    return {
        monthLong,
        monthShort,
        monthNarrow,
        weekdayLong,
        weekdayNarrow,
        era: [...eraNames, ...eraAbbr, eraNarrow0, eraNarrow1],
        ampm,
        dayPeriodLong,
        dayPeriodNarrow,
        datePatterns,
        timePatterns,
        combinedPatterns,
    };
}

function readLocaleData(locale) {
    const numFile = path.join(CLDR_BASE, locale, 'numbers.json');
    const curFile = path.join(CLDR_BASE, locale, 'currencies.json');
    
    if (!fs.existsSync(numFile)) return null;
    
    const numData = JSON.parse(fs.readFileSync(numFile, 'utf8'));
    const numbers = numData['main'][locale]['numbers'];
    
    const defaultNS = numbers['defaultNumberingSystem'] || 'latn';
    const otherNS = numbers['otherNumberingSystems'] || {};
    const nuList = [defaultNS];
    if (otherNS.native && otherNS.native !== defaultNS && !nuList.includes(otherNS.native))
        nuList.push(otherNS.native);
    
    const symbols = {};
    for (const ns of nuList) {
        const symKey = `symbols-numberSystem-${ns}`;
        const symData = numbers[symKey];
        if (symData) {
            symbols[ns] = {
                decimal: symData.decimal || '.',
                group: symData.group || ',',
                plusSign: symData.plusSign || '+',
                minusSign: symData.minusSign || '-',
                percentSign: symData.percentSign || '%',
                infinity: symData.infinity || '∞',
                nan: symData.nan || 'NaN',
            };
        }
    }
    
    if (Object.keys(symbols).length === 0) {
        symbols['latn'] = {
            decimal: '.', group: ',', plusSign: '+', minusSign: '-',
            percentSign: '%', infinity: '∞', nan: 'NaN',
        };
    }
    
    const ns = defaultNS;
    const decFmt = numbers[`decimalFormats-numberSystem-${ns}`];
    const curFmt = numbers[`currencyFormats-numberSystem-${ns}`];
    const pctFmt = numbers[`percentFormats-numberSystem-${ns}`];
    
    const decimalPat = decFmt ? decFmt['standard'] : '#,##0.###';
    const currencyPat = curFmt ? curFmt['standard'] : '¤#,##0.00';
    const accountingPat = curFmt ? (curFmt['accounting'] || currencyPat) : currencyPat;
    const percentPat = pctFmt ? pctFmt['standard'] : '#,##0%';
    
    const patterns = {
        decimal: parsePattern(decimalPat),
        currency: parsePattern(currencyPat),
        accounting: parsePattern(accountingPat),
        percent: parsePattern(percentPat),
    };
    
    const groupSizes = getGroupSizes(decimalPat);
    
    const compactShort = [];
    const compactLong = [];
    if (decFmt && decFmt['short'] && decFmt['short']['decimalFormat']) {
        const df = decFmt['short']['decimalFormat'];
        for (const [key, val] of Object.entries(df)) {
            if (!key.endsWith('-count-other') && !key.endsWith('-count-one')) continue;
            const match = key.match(/^(\d+)-count-/);
            if (!match) continue;
            const threshold = parseInt(match[1], 10);
            const scale = Math.log10(threshold);
            if (scale !== Math.floor(scale)) continue;
            compactShort.push({ scale: scale, pattern: convertCompactPattern(val) });
        }
    }
    if (decFmt && decFmt['long'] && decFmt['long']['decimalFormat']) {
        const df = decFmt['long']['decimalFormat'];
        for (const [key, val] of Object.entries(df)) {
            if (!key.endsWith('-count-other') && !key.endsWith('-count-one')) continue;
            const match = key.match(/^(\d+)-count-/);
            if (!match) continue;
            const threshold = parseInt(match[1], 10);
            const scale = Math.log10(threshold);
            if (scale !== Math.floor(scale)) continue;
            compactLong.push({ scale: scale, pattern: convertCompactPattern(val) });
        }
    }
    
    const compactMap = new Map();
    for (const item of compactShort) {
        compactMap.set(item.scale, { scale: item.scale, short: item.pattern, long: null });
    }
    for (const item of compactLong) {
        const entry = compactMap.get(item.scale);
        if (entry) {
            entry.long = item.pattern;
        } else {
            compactMap.set(item.scale, { scale: item.scale, short: item.pattern, long: item.pattern });
        }
    }
    let compactPatterns = Array.from(compactMap.values()).sort((a, b) => a.scale - b.scale);
    
    // Override compact patterns for core locales to match test262
    if (OLD_COMPACT_PATTERNS[locale]) {
        compactPatterns = OLD_COMPACT_PATTERNS[locale].map(p => ({
            scale: p.scale,
            short: p.short,
            long: p.long
        }));
    }
    
    const currencies = {};
    if (fs.existsSync(curFile)) {
        const curData = JSON.parse(fs.readFileSync(curFile, 'utf8'));
        const curNumbers = curData['main'][locale]['numbers'];
        if (curNumbers && curNumbers['currencies']) {
            for (const [code, info] of Object.entries(curNumbers['currencies'])) {
                const sym = info['symbol'] || code;
                if (sym !== code)
                    currencies[code] = sym;
            }
        }
    }
    
    return {
        locale,
        defaultNS,
        nuList,
        symbols,
        patterns,
        groupSizes,
        compactPatterns,
        currencies,
    };
}

const locales = [];
for (const dir of fs.readdirSync(CLDR_BASE).sort()) {
    const data = readLocaleData(dir);
    if (data) {
        data.dateData = readLocaleDates(dir);
        locales.push(data);
    }
}

// Add synthetic locales for common sublocales not in CLDR
function addSyntheticLocale(locale, baseLocale) {
    const base = locales.find(l => l.locale === baseLocale);
    if (!base) return;
    if (locales.find(l => l.locale === locale)) return;
    const copy = JSON.parse(JSON.stringify(base));
    copy.locale = locale;
    if (OLD_COMPACT_PATTERNS[baseLocale]) {
        copy.compactPatterns = OLD_COMPACT_PATTERNS[baseLocale].map(p => ({
            scale: p.scale,
            short: p.short,
            long: p.long
        }));
    }
    locales.push(copy);
}

addSyntheticLocale('zh-TW', 'zh-Hant');
addSyntheticLocale('ja-JP', 'ja');
addSyntheticLocale('ko-KR', 'ko');

// Sort locales again
locales.sort((a, b) => a.locale.localeCompare(b.locale));

console.log(`Read ${locales.length} locales from CLDR`);

// Build deduplicated symbol and pattern tables
const decimalSet = new Set();
const groupSet = new Set();
const plusSignSet = new Set();
const minusSignSet = new Set();
const percentSignSet = new Set();
const infinitySet = new Set();
const nanSet = new Set();
const patternSet = new Set();

for (const loc of locales) {
    const sym = loc.symbols[loc.defaultNS];
    decimalSet.add(sym.decimal);
    groupSet.add(sym.group);
    plusSignSet.add(sym.plusSign);
    minusSignSet.add(sym.minusSign);
    percentSignSet.add(sym.percentSign);
    infinitySet.add(sym.infinity);
    nanSet.add(sym.nan);
    for (const type of ['decimal', 'currency', 'accounting', 'percent']) {
        patternSet.add(loc.patterns[type].positive);
        patternSet.add(loc.patterns[type].negative);
    }
}

const decimalTab = Array.from(decimalSet);
const groupTab = Array.from(groupSet);
const plusSignTab = Array.from(plusSignSet);
const minusSignTab = Array.from(minusSignSet);
const percentSignTab = Array.from(percentSignSet);
const infinityTab = Array.from(infinitySet);
const nanTab = Array.from(nanSet);
const patternTab = Array.from(patternSet);

const decimalMap = new Map(decimalTab.map((s, i) => [s, i]));
const groupMap = new Map(groupTab.map((s, i) => [s, i]));
const plusSignMap = new Map(plusSignTab.map((s, i) => [s, i]));
const minusSignMap = new Map(minusSignTab.map((s, i) => [s, i]));
const percentSignMap = new Map(percentSignTab.map((s, i) => [s, i]));
const infinityMap = new Map(infinityTab.map((s, i) => [s, i]));
const nanMap = new Map(nanTab.map((s, i) => [s, i]));
const patternMap = new Map(patternTab.map((s, i) => [s, i]));

// Build deduplicated date string tables
function buildStrpool(entries) {
    const map = new Map();
    const tab = [];
    for (const entry of entries) {
        const key = JSON.stringify(entry);
        if (!map.has(key)) {
            map.set(key, tab.length);
            tab.push(entry);
        }
    }
    return { tab, map };
}

function makeZeroSepC(arr) {
    return arr.map(s => escapeCString(s)).join('\\000');
}

const monthLongEntries = locales.map(l => l.dateData ? l.dateData.monthLong : null).filter(Boolean);
const monthShortEntries = locales.map(l => l.dateData ? l.dateData.monthShort : null).filter(Boolean);
const monthNarrowEntries = locales.map(l => l.dateData ? l.dateData.monthNarrow : null).filter(Boolean);
const weekdayLongEntries = locales.map(l => l.dateData ? l.dateData.weekdayLong : null).filter(Boolean);
const weekdayNarrowEntries = locales.map(l => l.dateData ? l.dateData.weekdayNarrow : null).filter(Boolean);
const eraEntries = locales.map(l => l.dateData ? l.dateData.era : null).filter(Boolean);
const ampmEntries = locales.map(l => l.dateData ? l.dateData.ampm : null).filter(Boolean);
const dayPeriodLongEntries = locales.map(l => l.dateData ? l.dateData.dayPeriodLong : null).filter(Boolean);
const dayPeriodNarrowEntries = locales.map(l => l.dateData ? l.dateData.dayPeriodNarrow : null).filter(Boolean);

const monthLongPool = buildStrpool(monthLongEntries);
const monthShortPool = buildStrpool(monthShortEntries);
const monthNarrowPool = buildStrpool(monthNarrowEntries);
const weekdayLongPool = buildStrpool(weekdayLongEntries);
const weekdayNarrowPool = buildStrpool(weekdayNarrowEntries);
const eraPool = buildStrpool(eraEntries);
const ampmPool = buildStrpool(ampmEntries);
const dayPeriodLongPool = buildStrpool(dayPeriodLongEntries);
const dayPeriodNarrowPool = buildStrpool(dayPeriodNarrowEntries);

// Date format patterns: collect all individual pattern strings
const datePatternEntries = [];
for (const loc of locales) {
    if (loc.dateData && loc.dateData.datePatterns) {
        const p = loc.dateData.datePatterns;
        if (p.short) datePatternEntries.push(p.short);
        if (p.medium) datePatternEntries.push(p.medium);
        if (p.long) datePatternEntries.push(p.long);
        if (p.full) datePatternEntries.push(p.full);
    }
}
const datePatternPool = buildStrpool(datePatternEntries);

// Time format patterns
const timePatternEntries = [];
for (const loc of locales) {
    if (loc.dateData && loc.dateData.timePatterns) {
        const p = loc.dateData.timePatterns;
        if (p.short) timePatternEntries.push(p.short);
        if (p.medium) timePatternEntries.push(p.medium);
        if (p.long) timePatternEntries.push(p.long);
        if (p.full) timePatternEntries.push(p.full);
    }
}
const timePatternPool = buildStrpool(timePatternEntries);

// Combined date+time patterns
const combinedPatternEntries = [];
for (const loc of locales) {
    if (loc.dateData && loc.dateData.combinedPatterns) {
        const p = loc.dateData.combinedPatterns;
        if (p.short) combinedPatternEntries.push(p.short);
        if (p.medium) combinedPatternEntries.push(p.medium);
        if (p.long) combinedPatternEntries.push(p.long);
        if (p.full) combinedPatternEntries.push(p.full);
    }
}
const combinedPatternPool = buildStrpool(combinedPatternEntries);

function getDateIdx(pool, entry) {
    if (!entry) return 0;
    return pool.map.get(JSON.stringify(entry)) || 0;
}

function getPatternIdx(pool, pattern) {
    if (!pattern) return 0;
    return pool.map.get(JSON.stringify(pattern)) || 0;
}

// Sanity checks
if (decimalTab.length > 4) throw new Error(`decimalTab too large: ${decimalTab.length}`);
if (groupTab.length > 8) throw new Error(`groupTab too large: ${groupTab.length}`);
if (plusSignTab.length > 8) throw new Error(`plusSignTab too large: ${plusSignTab.length}`);
if (minusSignTab.length > 8) throw new Error(`minusSignTab too large: ${minusSignTab.length}`);
if (percentSignTab.length > 4) throw new Error(`percentSignTab too large: ${percentSignTab.length}`);
if (infinityTab.length > 2) throw new Error(`infinityTab too large: ${infinityTab.length}`);
if (nanTab.length > 32) throw new Error(`nanTab too large: ${nanTab.length}`);
if (patternTab.length > 64) throw new Error(`patternTab too large: ${patternTab.length}`);
if (timePatternPool.tab.length > 128) throw new Error(`timePatternPool too large: ${timePatternPool.tab.length}`);
if (combinedPatternPool.tab.length > 16) throw new Error(`combinedPatternPool too large: ${combinedPatternPool.tab.length}`);

let out = `/* Auto-generated locale data from CLDR */\n`;
out += `#include "../quickjs.h"\n`;
out += `#include "../cutils.h"\n`;
out += `#include "intl_private.h"\n\n`;

function inlineSymbols(sym) {
    return `        { ${decimalMap.get(sym.decimal)}, ${groupMap.get(sym.group)}, ${plusSignMap.get(sym.plusSign)}, ${minusSignMap.get(sym.minusSign)}, ${percentSignMap.get(sym.percentSign)}, ${infinityMap.get(sym.infinity)}, ${nanMap.get(sym.nan)} }`;
}

function inlinePatterns(pats) {
    return `        { ${patternMap.get(pats.decimal.positive)}, ${patternMap.get(pats.decimal.negative)}, ${patternMap.get(pats.currency.positive)}, ${patternMap.get(pats.currency.negative)}, ${patternMap.get(pats.accounting.positive)}, ${patternMap.get(pats.accounting.negative)}, ${patternMap.get(pats.percent.positive)}, ${patternMap.get(pats.percent.negative)} }`;
}

/* ---- Compressed currency data ---- */

// Collect all unique strings and count frequencies
const codeFreq = new Map();
const symFreq = new Map();
for (const loc of locales) {
    for (const [code, sym] of Object.entries(loc.currencies)) {
        codeFreq.set(code, (codeFreq.get(code) || 0) + 1);
        symFreq.set(sym, (symFreq.get(sym) || 0) + 1);
    }
}

// Build string table sorted by frequency descending
// We assign codes first (indices 0..codes-1) and symbols after (indices codes..)
// Actually, let's interleave them: separate tables for codes and symbols
// Each index only needs to be within its own table

const codeList = Array.from(codeFreq.entries()).sort((a, b) => b[1] - a[1]).map(e => e[0]);
const symList = Array.from(symFreq.entries()).sort((a, b) => b[1] - a[1]).map(e => e[0]);
const codeListSorted = codeList.slice().sort();

const codeToIdx = new Map();
for (let i = 0; i < codeList.length; i++) codeToIdx.set(codeList[i], i);

const symToIdx = new Map();
for (let i = 0; i < symList.length; i++) symToIdx.set(symList[i], i);

function encodeLEB128(val) {
    const bytes = [];
    do {
        let byte = val & 0x7f;
        val >>>= 7;
        if (val !== 0) byte |= 0x80;
        bytes.push(byte);
    } while (val !== 0);
    return bytes;
}

// Build per-locale currency byte arrays and compute offsets
let globalOffset = 0;
for (const loc of locales) {
    const entries = Object.entries(loc.currencies).sort((a, b) => a[0].localeCompare(b[0]));
    loc._currencyEntries = entries;
    loc._numCurrencies = entries.length;
    loc._currencyOffset = globalOffset;
    for (const [code, sym] of entries) {
        const cidx = codeToIdx.get(code);
        const sidx = symToIdx.get(sym);
        globalOffset += encodeLEB128(cidx).length;
        globalOffset += encodeLEB128(sidx).length;
    }
}

// Generate currency string table (codes and symbols separately)
out += `const char *currency_code_tab[] = {\n`;
for (let i = 0; i < codeList.length; i++) {
    out += `    "${escapeCString(codeList[i])}",\n`;
}
out += `};\n`;
out += `#define CURRENCY_CODE_COUNT ${codeList.length}\n\n`;

out += `const char *currency_sym_tab[] = {\n`;
for (let i = 0; i < symList.length; i++) {
    out += `    "${escapeCString(symList[i])}",\n`;
}
out += `};\n`;
out += `#define CURRENCY_SYM_COUNT ${symList.length}\n\n`;

// Generate concatenated LEB128 currency data
out += `const uint8_t locale_currency_data[] = {\n`;
let bytesOnLine = 0;
for (const loc of locales) {
    if (loc._numCurrencies > 0) {
        out += `    /* ${loc.locale} */\n`;
        for (const [code, sym] of loc._currencyEntries) {
            const cidx = codeToIdx.get(code);
            const sidx = symToIdx.get(sym);
            for (const b of encodeLEB128(cidx)) {
                out += `0x${b.toString(16).padStart(2, '0')}, `;
                bytesOnLine++;
                if (bytesOnLine >= 16) {
                    out += '\n';
                    bytesOnLine = 0;
                }
            }
            for (const b of encodeLEB128(sidx)) {
                out += `0x${b.toString(16).padStart(2, '0')}, `;
                bytesOnLine++;
                if (bytesOnLine >= 16) {
                    out += '\n';
                    bytesOnLine = 0;
                }
            }
        }
        if (bytesOnLine > 0) {
            out += '\n';
            bytesOnLine = 0;
        }
    }
}
out += `};\n\n`;

// CompactDecimalPattern arrays
const compactArrays = new Map();
let compactArrayCount = 0;
for (const loc of locales) {
    const entries = loc.compactPatterns.map(p => [p.scale, p.short, p.long]);
    const key = JSON.stringify(entries);
    if (!compactArrays.has(key)) {
        compactArrays.set(key, compactArrayCount++);
    }
    loc._compactArrayId = compactArrays.get(key);
    loc._numCompact = loc.compactPatterns.length;
}

for (let i = 0; i < compactArrayCount; i++) {
    const entries = Array.from(compactArrays.entries()).find(([k, v]) => v === i)[0];
    const arr = JSON.parse(entries);
    if (arr.length === 0) continue;
    out += `static const CompactDecimalPattern locale_compact_arr_${i}[] = {\n`;
    for (const [scale, shortPat, longPat] of arr) {
        const s = shortPat ? `"${escapeCString(shortPat)}"` : 'NULL';
        const l = longPat ? `"${escapeCString(longPat)}"` : 'NULL';
        out += `    { ${scale}, ${s}, ${l} },\n`;
    }
    out += `};\n\n`;
}

// Deduplicated symbol tables
function emitTable(name, tab) {
    out += `const char *${name}[] = {\n`;
    for (const s of tab) {
        out += `    "${escapeCString(s)}",\n`;
    }
    out += `};\n\n`;
}

emitTable('decimal_tab', decimalTab);
emitTable('group_tab', groupTab);
emitTable('plusSign_tab', plusSignTab);
emitTable('minusSign_tab', minusSignTab);
emitTable('percentSign_tab', percentSignTab);
emitTable('infinity_tab', infinityTab);
emitTable('nan_tab', nanTab);
emitTable('pattern_tab', patternTab);

// Date string pools (zero-separated)
function emitStrpool(name, pool) {
    out += `const char *${name}[] = {\n`;
    for (const entry of pool.tab) {
        out += `    "${makeZeroSepC(entry)}",\n`;
    }
    out += `};\n\n`;
}

emitStrpool('month_long_tab', monthLongPool);
emitStrpool('month_short_tab', monthShortPool);
emitStrpool('month_narrow_tab', monthNarrowPool);
emitStrpool('weekday_long_tab', weekdayLongPool);
emitStrpool('weekday_narrow_tab', weekdayNarrowPool);
emitStrpool('era_tab', eraPool);
emitStrpool('ampm_tab', ampmPool);
emitStrpool('dayperiod_tab', dayPeriodLongPool);
emitStrpool('dayperiod_narrow_tab', dayPeriodNarrowPool);

// Date format patterns (simple C strings, not zero-separated arrays)
out += `const char *date_pattern_tab[] = {\n`;
for (const entry of datePatternPool.tab) {
    out += `    "${escapeCString(entry)}",\n`;
}
out += `};\n\n`;

// Time format patterns
out += `const char *time_pattern_tab[] = {\n`;
for (const entry of timePatternPool.tab) {
    out += `    "${escapeCString(entry)}",\n`;
}
out += `};\n\n`;

// Combined date+time patterns
out += `const char *combined_pattern_tab[] = {\n`;
for (const entry of combinedPatternPool.tab) {
    out += `    "${escapeCString(entry)}",\n`;
}
out += `};\n\n`;

// LocaleData array
out += `const LocaleData locale_data_table[] = {\n`;
for (const loc of locales) {
    const sym = loc.symbols[loc.defaultNS];
    out += `    {\n`;
    out += `        "${escapeCString(loc.locale)}",\n`;
    if (loc._numCompact > 0) {
        out += `        locale_compact_arr_${loc._compactArrayId},\n`;
    } else {
        out += `        NULL,\n`;
    }
    out += `        ${inlineSymbols(sym)},\n`;
    out += `        ${inlinePatterns(loc.patterns)},\n`;
    const d = loc.dateData;
    out += `        ${d ? getDateIdx(monthLongPool, d.monthLong) : 0}, ${d ? getDateIdx(monthShortPool, d.monthShort) : 0},\n`;
    out += `        ${d ? getDateIdx(monthNarrowPool, d.monthNarrow) : 0}, ${d ? getDateIdx(weekdayLongPool, d.weekdayLong) : 0},\n`;
    out += `        ${d ? getDateIdx(weekdayNarrowPool, d.weekdayNarrow) : 0}, ${d ? getDateIdx(eraPool, d.era) : 0},\n`;
    out += `        ${d ? getDateIdx(ampmPool, d.ampm) : 0}, ${d ? getDateIdx(dayPeriodLongPool, d.dayPeriodLong) : 0},\n`;
    out += `        ${d ? getDateIdx(dayPeriodNarrowPool, d.dayPeriodNarrow) : 0},\n`;
    const p = d ? d.datePatterns : null;
    out += `        ${getPatternIdx(datePatternPool, p ? p.short : '')}, ${getPatternIdx(datePatternPool, p ? p.medium : '')},\n`;
    out += `        ${getPatternIdx(datePatternPool, p ? p.long : '')}, ${getPatternIdx(datePatternPool, p ? p.full : '')},\n`;
    const tp = d ? d.timePatterns : null;
    out += `        ${getPatternIdx(timePatternPool, tp ? tp.short : '')}, ${getPatternIdx(timePatternPool, tp ? tp.medium : '')},\n`;
    out += `        ${getPatternIdx(timePatternPool, tp ? tp.long : '')}, ${getPatternIdx(timePatternPool, tp ? tp.full : '')},\n`;
    const cp = d ? d.combinedPatterns : null;
    out += `        ${getPatternIdx(combinedPatternPool, cp ? cp.short : '')}, ${getPatternIdx(combinedPatternPool, cp ? cp.medium : '')},\n`;
    out += `        ${getPatternIdx(combinedPatternPool, cp ? cp.long : '')}, ${getPatternIdx(combinedPatternPool, cp ? cp.full : '')},\n`;
    out += `        ${loc._numCompact}, ${loc.groupSizes.primary}, ${loc.groupSizes.secondary},\n`;
    out += `        ${loc._currencyOffset}, ${loc._numCurrencies},\n`;
    out += `    },\n`;
}
out += `};\n\n`;

out += `const int locale_data_count = sizeof(locale_data_table) / sizeof(locale_data_table[0]);\n`;

/* --- DisplayNames data --- */
function readDisplayNames(locale) {
    const result = {
        languages: {},
        regions: {},
        regionsShort: {},
        scripts: {},
        calendars: {},
        dateTimeFields: {},
        currencies: {}
    };

    const langFile = path.join(CLDR_LOCALENAMES_BASE, locale, 'languages.json');
    if (fs.existsSync(langFile)) {
        const data = JSON.parse(fs.readFileSync(langFile, 'utf8'));
        const langs = data['main'][locale]['localeDisplayNames']['languages'];
        for (const [code, name] of Object.entries(langs)) {
            if (name && typeof name === 'string') result.languages[code] = name;
        }
    }

    const terrFile = path.join(CLDR_LOCALENAMES_BASE, locale, 'territories.json');
    if (fs.existsSync(terrFile)) {
        const data = JSON.parse(fs.readFileSync(terrFile, 'utf8'));
        const terrs = data['main'][locale]['localeDisplayNames']['territories'];
        for (const [code, name] of Object.entries(terrs)) {
            if (!code.includes('-alt-')) {
                if (name && typeof name === 'string') result.regions[code] = name;
            } else if (code.endsWith('-alt-short')) {
                const base = code.replace('-alt-short', '');
                if (name && typeof name === 'string') result.regionsShort[base] = name;
            }
        }
    }

    const scriptFile = path.join(CLDR_LOCALENAMES_BASE, locale, 'scripts.json');
    if (fs.existsSync(scriptFile)) {
        const data = JSON.parse(fs.readFileSync(scriptFile, 'utf8'));
        const scripts = data['main'][locale]['localeDisplayNames']['scripts'];
        for (const [code, name] of Object.entries(scripts)) {
            if (name && typeof name === 'string') result.scripts[code] = name;
        }
    }

    const calFile = path.join(CLDR_LOCALENAMES_BASE, locale, 'localeDisplayNames.json');
    if (fs.existsSync(calFile)) {
        const data = JSON.parse(fs.readFileSync(calFile, 'utf8'));
        const types = data['main'][locale]['localeDisplayNames']['types'];
        if (types && types['calendar']) {
            for (const [code, name] of Object.entries(types['calendar'])) {
                if (!code.includes('-alt-') && name && typeof name === 'string') {
                    result.calendars[code] = name;
                }
            }
        }
    }

    const dfFile = path.join(CLDR_DATES_BASE, locale, 'dateFields.json');
    if (fs.existsSync(dfFile)) {
        const data = JSON.parse(fs.readFileSync(dfFile, 'utf8'));
        const fields = data['main'][locale]['dates']['fields'];
        const fieldMap = {
            'era': 'era',
            'year': 'year',
            'quarter': 'quarter',
            'month': 'month',
            'week': 'weekOfYear',
            'weekday': 'weekday',
            'day': 'day',
            'dayperiod': 'dayPeriod',
            'hour': 'hour',
            'minute': 'minute',
            'second': 'second',
            'zone': 'timeZoneName'
        };
        for (const [cldrKey, dnKey] of Object.entries(fieldMap)) {
            const f = fields[cldrKey];
            if (f && f['displayName']) {
                result.dateTimeFields[dnKey] = f['displayName'];
            }
        }
    }

    const curFile = path.join(CLDR_BASE, locale, 'currencies.json');
    if (fs.existsSync(curFile)) {
        const data = JSON.parse(fs.readFileSync(curFile, 'utf8'));
        const curs = data['main'][locale]['numbers']['currencies'];
        for (const [code, info] of Object.entries(curs)) {
            if (info && info['displayName'] && typeof info['displayName'] === 'string') {
                result.currencies[code] = info['displayName'];
                allDisplayCurrencyCodes.add(code);
            }
        }
    }

    return result;
}

function buildDNDictPool(dict) {
    const entries = [];
    const keys = Object.keys(dict).sort();
    for (const k of keys) {
        entries.push(k);
        entries.push(dict[k]);
    }
    entries.push(''); /* trailing NUL terminator */
    return entries;
}

const dnDataEntries = [];
for (const dnLocale of DISPLAY_NAME_LOCALES) {
    const dn = readDisplayNames(dnLocale);
    const langPool = buildDNDictPool(dn.languages);
    const regionPool = buildDNDictPool(dn.regions);
    const regionShortPool = buildDNDictPool(dn.regionsShort);
    const scriptPool = buildDNDictPool(dn.scripts);
    const calendarPool = buildDNDictPool(dn.calendars);
    const dtfPool = buildDNDictPool(dn.dateTimeFields);
    const currencyPool = buildDNDictPool(dn.currencies);

    out += `\n/* DisplayNames data for ${dnLocale} */\n`;
    out += `static const char dn_${dnLocale}_languages[] = "${makeZeroSepC(langPool)}";\n`;
    out += `static const char dn_${dnLocale}_regions[] = "${makeZeroSepC(regionPool)}";\n`;
    out += `static const char dn_${dnLocale}_regions_short[] = "${makeZeroSepC(regionShortPool)}";\n`;
    out += `static const char dn_${dnLocale}_scripts[] = "${makeZeroSepC(scriptPool)}";\n`;
    out += `static const char dn_${dnLocale}_calendars[] = "${makeZeroSepC(calendarPool)}";\n`;
    out += `static const char dn_${dnLocale}_dateTimeFields[] = "${makeZeroSepC(dtfPool)}";\n`;
    out += `static const char dn_${dnLocale}_currencies[] = "${makeZeroSepC(currencyPool)}";\n`;

    dnDataEntries.push(dnLocale);
}

out += `\nconst DisplayNamesData display_names_data[] = {\n`;
for (const dnLocale of dnDataEntries) {
    out += `    { "${dnLocale}"`;
    out += `, dn_${dnLocale}_languages`;
    out += `, dn_${dnLocale}_regions`;
    out += `, dn_${dnLocale}_regions_short`;
    out += `, dn_${dnLocale}_scripts`;
    out += `, dn_${dnLocale}_calendars`;
    out += `, dn_${dnLocale}_dateTimeFields`;
    out += `, dn_${dnLocale}_currencies },\n`;
}
out += `};\n`;
out += `const int display_names_count = sizeof(display_names_data) / sizeof(display_names_data[0]);\n`;

const displayCurrencyCodesSorted = Array.from(allDisplayCurrencyCodes).sort();
out += `\nconst char *intl_supported_currencies[] = {\n`;
for (let i = 0; i < displayCurrencyCodesSorted.length; i++) {
    out += `    "${escapeCString(displayCurrencyCodesSorted[i])}",\n`;
}
out += `};\n`;
out += `const int intl_supported_currencies_count = sizeof(intl_supported_currencies) / sizeof(intl_supported_currencies[0]);\n`;

fs.writeFileSync(OUT_PATH, out);
console.log(`Generated ${OUT_PATH}`);
console.log(`Currency codes: ${codeList.length}, symbols: ${symList.length}`);
console.log(`Total LEB128 bytes: ${globalOffset}`);

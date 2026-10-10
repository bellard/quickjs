/*
 * QuickJS Intl.Segmenter implementation
 */

#include <string.h>
#include <ctype.h>

#include "../quickjs.h"
#include "../cutils.h"
#include "../libunicode.h"
#include "intl_private.h"

static JSClassID js_segmenter_class_id;
static JSClassID js_segments_class_id;
static JSClassID js_segment_iterator_class_id;

enum {
    GRANULARITY_GRAPHEME = 0,
    GRANULARITY_WORD,
    GRANULARITY_SENTENCE,
};

static const char *granularity_names[] = {
    "grapheme", "word", "sentence"
};

typedef struct {
    char *locale;
    int granularity;
} JSSegmenterData;

typedef struct {
    JSValue segmenter;   /* strong ref */
    JSValue string;      /* strong ref */
} JSSegmentsData;

typedef struct {
    JSValue segmenter;   /* strong ref */
    JSValue string;      /* strong ref */
    int next_index;      /* next code unit index */
} JSSegmentIteratorData;

static void js_segmenter_finalizer(JSRuntime *rt, JSValue val)
{
    JSSegmenterData *s = JS_GetOpaque(val, js_segmenter_class_id);
    if (s) {
        js_free_rt(rt, s->locale);
        js_free_rt(rt, s);
    }
}

static void js_segments_finalizer(JSRuntime *rt, JSValue val)
{
    JSSegmentsData *s = JS_GetOpaque(val, js_segments_class_id);
    if (s) {
        JS_FreeValueRT(rt, s->segmenter);
        JS_FreeValueRT(rt, s->string);
        js_free_rt(rt, s);
    }
}

static void js_segments_mark(JSRuntime *rt, JSValueConst val,
                              JS_MarkFunc *mark_func)
{
    JSSegmentsData *s = JS_GetOpaque(val, js_segments_class_id);
    if (s) {
        JS_MarkValue(rt, s->segmenter, mark_func);
        JS_MarkValue(rt, s->string, mark_func);
    }
}

static void js_segment_iterator_finalizer(JSRuntime *rt, JSValue val)
{
    JSSegmentIteratorData *s = JS_GetOpaque(val, js_segment_iterator_class_id);
    if (s) {
        JS_FreeValueRT(rt, s->segmenter);
        JS_FreeValueRT(rt, s->string);
        js_free_rt(rt, s);
    }
}

static void js_segment_iterator_mark(JSRuntime *rt, JSValueConst val,
                                      JS_MarkFunc *mark_func)
{
    JSSegmentIteratorData *s = JS_GetOpaque(val, js_segment_iterator_class_id);
    if (s) {
        JS_MarkValue(rt, s->segmenter, mark_func);
        JS_MarkValue(rt, s->string, mark_func);
    }
}

static JSClassDef js_segmenter_class = {
    "Segmenter",
    .finalizer = js_segmenter_finalizer,
};

static JSClassDef js_segments_class = {
    "Segments",
    .finalizer = js_segments_finalizer,
    .gc_mark = js_segments_mark,
};

static JSClassDef js_segment_iterator_class = {
    "SegmentIterator",
    .finalizer = js_segment_iterator_finalizer,
    .gc_mark = js_segment_iterator_mark,
};

/* --------------------------------------------------------------------- */
/* Segmenter constructor                                                 */
/* --------------------------------------------------------------------- */

static JSValue js_segmenter_constructor(JSContext *ctx,
                                         JSValueConst new_target,
                                         int argc, JSValueConst *argv)
{
    JSSegmenterData *s;
    JSValue obj = JS_UNDEFINED;
    JSValue options = JS_UNDEFINED;
    int granularity_idx;
    s = js_mallocz(ctx, sizeof(*s));
    if (!s)
        return JS_EXCEPTION;

    s->locale = js_strdup(ctx, "en-US");

    {
        static const char *available[] = {
            "ar", "de", "en", "fr", "hi", "it", "ja", "ko", "sr", "zh"
        };
        char **locales = NULL;
        int count = 0;
        int k;
        if (js_canonicalizeLocaleList_c(ctx, argc > 0 ? argv[0] : JS_UNDEFINED,
                                         &locales, &count) < 0)
            goto fail;
        for (k = 0; k < count; k++) {
            const char *str = locales[k];
            size_t i;
            for (i = 0; i < countof(available); i++) {
                size_t alen = strlen(available[i]);
                if (!strncmp(str, available[i], alen) &&
                    (str[alen] == '\0' || str[alen] == '-')) {
                    js_free(ctx, s->locale);
                    s->locale = js_strdup(ctx, str);
                    break;
                }
            }
            if (i < countof(available))
                break;
        }
        js_canonicalizeLocaleList_free_c(ctx, locales, count);
    }

    if (argc > 1 && !JS_IsUndefined(argv[1]))
        options = JS_ToObject(ctx, argv[1]);
    else
        options = JS_NewObjectProto(ctx, JS_NULL);
    if (JS_IsException(options))
        goto fail;

    /* Read localeMatcher first (spec order) */
    {
        int idx = js_intl_get_string_option(ctx, options, "localeMatcher",
                                             intl_localeMatcher_names,
                                             countof(intl_localeMatcher_names),
                                             INTL_LOCALEMATCHER_BESTFIT);
        if (idx < 0)
            goto fail;
    }

    granularity_idx = js_intl_get_string_option(ctx, options, "granularity",
                                                 granularity_names,
                                                 countof(granularity_names),
                                                 GRANULARITY_GRAPHEME);
    if (granularity_idx < 0)
        goto fail;

    JS_FreeValue(ctx, options);
    options = JS_UNDEFINED;

    s->granularity = granularity_idx;

    obj = JS_CreateFromConstructor(ctx, new_target, js_segmenter_class_id);
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

/* --------------------------------------------------------------------- */
/* Helper: create a segment data object                                  */
/* --------------------------------------------------------------------- */

static JSValue create_segment_data(JSContext *ctx, JSValueConst str_val,
                                    int index, int segment_len, int is_word_like,
                                    int include_is_word_like)
{
    JSValue obj = JS_NewObject(ctx);
    JSValue seg_str;
    if (JS_IsException(obj))
        return JS_EXCEPTION;

    seg_str = js_sub_string_val(ctx, str_val, index, index + segment_len);
    if (JS_IsException(seg_str)) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    JS_DefinePropertyValueStr(ctx, obj, "segment", seg_str, JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "index",
                               JS_NewInt32(ctx, index), JS_PROP_C_W_E);
    {
        JSValue input_str = JS_DupValue(ctx, str_val);
        JS_DefinePropertyValueStr(ctx, obj, "input", input_str, JS_PROP_C_W_E);
    }
    if (include_is_word_like) {
        JS_DefinePropertyValueStr(ctx, obj, "isWordLike",
                                   JS_NewBool(ctx, is_word_like), JS_PROP_C_W_E);
    }
    return obj;
}

/* --------------------------------------------------------------------- */
/* Segments helper: find segment boundaries                              */
/* --------------------------------------------------------------------- */

/* Returns 1 if c is "word-like" (letter, digit, non-ASCII, or some punct) */
static int is_word_char(int c)
{
    return isalnum(c) || c > 127 ||
           c == '.' || c == '-' || c == '+' ||
           c == '%' || c == '$' || c == '_' || c == '\'';
}

/* Determine if a code point should be part of the current grapheme cluster.
   Returns 1 if c combines with the preceding base or extender. */
static int is_grapheme_extender(int prev_c, int c)
{
    if (unicode_get_cc((uint32_t)c) > 0)
        return 1;
    /* Thai vowels and marks with combining class 0 */
    if ((c >= 0x0E31 && c <= 0x0E3A) || (c >= 0x0E47 && c <= 0x0E4E))
        return 1;
    /* Emoji skin tone modifiers */
    if (c >= 0x1F3FB && c <= 0x1F3FF)
        return 1;
    /* Variation selectors */
    if (c >= 0xFE00 && c <= 0xFE0F)
        return 1;
    /* ZWJ - combines to form emoji sequences */
    if (c == 0x200D)
        return 1;
    /* After ZWJ, consume the next char (emoji sequence continuation) */
    if (prev_c == 0x200D)
        return 1;
    /* Regional indicators form flag pairs */
    if (prev_c >= 0x1F1E6 && prev_c <= 0x1F1FF &&
        c >= 0x1F1E6 && c <= 0x1F1FF)
        return 1;
    return 0;
}

/* Get Hangul Jamo type: 0=none, 1=L, 2=V, 3=LV, 4=LVT, 5=T */
static int hangul_jamo_type(int c)
{
    if (c >= 0x1100 && c <= 0x115F)
        return 1; /* L */
    if (c >= 0x1160 && c <= 0x11A7)
        return 2; /* V */
    if (c >= 0x11A8 && c <= 0x11FF)
        return 5; /* T */
    if (c >= 0xAC00 && c <= 0xD7A3) {
        int t = (c - 0xAC00) % 28;
        return t == 0 ? 3 : 4; /* LV or LVT */
    }
    return 0;
}

/* Check if two Hangul characters should be in the same cluster */
static int hangul_combine(int prev_type, int type)
{
    if (prev_type == 1 && (type == 1 || type == 2 || type == 3 || type == 4)) /* L × (L|V|LV|LVT) */
        return 1;
    if (prev_type == 2 && (type == 2 || type == 5)) /* V × (V|T) */
        return 1;
    if (prev_type == 3 && (type == 2 || type == 5)) /* LV × (V|T) */
        return 1;
    if (prev_type == 4 && type == 5) /* LVT × T */
        return 1;
    if (prev_type == 5 && type == 5) /* T × T */
        return 1;
    return 0;
}

/* --------------------------------------------------------------------- */
/* Segments prototype                                                    */
/* --------------------------------------------------------------------- */

static JSValue js_segments_containing(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    JSSegmentsData *seg = JS_GetOpaque2(ctx, this_val, js_segments_class_id);
    JSSegmenterData *s;
    JSValue str_val;
    int len, pos, start, segment_len, is_word_like;

    if (!seg)
        return JS_EXCEPTION;

    s = JS_GetOpaque(seg->segmenter, js_segmenter_class_id);
    if (!s)
        return JS_EXCEPTION;

    str_val = JS_DupValue(ctx, seg->string);
    len = js_string_get_len(str_val);

    /* ToInteger(index) */
    if (JS_ToInt32Sat(ctx, &pos, argv[0]) < 0) {
        JS_FreeValue(ctx, str_val);
        return JS_EXCEPTION;
    }

    if (pos < 0 || pos >= len) {
        JS_FreeValue(ctx, str_val);
        return JS_UNDEFINED;
    }

    if (s->granularity == GRANULARITY_SENTENCE) {
        JS_FreeValue(ctx, str_val);
        return create_segment_data(ctx, str_val, 0, len, 0, 0);
    }

    /* For grapheme and word, find the segment containing pos */
    {
        int idx = 0;
        start = 0;
        segment_len = 0;
        is_word_like = 0;

        if (s->granularity == GRANULARITY_GRAPHEME) {
            while (idx < len) {
                int seg_start = idx;
                int c = js_string_getc(str_val, &idx);
                int prev_c = c;
                int prev_type = hangul_jamo_type(c);
                while (idx < len) {
                    int next_idx = idx;
                    int c2 = js_string_getc(str_val, &next_idx);
                    int type2 = hangul_jamo_type(c2);
                    if (hangul_combine(prev_type, type2) ||
                        is_grapheme_extender(prev_c, c2)) {
                        prev_c = c2;
                        prev_type = type2;
                        idx = next_idx;
                    } else {
                        break;
                    }
                }
                if (seg_start <= pos && pos < idx) {
                    start = seg_start;
                    segment_len = idx - seg_start;
                    break;
                }
            }
        } else { /* word */
            int first_word = -1;
            idx = 0;
            start = 0;
            while (idx < len) {
                int c = js_string_getc(str_val, &idx);
                int w = is_word_char(c);
                if (first_word < 0)
                    first_word = w;
                if (idx < len) {
                    int idx_after_c = idx;
                    int next_c = js_string_getc(str_val, &idx);
                    int next_w = is_word_char(next_c);
                    int is_lone_surrogate = (c >= 0xD800 && c <= 0xDFFF) || (next_c >= 0xD800 && next_c <= 0xDFFF);
                    idx = idx_after_c; /* restore after peek */
                    if (w != next_w || is_lone_surrogate) {
                        if (start <= pos && pos < idx) {
                            segment_len = idx - start;
                            is_word_like = first_word;
                            break;
                        }
                        start = idx;
                        first_word = next_w;
                    }
                } else {
                    segment_len = idx - start;
                    is_word_like = first_word;
                    break;
                }
            }
        }
        {
            JSValue res = create_segment_data(ctx, str_val, start, segment_len,
                                               is_word_like,
                                               s->granularity == GRANULARITY_WORD);
            JS_FreeValue(ctx, str_val);
            return res;
        }
    }
}

static JSValue js_segments_iterator(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    JSSegmentsData *seg = JS_GetOpaque2(ctx, this_val, js_segments_class_id);
    JSValue obj = JS_UNDEFINED;
    JSValue proto;
    JSSegmentIteratorData *it;

    if (!seg)
        return JS_EXCEPTION;

    it = js_mallocz(ctx, sizeof(*it));
    if (!it)
        return JS_EXCEPTION;

    it->segmenter = JS_DupValue(ctx, seg->segmenter);
    it->string = JS_DupValue(ctx, seg->string);
    it->next_index = 0;

    proto = JS_GetClassProto(ctx, js_segment_iterator_class_id);
    obj = JS_NewObjectProtoClass(ctx, proto, js_segment_iterator_class_id);
    JS_FreeValue(ctx, proto);
    if (JS_IsException(obj))
        goto fail;
    JS_SetOpaque(obj, it);
    return obj;
 fail:
    JS_FreeValue(ctx, it->segmenter);
    JS_FreeValue(ctx, it->string);
    js_free(ctx, it);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static const JSCFunctionListEntry js_segments_proto_funcs[] = {
    JS_CFUNC_DEF("containing", 1, js_segments_containing),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_segments_iterator),
};

/* --------------------------------------------------------------------- */
/* SegmentIterator prototype                                             */
/* --------------------------------------------------------------------- */

static JSValue js_segment_iterator_next(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv,
                                         int *pdone, int magic)
{
    JSSegmentIteratorData *it = JS_GetOpaque2(ctx, this_val, js_segment_iterator_class_id);
    JSSegmenterData *s;
    JSValue str_val;
    int len, start, segment_len, is_word_like;
    JSValue value;

    if (!it)
        return JS_EXCEPTION;

    s = JS_GetOpaque(it->segmenter, js_segmenter_class_id);
    if (!s)
        return JS_EXCEPTION;

    str_val = JS_DupValue(ctx, it->string);
    len = js_string_get_len(str_val);

    if (it->next_index >= len) {
        JS_FreeValue(ctx, str_val);
        *pdone = 1;
        return JS_UNDEFINED;
    }

    if (s->granularity == GRANULARITY_SENTENCE) {
        start = 0;
        segment_len = len;
        is_word_like = 0;
    } else if (s->granularity == GRANULARITY_GRAPHEME) {
        int idx = it->next_index;
        start = idx;
        int c = js_string_getc(str_val, &idx);
        int prev_c = c;
        int prev_type = hangul_jamo_type(c);
        while (idx < len) {
            int next_idx = idx;
            int c2 = js_string_getc(str_val, &next_idx);
            int type2 = hangul_jamo_type(c2);
            if (hangul_combine(prev_type, type2) ||
                is_grapheme_extender(prev_c, c2)) {
                prev_c = c2;
                prev_type = type2;
                idx = next_idx;
            } else {
                break;
            }
        }
        segment_len = idx - start;
        is_word_like = 0;
    } else { /* word */
        int idx = it->next_index;
        int c = js_string_getc(str_val, &idx);
        int first_word = is_word_char(c);
        start = it->next_index;
        while (idx < len) {
            int prev_idx = idx;
            int c2 = js_string_getc(str_val, &idx);
            if (is_word_char(c2) != first_word) {
                idx = prev_idx;
                break;
            }
        }
        segment_len = idx - start;
        is_word_like = first_word;
    }

    it->next_index = start + segment_len;

    value = create_segment_data(ctx, str_val, start, segment_len, is_word_like,
                                 s->granularity == GRANULARITY_WORD);
    JS_FreeValue(ctx, str_val);

    if (JS_IsException(value))
        return JS_EXCEPTION;

    *pdone = 0;
    return value;
}

static const JSCFunctionListEntry js_segment_iterator_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_segment_iterator_next, 0),
};

/* --------------------------------------------------------------------- */
/* Segmenter prototype methods                                           */
/* --------------------------------------------------------------------- */

static JSValue js_segmenter_segment(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    JSSegmenterData *s = JS_GetOpaque2(ctx, this_val, js_segmenter_class_id);
    JSValue str_val, obj = JS_UNDEFINED, proto;
    JSSegmentsData *seg;

    if (!s)
        return JS_EXCEPTION;

    /* 3. Let string be ? ToString(string). */
    str_val = JS_ToString(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
    if (JS_IsException(str_val))
        return JS_EXCEPTION;

    seg = js_mallocz(ctx, sizeof(*seg));
    if (!seg) {
        JS_FreeValue(ctx, str_val);
        return JS_EXCEPTION;
    }

    seg->segmenter = JS_DupValue(ctx, this_val);
    seg->string = str_val;

    proto = JS_GetClassProto(ctx, js_segments_class_id);
    obj = JS_NewObjectProtoClass(ctx, proto, js_segments_class_id);
    JS_FreeValue(ctx, proto);
    if (JS_IsException(obj))
        goto fail;
    JS_SetOpaque(obj, seg);
    return obj;
 fail:
    JS_FreeValue(ctx, seg->segmenter);
    JS_FreeValue(ctx, seg->string);
    js_free(ctx, seg);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static JSValue js_segmenter_resolvedOptions(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv)
{
    JSValue obj;
    JSSegmenterData *s = JS_GetOpaque2(ctx, this_val, js_segmenter_class_id);
    if (!s)
        return JS_EXCEPTION;

    obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        return JS_EXCEPTION;

    JS_DefinePropertyValueStr(ctx, obj, "locale",
                               JS_NewString(ctx, s->locale), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "granularity",
                               JS_NewString(ctx, granularity_names[s->granularity]), JS_PROP_C_W_E);

    return obj;
}

static JSValue js_segmenter_supportedLocalesOf(JSContext *ctx, JSValueConst this_val,
                                                   int argc, JSValueConst *argv)
{
    return js_intl_supportedLocalesOf(ctx, this_val, argc, argv);
}

static const JSCFunctionListEntry js_segmenter_proto_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Intl.Segmenter", JS_PROP_CONFIGURABLE),
    JS_CFUNC_DEF("segment", 1, js_segmenter_segment),
    JS_CFUNC_DEF("resolvedOptions", 0, js_segmenter_resolvedOptions),
};

static const JSCFunctionListEntry js_segmenter_static_funcs[] = {
    JS_CFUNC_DEF("supportedLocalesOf", 1, js_segmenter_supportedLocalesOf),
};

int js_add_segmenter(JSContext *ctx, JSValue intl_obj)
{
    JSValue segments_proto, segiter_proto;

    JS_NewClassID(&js_segmenter_class_id);
    JS_NewClassID(&js_segments_class_id);
    JS_NewClassID(&js_segment_iterator_class_id);

    JS_NewClass(JS_GetRuntime(ctx), js_segmenter_class_id, &js_segmenter_class);
    JS_NewClass(JS_GetRuntime(ctx), js_segments_class_id, &js_segments_class);
    JS_NewClass(JS_GetRuntime(ctx), js_segment_iterator_class_id, &js_segment_iterator_class);

    /* Segments prototype */
    segments_proto = JS_NewObject(ctx);
    if (JS_IsException(segments_proto))
        return -1;
    JS_SetPropertyFunctionList(ctx, segments_proto, js_segments_proto_funcs,
                               countof(js_segments_proto_funcs));
    JS_SetClassProto(ctx, js_segments_class_id, segments_proto);

    /* SegmentIterator prototype */
    segiter_proto = JS_NewObject(ctx);
    if (JS_IsException(segiter_proto)) {
        JS_FreeValue(ctx, segments_proto);
        return -1;
    }
    JS_SetPropertyFunctionList(ctx, segiter_proto, js_segment_iterator_proto_funcs,
                               countof(js_segment_iterator_proto_funcs));
    JS_SetClassProto(ctx, js_segment_iterator_class_id, segiter_proto);

    if (JS_IsException(js_intl_new_constructor(ctx, intl_obj,
                                                -1,
                                                js_segmenter_class_id, "Segmenter",
                                                (JSCFunction *)js_segmenter_constructor, 0,
                                                JS_CFUNC_constructor, 0,
                                                js_segmenter_static_funcs, countof(js_segmenter_static_funcs),
                                                js_segmenter_proto_funcs, countof(js_segmenter_proto_funcs)))) {
        return -1;
    }

    return 0;
}

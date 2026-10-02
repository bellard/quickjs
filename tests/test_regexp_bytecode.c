/*
 * RegExp bytecode validator tests
 *
 * Copyright (c) 2026 QuickJS contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cutils.h"
#include "libregexp.h"

typedef enum {
#define DEF(id, size) TEST_REOP_ ## id,
#include "libregexp-opcode.h"
#undef DEF
    TEST_REOP_COUNT,
} TestREOpcode;

#define TEST_RE_HEADER_FLAGS 0
#define TEST_RE_HEADER_CAPTURE_COUNT 2
#define TEST_RE_HEADER_REGISTER_COUNT 3
#define TEST_RE_HEADER_BYTECODE_LEN 4
#define TEST_RE_HEADER_LEN 8
#define TEST_RE_META_VERSION_OFFSET 0
#define TEST_RE_META_KIND_OFFSET 1
#define TEST_RE_META_ENCODING_OFFSET 2
#define TEST_RE_META_FLAGS_OFFSET 3
#define TEST_RE_META_PAYLOAD_SIZE_OFFSET 4
#define TEST_RE_META_LENGTH_OFFSET 8
#define TEST_RE_META_ENTRY_OFFSET 12
#define TEST_RE_META_EXEC_FLAGS_OFFSET 16
#define TEST_RE_META_LEADING_COUNT_OFFSET 20
#define TEST_RE_META_HEADER_LEN 24

int lre_check_stack_overflow(void *opaque, size_t alloca_size)
{
    return 0;
}

int lre_check_timeout(void *opaque)
{
    return 0;
}

void *lre_realloc(void *opaque, void *ptr, size_t size)
{
    return realloc(ptr, size);
}

static uint8_t *compile_regexp(const char *pattern, int flags, int *plen)
{
    char error[128];
    uint8_t *buf;

    buf = lre_compile(plen, error, sizeof(error), pattern, strlen(pattern),
                      flags, NULL);
    if (!buf) {
        fprintf(stderr, "compile failed for '%s': %s\n", pattern, error);
        abort();
    }
    return buf;
}

static void expect_valid(const uint8_t *buf, size_t len)
{
    char error[128];

    if (lre_validate_bytecode(buf, len, error, sizeof(error), NULL)) {
        fprintf(stderr, "expected valid bytecode: %s\n", error);
        abort();
    }
}

static void expect_invalid(const uint8_t *buf, size_t len)
{
    static int test_index;
    char error[128];

    test_index++;
    if (!lre_validate_bytecode(buf, len, error, sizeof(error), NULL)) {
        fprintf(stderr, "expected invalid bytecode in case %d\n", test_index);
        abort();
    }
    assert(error[0] != '\0');
}

static uint8_t *dup_bytecode(const uint8_t *buf, size_t len)
{
    uint8_t *copy = malloc(len);

    assert(copy != NULL);
    memcpy(copy, buf, len);
    return copy;
}

static size_t find_opcode(const uint8_t *buf, size_t len, int wanted)
{
    size_t code_len = get_u32(buf + TEST_RE_HEADER_BYTECODE_LEN);
    size_t pos = TEST_RE_HEADER_LEN;
    size_t end = pos + code_len;
    static const uint8_t sizes[TEST_REOP_COUNT] = {
#define DEF(id, size) size,
#include "libregexp-opcode.h"
#undef DEF
    };

    assert(end <= len);
    while (pos < end) {
        int opcode = buf[pos];
        size_t insn_len;

        assert(opcode > TEST_REOP_invalid && opcode < TEST_REOP_COUNT);
        if (opcode == wanted)
            return pos;
        insn_len = sizes[opcode];
        if (opcode == TEST_REOP_range || opcode == TEST_REOP_range_i)
            insn_len += get_u16(buf + pos + 1) * 4;
        else if (opcode == TEST_REOP_range32 || opcode == TEST_REOP_range32_i)
            insn_len += get_u16(buf + pos + 1) * 8;
        else if (opcode == TEST_REOP_back_reference ||
                 opcode == TEST_REOP_back_reference_i ||
                 opcode == TEST_REOP_backward_back_reference ||
                 opcode == TEST_REOP_backward_back_reference_i)
            insn_len += buf[pos + 1];
        pos += insn_len;
    }
    return SIZE_MAX;
}

static void test_valid_bytecode(void)
{
    static const struct {
        const char *pattern;
        int flags;
    } cases[] = {
        { "abc", 0 },
        { "[a-z]+", LRE_FLAG_GLOBAL },
        { "(?=abc)abc", 0 },
        { "(ab)\\1", 0 },
        { "(?<word>ab)+", LRE_FLAG_INDICES },
        { "[^a-z]", LRE_FLAG_UNICODE },
    };
    size_t i;

    for(i = 0; i < countof(cases); i++) {
        uint8_t *buf;
        int len;

        buf = compile_regexp(cases[i].pattern, cases[i].flags, &len);
        expect_valid(buf, len);
        free(buf);
    }
}

static void test_header_validation(void)
{
    uint8_t *buf, *copy;
    int len;

    buf = compile_regexp("abc", 0, &len);
    expect_invalid(buf, TEST_RE_HEADER_LEN - 1);

    copy = dup_bytecode(buf, len);
    put_u16(copy + TEST_RE_HEADER_FLAGS,
            get_u16(copy + TEST_RE_HEADER_FLAGS) | (1 << 15));
    expect_invalid(copy, len);
    free(copy);

    copy = dup_bytecode(buf, len);
    copy[TEST_RE_HEADER_CAPTURE_COUNT] = 0;
    expect_invalid(copy, len);
    free(copy);

    copy = dup_bytecode(buf, len);
    copy[TEST_RE_HEADER_CAPTURE_COUNT] = 2;
    put_u16(copy + TEST_RE_HEADER_FLAGS,
            get_u16(copy + TEST_RE_HEADER_FLAGS) | LRE_FLAG_ATOM);
    expect_invalid(copy, len);
    free(copy);

    copy = dup_bytecode(buf, len);
    put_u32(copy + TEST_RE_HEADER_BYTECODE_LEN, len);
    expect_invalid(copy, len);
    free(copy);
    free(buf);
}

static void test_instruction_validation(void)
{
    uint8_t *buf, *copy;
    size_t pos;
    int len;

    buf = compile_regexp("[a-z]+", 0, &len);
    copy = dup_bytecode(buf, len);
    copy[TEST_RE_HEADER_LEN] = 0xff;
    expect_invalid(copy, len);
    free(copy);

    pos = find_opcode(buf, len, TEST_REOP_range);
    assert(pos != SIZE_MAX);
    copy = dup_bytecode(buf, len);
    put_u16(copy + pos + 1, 0xffff);
    expect_invalid(copy, len);
    free(copy);

    pos = find_opcode(buf, len, TEST_REOP_split_goto_first);
    assert(pos != SIZE_MAX);
    copy = dup_bytecode(buf, len);
    put_u32(copy + pos + 1, 2);
    expect_invalid(copy, len);
    free(copy);

    pos = find_opcode(buf, len, TEST_REOP_save_start);
    assert(pos != SIZE_MAX);
    copy = dup_bytecode(buf, len);
    copy[pos + 1] = copy[TEST_RE_HEADER_CAPTURE_COUNT];
    expect_invalid(copy, len);
    free(copy);

    copy = dup_bytecode(buf, len);
    copy[TEST_RE_HEADER_REGISTER_COUNT]++;
    expect_invalid(copy, len);
    free(copy);
    free(buf);
}

static void test_lookahead_and_group_validation(void)
{
    uint8_t *buf, *copy;
    size_t pos;
    int len;

    buf = compile_regexp("(?=abc)abc", 0, &len);
    pos = find_opcode(buf, len, TEST_REOP_lookahead);
    assert(pos != SIZE_MAX);
    copy = dup_bytecode(buf, len);
    put_u32(copy + pos + 1, 0);
    expect_invalid(copy, len);
    free(copy);
    free(buf);

    buf = compile_regexp("(?<word>ab)", 0, &len);
    expect_valid(buf, len);
    expect_invalid(buf, len - 1);
    copy = dup_bytecode(buf, len);
    copy[len - 2] = 'x';
    expect_invalid(copy, len);
    free(copy);
    free(buf);
}

static void test_literal_metadata(void)
{
    LREMetadata meta;
    uint8_t *buf, *copy;
    size_t meta_pos;
    int len;

    buf = compile_regexp("abc", 0, &len);
    assert((lre_get_flags(buf) & (LRE_FLAG_ATOM | LRE_FLAG_HAS_META)) ==
           (LRE_FLAG_ATOM | LRE_FLAG_HAS_META));
    assert(lre_get_metadata(buf, len, &meta) == 1);
    assert(meta.kind == LRE_META_LITERAL && meta.encoding == 0);
    assert(meta.flags == LRE_META_FLAG_SOURCE);
    assert(meta.length == 3 && meta.payload_size == 0);
    meta_pos = TEST_RE_HEADER_LEN +
        get_u32(buf + TEST_RE_HEADER_BYTECODE_LEN);
    copy = dup_bytecode(buf, len);
    put_u32(copy + meta_pos + TEST_RE_META_PAYLOAD_SIZE_OFFSET, 1);
    expect_invalid(copy, len);
    free(copy);
    expect_invalid(buf, meta_pos + TEST_RE_META_HEADER_LEN - 1);
    free(buf);

    buf = compile_regexp("\\x61bc", 0, &len);
    expect_valid(buf, len);
    assert(lre_get_metadata(buf, len, &meta) == 1);
    assert(meta.kind == LRE_META_LITERAL && meta.encoding == 0);
    assert(meta.flags == 0 && meta.length == 3);
    assert(memcmp(meta.payload, "abc", 3) == 0);
    meta_pos = TEST_RE_HEADER_LEN +
        get_u32(buf + TEST_RE_HEADER_BYTECODE_LEN);

    copy = dup_bytecode(buf, len);
    copy[meta_pos + TEST_RE_META_VERSION_OFFSET]++;
    expect_invalid(copy, len);
    free(copy);

    copy = dup_bytecode(buf, len);
    copy[meta_pos + TEST_RE_META_KIND_OFFSET] = LRE_META_NONE;
    expect_invalid(copy, len);
    free(copy);

    copy = dup_bytecode(buf, len);
    copy[meta_pos + TEST_RE_META_ENCODING_OFFSET] = 2;
    expect_invalid(copy, len);
    free(copy);

    copy = dup_bytecode(buf, len);
    copy[meta_pos + TEST_RE_META_FLAGS_OFFSET] = 0x80;
    expect_invalid(copy, len);
    free(copy);

    copy = dup_bytecode(buf, len);
    put_u32(copy + meta_pos + TEST_RE_META_PAYLOAD_SIZE_OFFSET, len);
    expect_invalid(copy, len);
    free(copy);

    copy = dup_bytecode(buf, len);
    put_u32(copy + meta_pos + TEST_RE_META_LENGTH_OFFSET, 2);
    expect_invalid(copy, len);
    free(copy);

    copy = dup_bytecode(buf, len);
    copy[meta_pos + TEST_RE_META_HEADER_LEN] = 'z';
    expect_invalid(copy, len);
    free(copy);
    expect_invalid(buf, len - 1);
    free(buf);

    buf = compile_regexp("\\u1234x", 0, &len);
    expect_valid(buf, len);
    assert(lre_get_metadata(buf, len, &meta) == 1);
    assert(meta.encoding == 1 && meta.length == 2 && meta.payload_size == 4);
    assert(get_u16(meta.payload) == 0x1234);
    assert(get_u16(meta.payload + 2) == 'x');
    free(buf);

    buf = compile_regexp("a.c", 0, &len);
    assert(lre_get_flags(buf) & LRE_FLAG_HAS_META);
    assert(lre_get_metadata(buf, len, &meta) == 1);
    assert(meta.kind == LRE_META_NONE && meta.length == 0);
    assert(meta.exec_flags == LRE_META_EXEC_SCAN);
    assert(meta.leading_count == 0 && meta.payload_size == 0);
    free(buf);

    buf = compile_regexp("a.c", LRE_FLAG_STICKY, &len);
    assert(!(lre_get_flags(buf) & LRE_FLAG_HAS_META));
    assert(lre_get_metadata(buf, len, &meta) == 0);
    free(buf);
}

static void test_prefix_metadata(void)
{
    LREMetadata meta;
    uint8_t *buf, *copy;
    uint8_t *capture[4];
    size_t meta_pos;
    int len;

    buf = compile_regexp("abcdef[0-9]+", 0, &len);
    expect_valid(buf, len);
    assert(!(lre_get_flags(buf) & LRE_FLAG_ATOM));
    assert(lre_get_metadata(buf, len, &meta) == 1);
    assert(meta.kind == LRE_META_PREFIX && meta.encoding == 0);
    assert(meta.flags == 0 && meta.length == 6);
    assert(meta.entry_offset == 11);
    assert(memcmp(meta.payload, "abcdef", 6) == 0);
    assert(lre_exec_at(capture, buf, meta.entry_offset,
                       (const uint8_t *)"zzzabcdef123", 3, 12, 0, NULL) == 1);

    meta_pos = TEST_RE_HEADER_LEN +
        get_u32(buf + TEST_RE_HEADER_BYTECODE_LEN);
    copy = dup_bytecode(buf, len);
    put_u32(copy + meta_pos + TEST_RE_META_ENTRY_OFFSET,
            meta.entry_offset + 1);
    expect_invalid(copy, len);
    free(copy);

    copy = dup_bytecode(buf, len);
    copy[meta_pos + TEST_RE_META_HEADER_LEN] = 'z';
    expect_invalid(copy, len);
    free(copy);
    free(buf);

    buf = compile_regexp("abcdef|abcxyz", 0, &len);
    expect_valid(buf, len);
    assert(lre_get_metadata(buf, len, &meta) == 1);
    assert(meta.kind == LRE_META_PREFIX && meta.length == 3);
    assert(memcmp(meta.payload, "abc", 3) == 0);
    free(buf);

    buf = compile_regexp("abcdefghijklmnopqrstuvwxyzABCDEF[0-9]+", 0,
                         &len);
    expect_valid(buf, len);
    assert(lre_get_metadata(buf, len, &meta) == 1);
    assert(meta.kind == LRE_META_PREFIX &&
           meta.length == LRE_PREFIX_MAX_LENGTH);
    assert(meta.payload_size == LRE_PREFIX_MAX_LENGTH);
    assert(memcmp(meta.payload, "abcdefghijklmnopqrstuvwxyzABCDEF",
                  LRE_PREFIX_MAX_LENGTH) == 0);
    free(buf);

    buf = compile_regexp("abcdefghijklmnopqrstuvwxyzABCDEFG[0-9]+", 0,
                         &len);
    expect_valid(buf, len);
    assert(lre_get_metadata(buf, len, &meta) == 1);
    assert(meta.kind == LRE_META_PREFIX &&
           meta.length == LRE_PREFIX_MAX_LENGTH);
    assert(memcmp(meta.payload, "abcdefghijklmnopqrstuvwxyzABCDEF",
                  LRE_PREFIX_MAX_LENGTH) == 0);
    meta_pos = TEST_RE_HEADER_LEN +
        get_u32(buf + TEST_RE_HEADER_BYTECODE_LEN);
    copy = dup_bytecode(buf, len);
    put_u32(copy + meta_pos + TEST_RE_META_LENGTH_OFFSET,
            LRE_PREFIX_MAX_LENGTH + 1);
    expect_invalid(copy, len);
    free(copy);
    free(buf);

    buf = compile_regexp("abcdef(?<word>[0-9]+)", 0, &len);
    expect_valid(buf, len);
    assert(lre_get_metadata(buf, len, &meta) == 1);
    assert(meta.kind == LRE_META_PREFIX && meta.length == 6);
    assert(lre_get_groupnames(buf, len) != NULL);
    free(buf);

    buf = compile_regexp("^abcdef[0-9]+", 0, &len);
    assert(lre_get_metadata(buf, len, &meta) == 1);
    assert(meta.kind == LRE_META_NONE &&
           meta.exec_flags == LRE_META_EXEC_SCAN);
    free(buf);

    buf = compile_regexp("ab(c)?def", 0, &len);
    assert(lre_get_metadata(buf, len, &meta) == 1);
    assert(meta.kind == LRE_META_NONE &&
           meta.exec_flags == LRE_META_EXEC_SCAN);
    free(buf);
}

static void test_quick_check_metadata(void)
{
    LREMetadata meta;
    uint8_t *buf, *copy;
    const uint8_t *record;
    size_t meta_pos;
    int len;

    buf = compile_regexp("[a-z]bcdef", 0, &len);
    expect_valid(buf, len);
    assert(lre_get_metadata(buf, len, &meta) == 1);
    assert(meta.kind == LRE_META_QUICK_CHECK && meta.length == 4);
    assert(meta.entry_offset == 11);
    assert(meta.payload_size == 4 * LRE_QUICK_CHECK_RECORD_SIZE);
    record = meta.payload;
    assert(get_u16(record + LRE_QUICK_CHECK_OFFSET) == 0);
    assert(get_u16(record + LRE_QUICK_CHECK_MASK) != 0);
    record += LRE_QUICK_CHECK_RECORD_SIZE;
    assert(get_u16(record + LRE_QUICK_CHECK_OFFSET) == 1);
    assert(get_u16(record + LRE_QUICK_CHECK_VALUE) == 'b');
    assert(get_u16(record + LRE_QUICK_CHECK_MASK) == 0xffff);

    meta_pos = TEST_RE_HEADER_LEN +
        get_u32(buf + TEST_RE_HEADER_BYTECODE_LEN);
    copy = dup_bytecode(buf, len);
    put_u16(copy + meta_pos + TEST_RE_META_HEADER_LEN +
            LRE_QUICK_CHECK_MASK, 0);
    expect_invalid(copy, len);
    free(copy);

    copy = dup_bytecode(buf, len);
    put_u16(copy + meta_pos + TEST_RE_META_HEADER_LEN +
            LRE_QUICK_CHECK_VALUE, 0xffff);
    expect_invalid(copy, len);
    free(copy);
    free(buf);

    buf = compile_regexp("[a-z]b", 0, &len);
    assert(lre_get_metadata(buf, len, &meta) == 1);
    assert(meta.kind == LRE_META_NONE &&
           meta.exec_flags == LRE_META_EXEC_SCAN);
    free(buf);
}

static void test_execution_descriptor(void)
{
    LREMetadata meta;
    uint8_t *buf, *copy;
    size_t meta_pos;
    int len;

    buf = compile_regexp("(^|[^\\\\])\"x\"", 0, &len);
    expect_valid(buf, len);
    assert(lre_get_metadata(buf, len, &meta) == 1);
    assert(meta.kind == LRE_META_NONE);
    assert(meta.exec_flags ==
           (LRE_META_EXEC_SCAN | LRE_META_EXEC_LEADING));
    assert(meta.entry_offset == 11 && meta.leading_count == 3);
    assert(get_u32(meta.leading_chars) == '"');
    assert(get_u32(meta.leading_chars + 4) == 'x');
    assert(get_u32(meta.leading_chars + 8) == '"');

    meta_pos = TEST_RE_HEADER_LEN +
        get_u32(buf + TEST_RE_HEADER_BYTECODE_LEN);
    copy = dup_bytecode(buf, len);
    put_u32(copy + meta_pos + TEST_RE_META_EXEC_FLAGS_OFFSET,
            LRE_META_EXEC_LEADING);
    expect_invalid(copy, len);
    free(copy);

    copy = dup_bytecode(buf, len);
    put_u32(copy + meta_pos + TEST_RE_META_LEADING_COUNT_OFFSET,
            LRE_META_LEADING_MAX + 1);
    expect_invalid(copy, len);
    free(copy);

    copy = dup_bytecode(buf, len);
    put_u32(copy + meta_pos + TEST_RE_META_HEADER_LEN, 'z');
    expect_invalid(copy, len);
    free(copy);
    free(buf);
}

static void test_descriptor_generic_differential(void)
{
    static const char * const patterns[] = {
        "a.c",
        "(^|[^\\\\])\"x\"",
        "(a|ab)+c",
        "(?=(ab+))ab+c",
        "[a-z]bcdef",
        "abcdef[0-9]+",
    };
    uint32_t state = 0x5eed1234;
    char subject[65];
    size_t case_index, i, j;

    for(case_index = 0; case_index < countof(patterns); case_index++) {
        uint8_t *buf, *generic;
        uint8_t **capture, **generic_capture;
        int alloc_count, capture_count, len;

        buf = compile_regexp(patterns[case_index], 0, &len);
        generic = dup_bytecode(buf, len);
        put_u16(generic + TEST_RE_HEADER_FLAGS,
                get_u16(generic + TEST_RE_HEADER_FLAGS) &
                ~(LRE_FLAG_HAS_META | LRE_FLAG_ATOM));
        capture_count = lre_get_capture_count(buf);
        alloc_count = lre_get_alloc_count(buf);
        capture = calloc(alloc_count, sizeof(*capture));
        generic_capture = calloc(alloc_count,
                                 sizeof(*generic_capture));
        assert(capture != NULL && generic_capture != NULL);

        for(i = 0; i < 200; i++) {
            int ret, generic_ret;

            for(j = 0; j < sizeof(subject) - 1; j++) {
                state = state * 1664525 + 1013904223;
                subject[j] = "abcxyz0\\\""[state % 9];
            }
            subject[sizeof(subject) - 1] = '\0';
            ret = lre_exec(capture, buf, (const uint8_t *)subject, 0,
                           sizeof(subject) - 1, 0, NULL);
            generic_ret = lre_exec(generic_capture, generic,
                                   (const uint8_t *)subject, 0,
                                   sizeof(subject) - 1, 0, NULL);
            assert(ret == generic_ret);
            if (ret == 1) {
                for(j = 0; j < (size_t)capture_count * 2; j++) {
                    if (capture[j] == NULL || generic_capture[j] == NULL) {
                        assert(capture[j] == generic_capture[j]);
                    } else {
                        assert(capture[j] - (uint8_t *)subject ==
                               generic_capture[j] - (uint8_t *)subject);
                    }
                }
            }
        }
        free(generic_capture);
        free(capture);
        free(generic);
        free(buf);
    }
}

static void test_arbitrary_bytecode(void)
{
    uint8_t buf[96];
    uint32_t state = 0x31415926;
    size_t i, j, len;
    char error[128];

    for(i = 0; i < 10000; i++) {
        state = state * 1664525 + 1013904223;
        len = state % (sizeof(buf) + 1);
        for(j = 0; j < len; j++) {
            state = state * 1664525 + 1013904223;
            buf[j] = state >> 24;
        }
        lre_validate_bytecode(buf, len, error, sizeof(error), NULL);
    }
}

int main(void)
{
    test_valid_bytecode();
    test_header_validation();
    test_instruction_validation();
    test_lookahead_and_group_validation();
    test_literal_metadata();
    test_prefix_metadata();
    test_quick_check_metadata();
    test_execution_descriptor();
    test_descriptor_generic_differential();
    test_arbitrary_bytecode();
    return 0;
}

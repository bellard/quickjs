/* Test-only QBC mutator for RegExp validation coverage. */

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cutils.h"
#include "libregexp.h"

#define QBC_HEADER_SIZE 64
#define QBC_PAYLOAD_SIZE_OFFSET 44
#define QBC_CHECKSUM_OFFSET 52

#define SHORT_OPCODES 1

enum {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) TEST_OP_ ## id,
#define def(id, size, n_pop, n_push, f)
#include "quickjs-opcode.h"
#undef def
#undef DEF
#undef FMT
    TEST_OP_COUNT,
};

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

static uint64_t qbc_checksum(const uint8_t *buf, size_t len)
{
    uint64_t h = UINT64_C(1469598103934665603);
    size_t i;

    for(i = 0; i < len; i++) {
        h ^= buf[i];
        h *= UINT64_C(1099511628211);
    }
    return h;
}

static void die(const char *message)
{
    fprintf(stderr, "qbc_regexp_mutator: %s\n", message);
    exit(1);
}

static uint8_t *read_file(const char *filename, size_t *plen)
{
    FILE *f;
    uint8_t *buf;
    long len = 0;

    f = fopen(filename, "rb");
    if (!f)
        die(strerror(errno));
    if (fseek(f, 0, SEEK_END) || (len = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET)) {
        fclose(f);
        die("could not determine input size");
    }
    buf = malloc(len ? (size_t)len : 1);
    if (!buf) {
        fclose(f);
        die("out of memory");
    }
    if (fread(buf, 1, len, f) != (size_t)len) {
        fclose(f);
        free(buf);
        die("could not read input");
    }
    fclose(f);
    *plen = len;
    return buf;
}

static void write_file(const char *filename, const uint8_t *buf, size_t len)
{
    FILE *f = fopen(filename, "wb");

    if (!f)
        die(strerror(errno));
    if (fwrite(buf, 1, len, f) != len || fclose(f))
        die("could not write output");
}

static uint8_t *find_unique(uint8_t *haystack, size_t haystack_len,
                            const uint8_t *needle, size_t needle_len)
{
    uint8_t *match = NULL;
    size_t i;

    if (needle_len == 0 || needle_len > haystack_len)
        die("invalid search sequence");
    for(i = 0; i <= haystack_len - needle_len; i++) {
        if (!memcmp(haystack + i, needle, needle_len)) {
            if (match)
                die("target sequence is not unique");
            match = haystack + i;
        }
    }
    if (!match)
        die("target sequence was not found");
    return match;
}

static uint8_t *find_previous_const_push(uint8_t *payload, uint8_t *pos)
{
    if (pos - payload >= 2 && pos[-2] == TEST_OP_push_const8)
        return pos - 2;
    if (pos - payload >= 5 && pos[-5] == TEST_OP_push_const)
        return pos - 5;
    return NULL;
}

static void mutate_function_opcode(uint8_t *payload, size_t payload_len)
{
    uint8_t *match = NULL;
    size_t i;

    for(i = 0; i < payload_len; i++) {
        uint8_t *regexp_op, *bytecode_push, *pattern_push;

        if (payload[i] != TEST_OP_regexp)
            continue;
        regexp_op = payload + i;
        bytecode_push = find_previous_const_push(payload, regexp_op);
        if (!bytecode_push)
            continue;
        pattern_push = find_previous_const_push(payload, bytecode_push);
        if (!pattern_push)
            continue;
        if (match)
            die("function regexp sequence is not unique");
        match = regexp_op;
    }
    if (!match)
        die("function regexp sequence was not found");
    *match = TEST_OP_invalid;
}

static void mutate_regexp(uint8_t *payload, size_t payload_len,
                          const char *pattern, const char *mode)
{
    uint8_t *compiled, *target;
    uint8_t *metadata;
    int compiled_len;
    char error[128];

    compiled = lre_compile(&compiled_len, error, sizeof(error), pattern,
                           strlen(pattern), 0, NULL);
    if (!compiled)
        die(error);
    target = find_unique(payload, payload_len, compiled, compiled_len);
    metadata = target + 8 + get_u32(target + 4);
    if (!strcmp(mode, "forge-atom")) {
        put_u16(target, get_u16(target) | LRE_FLAG_ATOM);
    } else if (!strcmp(mode, "clear-atom")) {
        put_u16(target, get_u16(target) & ~LRE_FLAG_ATOM);
    } else if (!strcmp(mode, "bad-regexp-opcode")) {
        target[8] = 0xff;
    } else if (!strcmp(mode, "bad-metadata-version")) {
        metadata[LRE_META_VERSION_OFFSET]++;
    } else if (!strcmp(mode, "bad-source-payload")) {
        metadata[LRE_META_FLAGS_OFFSET] |= LRE_META_FLAG_SOURCE;
    } else if (!strcmp(mode, "bad-scan-entry")) {
        put_u32(metadata + LRE_META_ENTRY_OFFSET,
                get_u32(metadata + LRE_META_ENTRY_OFFSET) + 1);
    } else if (!strcmp(mode, "bad-leading-char")) {
        put_u32(metadata + LRE_META_HEADER_LEN,
                get_u32(metadata + LRE_META_HEADER_LEN) + 1);
    } else if (!strcmp(mode, "bad-prefix-entry")) {
        put_u32(metadata + LRE_META_ENTRY_OFFSET,
                get_u32(metadata + LRE_META_ENTRY_OFFSET) + 1);
    } else if (!strcmp(mode, "bad-quick-check")) {
        put_u16(metadata + LRE_META_HEADER_LEN + LRE_QUICK_CHECK_MASK, 0);
    } else {
        free(compiled);
        die("unknown regexp mutation");
    }
    free(compiled);
}

int main(int argc, char **argv)
{
    uint8_t *buf, *payload;
    size_t len, payload_len;
    const char *input, *output, *mode;

    if (argc != 4) {
        fprintf(stderr, "usage: %s input.qbc output.qbc mode\n", argv[0]);
        return 1;
    }
    input = argv[1];
    output = argv[2];
    mode = argv[3];
    buf = read_file(input, &len);
    if (len < QBC_HEADER_SIZE || memcmp(buf, "QJSBC\0\r\n", 8))
        die("invalid QBC header");
    payload_len = get_u64(buf + QBC_PAYLOAD_SIZE_OFFSET);
    if (payload_len != len - QBC_HEADER_SIZE)
        die("invalid QBC payload size");
    payload = buf + QBC_HEADER_SIZE;

    if (!strcmp(mode, "old-bytecode-version")) {
        if (payload_len == 0)
            die("bytecode payload is empty");
        payload[0]--;
        goto write_output;
    }

    if (!strcmp(mode, "forge-atom-capture")) {
        mutate_regexp(payload, payload_len, "(abc)", "forge-atom");
    } else if (!strcmp(mode, "forge-atom-nonliteral")) {
        mutate_regexp(payload, payload_len, "a.c", "forge-atom");
    } else if (!strcmp(mode, "clear-atom")) {
        mutate_regexp(payload, payload_len, "abc", mode);
    } else if (!strcmp(mode, "clear-atom-escaped")) {
        mutate_regexp(payload, payload_len, "\\x74he quick brown fox",
                      "clear-atom");
    } else if (!strcmp(mode, "bad-regexp-opcode")) {
        mutate_regexp(payload, payload_len, "abc", mode);
    } else if (!strcmp(mode, "bad-metadata-version")) {
        mutate_regexp(payload, payload_len, "abc", mode);
    } else if (!strcmp(mode, "bad-source-payload")) {
        mutate_regexp(payload, payload_len, "\\x61bc", mode);
    } else if (!strcmp(mode, "bad-scan-entry")) {
        mutate_regexp(payload, payload_len, "a.c", mode);
    } else if (!strcmp(mode, "bad-leading-char")) {
        mutate_regexp(payload, payload_len, "(^|[^\\\\])\"x\"", mode);
    } else if (!strcmp(mode, "bad-prefix-entry")) {
        mutate_regexp(payload, payload_len, "abcdef[0-9]+", mode);
    } else if (!strcmp(mode, "bad-quick-check")) {
        mutate_regexp(payload, payload_len, "[a-z]bcdef", mode);
    } else if (!strcmp(mode, "bad-function-opcode")) {
        mutate_function_opcode(payload, payload_len);
    } else {
        die("unknown mutation mode");
    }

write_output:
    put_u64(buf + QBC_CHECKSUM_OFFSET, qbc_checksum(payload, payload_len));
    write_file(output, buf, len);
    free(buf);
    return 0;
}

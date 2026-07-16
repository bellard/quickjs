/*
 * Regular Expression Engine
 *
 * Copyright (c) 2017-2018 Fabrice Bellard
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef LIBREGEXP_H
#define LIBREGEXP_H

#include <stddef.h>
#include <stdint.h>

#define LRE_FLAG_GLOBAL     (1 << 0)
#define LRE_FLAG_IGNORECASE (1 << 1)
#define LRE_FLAG_MULTILINE  (1 << 2)
#define LRE_FLAG_DOTALL     (1 << 3)
#define LRE_FLAG_UNICODE    (1 << 4)
#define LRE_FLAG_STICKY     (1 << 5)
#define LRE_FLAG_INDICES    (1 << 6) /* Unused by libregexp, just recorded. */
#define LRE_FLAG_NAMED_GROUPS (1 << 7) /* named groups are present in the regexp */
#define LRE_FLAG_UNICODE_SETS (1 << 8)
#define LRE_FLAG_ATOM       (1 << 9) /* internal: raw literal pattern */
#define LRE_FLAG_HAS_META   (1 << 10) /* internal optimization metadata */

/* RegExp optimization metadata is serialized little-endian after the opcode
   stream. Source literals borrow their payload from JSRegExp.pattern; all
   other payloads are bounded so untrusted bytecode cannot request an
   unbounded fast-path allocation or comparison. */
#define LRE_META_VERSION 3
#define LRE_META_NONE 0
#define LRE_META_LITERAL 1
#define LRE_META_PREFIX 2
#define LRE_META_QUICK_CHECK 3

#define LRE_META_FLAG_SOURCE 1 /* literal uses the source pattern; no payload */

#define LRE_LITERAL_MAX_LENGTH 4096
#define LRE_PREFIX_MIN_LENGTH 3
#define LRE_PREFIX_MAX_LENGTH 32

#define LRE_META_VERSION_OFFSET 0
#define LRE_META_KIND_OFFSET 1
#define LRE_META_ENCODING_OFFSET 2
#define LRE_META_FLAGS_OFFSET 3
#define LRE_META_PAYLOAD_SIZE_OFFSET 4
#define LRE_META_LENGTH_OFFSET 8
#define LRE_META_ENTRY_OFFSET 12
#define LRE_META_EXEC_FLAGS_OFFSET 16
#define LRE_META_LEADING_COUNT_OFFSET 20
#define LRE_META_HEADER_LEN 24

#define LRE_META_EXEC_SCAN (1 << 0)
#define LRE_META_EXEC_LEADING (1 << 1)
#define LRE_META_LEADING_MAX 4

#define LRE_QUICK_CHECK_OFFSET 0
#define LRE_QUICK_CHECK_VALUE 2
#define LRE_QUICK_CHECK_MASK 4
#define LRE_QUICK_CHECK_RECORD_SIZE 6
#define LRE_QUICK_CHECK_MAX 4

typedef struct LREMetadata {
    const uint8_t *payload;
    const uint8_t *leading_chars;
    uint32_t payload_size;
    uint32_t primary_payload_size;
    uint32_t length;
    uint32_t entry_offset;
    uint32_t exec_flags;
    uint32_t leading_count;
    uint8_t kind;
    uint8_t encoding;
    uint8_t flags;
} LREMetadata;

#define LRE_RET_MEMORY_ERROR (-1)
#define LRE_RET_TIMEOUT      (-2)

/* trailer length after the group name including the trailing '\0' */
#define LRE_GROUP_NAME_TRAILER_LEN 2 

uint8_t *lre_compile(int *plen, char *error_msg, int error_msg_size,
                     const char *buf, size_t buf_len, int re_flags,
                     void *opaque);
int lre_is_raw_atom(const char *buf, size_t len, int re_flags);
int lre_validate_bytecode(const uint8_t *bc_buf, size_t bc_buf_len,
                          char *error_msg, int error_msg_size, void *opaque);
int lre_get_alloc_count(const uint8_t *bc_buf);
int lre_get_capture_count(const uint8_t *bc_buf);
int lre_get_flags(const uint8_t *bc_buf);
int lre_get_metadata(const uint8_t *bc_buf, size_t bc_buf_len,
                     LREMetadata *meta);
const char *lre_get_groupnames(const uint8_t *bc_buf, size_t bc_buf_len);
int lre_exec(uint8_t **capture,
             const uint8_t *bc_buf, const uint8_t *cbuf, int cindex, int clen,
             int cbuf_type, void *opaque);
int lre_exec_at(uint8_t **capture,
                const uint8_t *bc_buf, uint32_t bytecode_offset,
                const uint8_t *cbuf, int cindex, int clen,
                int cbuf_type, void *opaque);

int lre_parse_escape(const uint8_t **pp, int allow_utf16);

/* must be provided by the user, return non zero if overflow */
int lre_check_stack_overflow(void *opaque, size_t alloca_size);
/* must be provided by the user, return non zero if time out */
int lre_check_timeout(void *opaque);
void *lre_realloc(void *opaque, void *ptr, size_t size);

#endif /* LIBREGEXP_H */

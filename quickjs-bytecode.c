/*
 * QuickJS bytecode file wrapper
 */
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "cutils.h"
#include "quickjs-libc.h"
#include "quickjs-bytecode.h"

#define QJS_BYTECODE_HEADER_SIZE 64
#define QJS_BYTECODE_VERSION_OFFSET 12
#define QJS_BYTECODE_VERSION_SIZE 32
#define QJS_BYTECODE_PAYLOAD_SIZE_OFFSET 44
#define QJS_BYTECODE_CHECKSUM_OFFSET 52
#define QJS_BYTECODE_FLAGS_OFFSET 60

static const uint8_t qjs_bytecode_magic[8] = {
    'Q', 'J', 'S', 'B', 'C', 0, '\r', '\n'
};

static uint64_t qjs_bytecode_checksum(const uint8_t *buf, size_t len)
{
    uint64_t h = UINT64_C(1469598103934665603);
    size_t i;

    for (i = 0; i < len; i++) {
        h ^= buf[i];
        h *= UINT64_C(1099511628211);
    }
    return h;
}

static int qjs_bytecode_write_all(FILE *f, const void *buf, size_t len)
{
    return fwrite(buf, 1, len, f) == len ? 0 : -1;
}

int qjs_bytecode_write_file(JSContext *ctx, const char *filename,
                            JSValueConst obj, int write_flags)
{
    uint8_t header[QJS_BYTECODE_HEADER_SIZE];
    uint8_t *payload;
    size_t payload_len;
    FILE *f;
    int ret = -1;

    payload = JS_WriteObject(ctx, &payload_len, obj,
                             write_flags | JS_WRITE_OBJ_BYTECODE);
    if (!payload)
        return -1;

    memset(header, 0, sizeof(header));
    memcpy(header, qjs_bytecode_magic, sizeof(qjs_bytecode_magic));
    put_u32(header + 8, QJS_BYTECODE_HEADER_SIZE);
    snprintf((char *)header + QJS_BYTECODE_VERSION_OFFSET,
             QJS_BYTECODE_VERSION_SIZE, "%s", CONFIG_VERSION);
    put_u64(header + QJS_BYTECODE_PAYLOAD_SIZE_OFFSET, payload_len);
    put_u64(header + QJS_BYTECODE_CHECKSUM_OFFSET,
            qjs_bytecode_checksum(payload, payload_len));
    put_u32(header + QJS_BYTECODE_FLAGS_OFFSET, 0);

    f = fopen(filename, "wb");
    if (!f) {
        JS_ThrowReferenceError(ctx, "could not open bytecode file '%s': %s",
                               filename, strerror(errno));
        goto done;
    }
    if (qjs_bytecode_write_all(f, header, sizeof(header)) ||
        qjs_bytecode_write_all(f, payload, payload_len)) {
        JS_ThrowReferenceError(ctx, "could not write bytecode file '%s': %s",
                               filename, strerror(errno));
        goto close_file;
    }
    ret = 0;

close_file:
    if (fclose(f) != 0 && ret == 0) {
        JS_ThrowReferenceError(ctx, "could not close bytecode file '%s': %s",
                               filename, strerror(errno));
        ret = -1;
    }
done:
    js_free(ctx, payload);
    return ret;
}

JSValue qjs_bytecode_read_file(JSContext *ctx, const char *filename)
{
    uint8_t *buf;
    const uint8_t *payload;
    size_t buf_len, payload_len;
    uint64_t payload_len64, checksum;
    uint32_t header_size, flags;
    char version[QJS_BYTECODE_VERSION_SIZE + 1];
    JSValue obj = JS_EXCEPTION;

    buf = js_load_file(ctx, &buf_len, filename);
    if (!buf) {
        JS_ThrowReferenceError(ctx, "could not load bytecode file '%s'",
                               filename);
        return JS_EXCEPTION;
    }

    if (buf_len < QJS_BYTECODE_HEADER_SIZE) {
        JS_ThrowSyntaxError(ctx, "invalid bytecode file: header is too short");
        goto done;
    }
    if (memcmp(buf, qjs_bytecode_magic, sizeof(qjs_bytecode_magic)) != 0) {
        JS_ThrowSyntaxError(ctx, "invalid bytecode file: bad magic");
        goto done;
    }

    header_size = get_u32(buf + 8);
    if (header_size != QJS_BYTECODE_HEADER_SIZE) {
        JS_ThrowSyntaxError(ctx, "invalid bytecode file: unsupported header size");
        goto done;
    }

    memcpy(version, buf + QJS_BYTECODE_VERSION_OFFSET,
           QJS_BYTECODE_VERSION_SIZE);
    version[QJS_BYTECODE_VERSION_SIZE] = '\0';
    if (strcmp(version, CONFIG_VERSION) != 0) {
        JS_ThrowSyntaxError(ctx,
                            "bytecode version mismatch: file uses '%s', runtime uses '%s'",
                            version, CONFIG_VERSION);
        goto done;
    }

    flags = get_u32(buf + QJS_BYTECODE_FLAGS_OFFSET);
    if (flags != 0) {
        JS_ThrowSyntaxError(ctx, "invalid bytecode file: unsupported flags");
        goto done;
    }

    payload_len64 = get_u64(buf + QJS_BYTECODE_PAYLOAD_SIZE_OFFSET);
    if (payload_len64 > SIZE_MAX) {
        JS_ThrowSyntaxError(ctx, "invalid bytecode file: payload is too large");
        goto done;
    }
    payload_len = (size_t)payload_len64;
    if (payload_len != buf_len - QJS_BYTECODE_HEADER_SIZE) {
        JS_ThrowSyntaxError(ctx, "invalid bytecode file: size mismatch");
        goto done;
    }

    payload = buf + QJS_BYTECODE_HEADER_SIZE;
    checksum = get_u64(buf + QJS_BYTECODE_CHECKSUM_OFFSET);
    if (checksum != qjs_bytecode_checksum(payload, payload_len)) {
        JS_ThrowSyntaxError(ctx, "bytecode checksum mismatch");
        goto done;
    }

    obj = JS_ReadObject(ctx, payload, payload_len, JS_READ_OBJ_BYTECODE);

done:
    js_free(ctx, buf);
    return obj;
}

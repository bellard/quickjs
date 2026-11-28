// Copyright 2025 Google LLC
// Fuzz target for the QuickJS JSON parser.  The raw fuzzer input is fed
// directly to JS_ParseJSON, the entry point behind JSON.parse(), so the
// fuzzer exercises the parser itself instead of having to mutate valid JS
// source containing JSON literals.  No script execution and no
// quickjs-libc modules are involved; memory and stack limits are set
// explicitly so that pathological inputs surface parser bugs rather than
// harness artifacts.

#include "quickjs.h"

#include <stdint.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0)
        return 0;

    JSRuntime *rt = JS_NewRuntime();
    if (!rt)
        return 0;
    JS_SetMemoryLimit(rt, 64 << 20);   /* 64 MB */
    JS_SetMaxStackSize(rt, 512 << 10); /* 512 KB */
    JSContext *ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return 0;
    }

    /* JS_ParseJSON requires a zero-terminated buffer (buf[buf_len] == '\0'). */
    char *buf = malloc(size + 1);
    if (!buf) {
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 0;
    }
    memcpy(buf, data, size);
    buf[size] = '\0';

    JSValue obj = JS_ParseJSON(ctx, buf, size, "<json>");
    if (JS_IsException(obj)) {
        JSValue exc = JS_GetException(ctx);
        JS_FreeValue(ctx, exc);
    } else {
        JS_FreeValue(ctx, obj);
    }
    free(buf);

    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return 0;
}

// Copyright 2025 Google LLC
// Fuzz target for QuickJS bytecode loading.  The raw fuzzer input is treated
// as a JS_READ_OBJ_BYTECODE object, i.e. exactly the bytes qjsc emits and an
// embedding host may load from disk.  The input is first deserialised with
// JS_ReadObject (the bytecode reader) and then executed like
// js_std_eval_binary does (module resolve, import meta, JS_EvalFunction,
// js_std_loop), so both the reader and the interpreter run on
// attacker-controlled bytecode.  Memory/stack/interrupt limits come from
// fuzz_common.c, matching the other quickjs targets.

#include "quickjs.h"
#include "quickjs-libc.h"
#include "cutils.h"
#include "fuzz/fuzz_common.h"

#include <stdint.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0)
        return 0;

    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);
    test_one_input_init(rt, ctx);

    JSValue obj = JS_ReadObject(ctx, data, size, JS_READ_OBJ_BYTECODE);
    if (JS_IsException(obj)) {
        js_std_dump_error(ctx);
        js_std_free_handlers(rt);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 0;
    }

    reset_nbinterrupts();
    if (JS_VALUE_GET_TAG(obj) == JS_TAG_MODULE) {
        if (JS_ResolveModule(ctx, obj) < 0) {
            js_std_free_handlers(rt);
            JS_FreeContext(ctx);
            JS_FreeRuntime(rt);
            return 0;
        }
        js_module_set_import_meta(ctx, obj, FALSE, TRUE);
    }
    JSValue val = JS_EvalFunction(ctx, obj);
    if (!JS_IsException(val)) {
        js_std_loop(ctx);
        JS_FreeValue(ctx, val);
    } else {
        js_std_dump_error(ctx);
    }
    js_std_free_handlers(rt);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return 0;
}

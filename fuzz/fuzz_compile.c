/* Copyright 2020 Google Inc.

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 */

#include "quickjs.h"
#include "quickjs-libc.h"
#include "cutils.h"
#include "fuzz/fuzz_common.h"

#include <stdint.h>
#include <stdio.h>


int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0)
        return 0;

    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);
    test_one_input_init(rt, ctx);

    uint8_t *null_terminated_data = malloc(size + 1);
    memcpy(null_terminated_data, data, size);
    null_terminated_data[size] = 0;

    JSValue obj = JS_Eval(ctx, (const char *)null_terminated_data, size, "<none>", JS_EVAL_FLAG_COMPILE_ONLY | JS_EVAL_TYPE_MODULE);
    free(null_terminated_data);
    //TODO target with JS_ParseJSON
    if (JS_IsException(obj)) {
        js_std_free_handlers(rt);
        JS_FreeValue(ctx, obj);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 0;
    }
    obj = js_std_await(ctx, obj);
    size_t bytecode_size;
    uint8_t* bytecode = JS_WriteObject(ctx, &bytecode_size, obj, JS_WRITE_OBJ_BYTECODE);
    JS_FreeValue(ctx, obj);
    if (!bytecode) {
        js_std_free_handlers(rt);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 0;
    }
    obj = JS_ReadObject(ctx, bytecode, bytecode_size, JS_READ_OBJ_BYTECODE);
    js_free(ctx, bytecode);
    if (JS_IsException(obj)) {
        js_std_free_handlers(rt);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 0;
    }
    reset_nbinterrupts();
    /* this is based on
     * js_std_eval_binary(ctx, bytecode, bytecode_size, 0);
     * modified so as not to exit on JS exception
     */
    JSValue val;
    if (JS_VALUE_GET_TAG(obj) == JS_TAG_MODULE) {
        if (JS_ResolveModule(ctx, obj) < 0) {
            JS_FreeValue(ctx, obj);
            js_std_free_handlers(rt);
            JS_FreeContext(ctx);
            JS_FreeRuntime(rt);
            return 0;
        }
        js_module_set_import_meta(ctx, obj, FALSE, TRUE);
    }
    val = JS_EvalFunction(ctx, obj);
    if (JS_IsException(val)) {
        js_std_dump_error(ctx);
    } else {
        js_std_loop(ctx);
    }
    JS_FreeValue(ctx, val);
    js_std_free_handlers(rt);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);

    return 0;
}

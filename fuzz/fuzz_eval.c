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
#include "fuzz/fuzz_common.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0)
        return 0;

    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);
    test_one_input_init(rt, ctx);

    uint8_t *null_terminated_data = malloc(size + 1);
    memcpy(null_terminated_data, data, size);
    null_terminated_data[size] = 0;

    reset_nbinterrupts();
    //the final 0 does not count (as in strlen)
    JSValue val = JS_Eval(ctx, (const char *)null_terminated_data, size, "<none>", JS_EVAL_TYPE_GLOBAL);
    free(null_terminated_data);
    //TODO targets with JS_ParseJSON, JS_ReadObject
    if (!JS_IsException(val)) {
        js_std_loop(ctx);
        JS_FreeValue(ctx, val);
    }
    js_std_free_handlers(rt);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return 0;
}

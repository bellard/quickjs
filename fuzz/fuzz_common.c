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

#include <string.h>

#include "fuzz/fuzz_common.h"

// handle timeouts from infinite loops
static int interrupt_handler(JSRuntime *rt, void *opaque)
{
    nbinterrupts++;
    return (nbinterrupts > 100);
}

void reset_nbinterrupts() {
    nbinterrupts = 0;
}

void test_one_input_init(JSRuntime *rt, JSContext *ctx) {
    // 64 Mo
    JS_SetMemoryLimit(rt, 0x4000000);
    // 64 Kb
    JS_SetMaxStackSize(rt, 0x10000);

    JS_SetModuleLoaderFunc(rt, NULL, js_module_loader, NULL);
    JS_SetInterruptHandler(JS_GetRuntime(ctx), interrupt_handler, NULL);
    js_std_add_helpers(ctx, 0, NULL);

    // Load os and std
    js_std_init_handlers(rt);
    js_init_module_std(ctx, "std");
    js_init_module_os(ctx, "os");
    const char *str = "import * as std from 'std';\n"
                "import * as os from 'os';\n"
                "globalThis.std = std;\n"
                "globalThis.os = os;\n";
    JSValue std_val = JS_Eval(ctx, str, strlen(str), "<input>", JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (!JS_IsException(std_val)) {
        js_module_set_import_meta(ctx, std_val, 1, 1);
        std_val = JS_EvalFunction(ctx, std_val);
    } else {
        js_std_dump_error(ctx);
    }
    std_val = js_std_await(ctx, std_val);
    JS_FreeValue(ctx, std_val);
}

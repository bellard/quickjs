// Copyright 2025 Google LLC
// Fuzz target for QuickJS ES6 module parsing

#include "quickjs.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) return 0;
    
    JSRuntime* rt = JS_NewRuntime();
    if (!rt) return 0;
    
    JSContext* ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return 0;
    }
    
    char* input = malloc(size + 1);
    if (!input) {
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 0;
    }
    memcpy(input, data, size);
    input[size] = '\0';
    
    const char* export_patterns[] = {
        "export default %s;",
        "export const x = %s;",
        "export let x = %s;",
        "export var x = %s;",
        "export function f() { %s }",
        "export class C { %s }",
        "export { %s };",
        "export * from '%s';",
        "export { %s } from 'module';",
        "export { default as x } from '%s';",
    };
    
    int pattern_idx = data[0] % (sizeof(export_patterns) / sizeof(export_patterns[0]));
    
    char script[8192];
    snprintf(script, sizeof(script), export_patterns[pattern_idx], input);
    
    JSValue result = JS_Eval(ctx, script, strlen(script), "<input>",
                               JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    
    if (!JS_IsException(result)) {
        JS_FreeValue(ctx, result);
    } else {
        JS_GetException(ctx);
    }
    
    JSValue result2 = JS_Eval(ctx, input, size, "<input>", JS_EVAL_FLAG_COMPILE_ONLY);
    if (!JS_IsException(result2)) {
        JS_FreeValue(ctx, result2);
    } else {
        JS_GetException(ctx);
    }
    
    const char* import_patterns[] = {
        "import '%s';",
        "import x from '%s';",
        "import * as x from '%s';",
        "import { x } from '%s';",
        "import { x as y } from '%s';",
        "import x, { y } from '%s';",
        "import x, * as y from '%s';",
    };
    
    int import_idx = (data[0] >> 4) % (sizeof(import_patterns) / sizeof(import_patterns[0]));
    char import_script[8192];
    char* sanitized = malloc(size + 1);
    if (sanitized) {
        size_t j = 0;
        for (size_t i = 0; i < size && j < size; i++) {
            if (data[i] != '\'' && data[i] != '"' && data[i] != '\n' && data[i] != '\r') {
                sanitized[j++] = data[i];
            }
        }
        sanitized[j] = '\0';
        
        snprintf(import_script, sizeof(import_script), import_patterns[import_idx], sanitized);
        
        JSValue import_result = JS_Eval(ctx, import_script, strlen(import_script), "<input>",
                                         JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        if (!JS_IsException(import_result)) {
            JS_FreeValue(ctx, import_result);
        } else {
            JS_GetException(ctx);
        }
        
        free(sanitized);
    }
    
    free(input);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    
    return 0;
}

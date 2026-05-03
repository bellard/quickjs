// Copyright 2025 Google LLC
// Fuzz target for QuickJS ES6 module parsing
// Targets: js_parse_export (0% coverage, complexity 6727)
//          js_parse_import (related)

#include <quickjs/quickjs.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// Fuzz target for ES6 module export/import parsing
// This targets the highest complexity uncovered function
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) return 0;
    
    // Create a JS runtime and context
    JSRuntime* rt = JS_NewRuntime();
    if (!rt) return 0;
    
    JSContext* ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return 0;
    }
    
    // Ensure null-terminated input
    char* input = (char*)malloc(size + 1);
    if (!input) {
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 0;
    }
    memcpy(input, data, size);
    input[size] = '\0';
    
    // Test various ES6 module patterns
    // These patterns exercise js_parse_export
    
    // Pattern 1: Simple export
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
    
    // Use the first byte to select pattern
    int pattern_idx = data[0] % (sizeof(export_patterns) / sizeof(export_patterns[0]));
    
    // Create the test script
    char script[8192];
    snprintf(script, sizeof(script), export_patterns[pattern_idx], input);
    
    // Try to parse as module script
    JSValue result = JS_Eval(ctx, script, strlen(script), "<input>",
                               JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    
    if (!JS_IsException(result)) {
        JS_FreeValue(ctx, result);
    } else {
        // Clear exception
        JS_GetException(ctx);
    }
    
    // Also test as regular script (non-module)
    JSValue result2 = JS_Eval(ctx, input, size, "<input>",
                                JS_EVAL_FLAG_COMPILE_ONLY);
    if (!JS_IsException(result2)) {
        JS_FreeValue(ctx, result2);
    } else {
        JS_GetException(ctx);
    }
    
    // Test import statements
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
    // Sanitize input for use as module specifier (remove quotes)
    char* sanitized = (char*)malloc(size + 1);
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
    
    // Cleanup
    free(input);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    
    return 0;
}

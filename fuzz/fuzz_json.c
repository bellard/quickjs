// Copyright 2025 Google LLC
// Fuzz target for QuickJS JSON operations

#include "quickjs.h"
#include "quickjs-libc.h"
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
    
    char parse_script[8192];
    snprintf(parse_script, sizeof(parse_script), 
             "JSON.parse(JSON.stringify(%s))", input);
    
    JSValue parse_result = JS_Eval(ctx, parse_script, strlen(parse_script), 
                                    "<json-fuzz>", 0);
    
    if (JS_IsException(parse_result)) {
        JS_GetException(ctx);
    } else {
        JS_FreeValue(ctx, parse_result);
    }
    
    char direct_parse[16384];
    snprintf(direct_parse, sizeof(direct_parse), "JSON.parse('");
    
    size_t script_len = strlen(direct_parse);
    for (size_t i = 0; i < size && script_len < sizeof(direct_parse) - 20; i++) {
        char c = input[i];
        if (c == '\\' || c == '\'') {
            direct_parse[script_len++] = '\\';
        }
        if (c >= 32 && c < 127) {
            direct_parse[script_len++] = c;
        }
    }
    strcat(direct_parse + script_len, "');");
    
    JSValue direct_result = JS_Eval(ctx, direct_parse, strlen(direct_parse), 
                                     "<json-direct>", 0);
    if (JS_IsException(direct_result)) {
        JS_GetException(ctx);
    } else {
        JS_FreeValue(ctx, direct_result);
    }
    
    char stringify_script[8192];
    snprintf(stringify_script, sizeof(stringify_script),
             "var obj = { data: %s, num: 123, str: 'test', bool: true, nullv: null, "
             "arr: [1,2,3], nested: { a: 1 } }; JSON.stringify(obj);",
             input);
    
    JSValue stringify_result = JS_Eval(ctx, stringify_script, 
                                        strlen(stringify_script), "<json-stringify>", 0);
    if (JS_IsException(stringify_result)) {
        JS_GetException(ctx);
    } else {
        const char* str = JS_ToCString(ctx, stringify_result);
        if (str) {
            JS_FreeCString(ctx, str);
        }
        JS_FreeValue(ctx, stringify_result);
    }
    
    const char* spacing_tests[] = {
        "JSON.stringify({a:1,b:2})",
        "JSON.stringify({a:1,b:2}, null, 2)",
        "JSON.stringify({a:1,b:2}, null, '  ')",
        "JSON.stringify([1,2,3])",
        "JSON.stringify(null)",
        "JSON.stringify(undefined)",
        "JSON.stringify(123)",
        "JSON.stringify('string')",
        "JSON.stringify(true)",
    };
    
    for (size_t i = 0; i < sizeof(spacing_tests) / sizeof(spacing_tests[0]); i++) {
        JSValue r = JS_Eval(ctx, spacing_tests[i], strlen(spacing_tests[i]), 
                            "<json-test>", 0);
        if (!JS_IsException(r)) {
            JS_FreeValue(ctx, r);
        } else {
            JS_GetException(ctx);
        }
    }
    
    const char* reviver_test = "JSON.parse('{\"a\":1,\"b\":2}', function(k,v) { return v; })";
    JSValue reviver_result = JS_Eval(ctx, reviver_test, strlen(reviver_test), 
                                       "<json-reviver>", 0);
    if (!JS_IsException(reviver_result)) {
        JS_FreeValue(ctx, reviver_result);
    } else {
        JS_GetException(ctx);
    }
    
    const char* replacer_test = "JSON.stringify({a:1,b:2}, function(k,v) { return v; })";
    JSValue replacer_result = JS_Eval(ctx, replacer_test, strlen(replacer_test),
                                       "<json-replacer>", 0);
    if (!JS_IsException(replacer_result)) {
        JS_FreeValue(ctx, replacer_result);
    } else {
        JS_GetException(ctx);
    }
    
    free(input);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    
    return 0;
}

// Copyright 2025 Google LLC
// Fuzz target for QuickJS RegExp compilation

#include "quickjs.h"
#include "quickjs-libc.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2) return 0;
    
    JSRuntime* rt = JS_NewRuntime();
    if (!rt) return 0;
    
    JSContext* ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return 0;
    }
    
    size_t pattern_len = size / 2;
    size_t flags_len = size - pattern_len;
    
    if (pattern_len == 0 || flags_len == 0) {
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 0;
    }
    
    char* pattern = malloc(pattern_len + 1);
    char* flags = malloc(flags_len + 1);
    
    if (!pattern || !flags) {
        free(pattern);
        free(flags);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 0;
    }
    
    memcpy(pattern, data, pattern_len);
    pattern[pattern_len] = '\0';
    
    memcpy(flags, data + pattern_len, flags_len);
    flags[flags_len] = '\0';
    
    char valid_flags[16];
    size_t valid_idx = 0;
    const char* valid = "gimsuy";
    for (size_t i = 0; i < flags_len && valid_idx < sizeof(valid_flags) - 1; i++) {
        if (strchr(valid, flags[i]) && !strchr(valid_flags, flags[i])) {
            valid_flags[valid_idx++] = flags[i];
        }
    }
    valid_flags[valid_idx] = '\0';
    
    char script[8192];
    char escaped_pattern[4096];
    size_t esc_idx = 0;
    for (size_t i = 0; i < pattern_len && esc_idx < sizeof(escaped_pattern) - 2; i++) {
        if (pattern[i] == '\\' || pattern[i] == '"' || pattern[i] == '\n' || 
            pattern[i] == '\r' || pattern[i] == '\t') {
            escaped_pattern[esc_idx++] = '\\';
        }
        escaped_pattern[esc_idx++] = pattern[i];
    }
    escaped_pattern[esc_idx] = '\0';
    
    snprintf(script, sizeof(script), "new RegExp(\"%s\", \"%s\")", 
             escaped_pattern, valid_flags);
    
    JSValue regexp_result = JS_Eval(ctx, script, strlen(script), "<regexp>", 0);
    
    if (!JS_IsException(regexp_result)) {
        const char* test_strings[] = {
            "'test string'",
            "''",
            "'aaaaaaaaaa'",
            "'1234567890'",
            "'!@#$%^&*()'",
        };
        
        for (size_t i = 0; i < sizeof(test_strings) / sizeof(test_strings[0]); i++) {
            char match_script[4096];
            snprintf(match_script, sizeof(match_script),
                     "var re = %s; re.test(%s); re.exec(%s); %s.match(re);",
                     script, test_strings[i], test_strings[i], test_strings[i]);
            
            JSValue match_result = JS_Eval(ctx, match_script, strlen(match_script), 
                                           "<regexp-match>", 0);
            if (!JS_IsException(match_result)) {
                JS_FreeValue(ctx, match_result);
            } else {
                JS_GetException(ctx);
            }
        }
        
        const char* split_test = "'a,b,c,d'.split(/,/)";
        JSValue split_result = JS_Eval(ctx, split_test, strlen(split_test), 
                                       "<regexp-split>", 0);
        if (!JS_IsException(split_result)) {
            JS_FreeValue(ctx, split_result);
        } else {
            JS_GetException(ctx);
        }
        
        const char* replace_test = "'hello world'.replace(/world/, 'universe')";
        JSValue replace_result = JS_Eval(ctx, replace_test, strlen(replace_test),
                                          "<regexp-replace>", 0);
        if (!JS_IsException(replace_result)) {
            JS_FreeValue(ctx, replace_result);
        } else {
            JS_GetException(ctx);
        }
        
        const char* search_test = "'abc123def'.search(/[0-9]+/)";
        JSValue search_result = JS_Eval(ctx, search_test, strlen(search_test),
                                         "<regexp-search>", 0);
        if (!JS_IsException(search_result)) {
            JS_FreeValue(ctx, search_result);
        } else {
            JS_GetException(ctx);
        }
        
        JS_FreeValue(ctx, regexp_result);
    } else {
        JS_GetException(ctx);
    }
    
    char literal_script[4096];
    char slash_escaped[2048];
    size_t slash_idx = 0;
    for (size_t i = 0; i < pattern_len && slash_idx < sizeof(slash_escaped) - 2; i++) {
        if (pattern[i] == '/') {
            slash_escaped[slash_idx++] = '\\';
        }
        slash_escaped[slash_idx++] = pattern[i];
    }
    slash_escaped[slash_idx] = '\0';
    
    snprintf(literal_script, sizeof(literal_script), "/%s/%s.test('test')", slash_escaped, valid_flags);
    
    JSValue literal_result = JS_Eval(ctx, literal_script, strlen(literal_script),
                                      "<regexp-literal>", 0);
    if (!JS_IsException(literal_result)) {
        JS_FreeValue(ctx, literal_result);
    } else {
        JS_GetException(ctx);
    }
    
    const char* builtin_tests[] = {
        "RegExp.prototype.compile",
        "/a/g[Symbol.match]('a')",
        "/a/g[Symbol.replace]('a', 'b')",
        "/a/g[Symbol.search]('a')",
        "/a/g[Symbol.split]('a,b,a')",
    };
    
    for (size_t i = 0; i < sizeof(builtin_tests) / sizeof(builtin_tests[0]); i++) {
        JSValue r = JS_Eval(ctx, builtin_tests[i], strlen(builtin_tests[i]),
                            "<regexp-builtin>", 0);
        if (!JS_IsException(r)) {
            JS_FreeValue(ctx, r);
        } else {
            JS_GetException(ctx);
        }
    }
    
    free(pattern);
    free(flags);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    
    return 0;
}

// Copyright 2025 Google LLC
// Fuzz target for QuickJS bytecode execution

#include "quickjs.h"
#include "quickjs-libc.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 8) return 0; // Need at least minimal bytecode header
    
    JSRuntime* rt = JS_NewRuntime();
    if (!rt) return 0;
    
    JSContext* ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return 0;
    }
    
    char load_script[256];
    snprintf(load_script, sizeof(load_script),
             "(function() { "
             "  var buf = new Uint8Array(%zu); "
             "  for (var i = 0; i < %zu; i++) buf[i] = 0; "
             "  return evalBinary(buf); "
             "})()", size, size);
    
    JSValue eval_result = JS_Eval(ctx, load_script, strlen(load_script), 
                                   "<bytecode-load>", 0);
    
    if (!JS_IsException(eval_result)) {
        JS_FreeValue(ctx, eval_result);
    } else {
        JS_GetException(ctx);
    }
    
    const char* simple_script = "({ a: 1, b: 'test', c: function() { return 42; } })";
    
    JSValue bytecode = JS_Eval(ctx, simple_script, strlen(simple_script),
                                "<compile>", JS_EVAL_FLAG_COMPILE_ONLY);
    
    if (!JS_IsException(bytecode)) {
        size_t bytecode_len;
        uint8_t* bytecode_buf = JS_WriteObject(ctx, &bytecode_len, bytecode, 
                                                JS_WRITE_OBJ_BYTECODE);
        
        if (bytecode_buf) {
            JSValue loaded = JS_ReadObject(ctx, bytecode_buf, bytecode_len,
                                            JS_READ_OBJ_BYTECODE);
            
            if (!JS_IsException(loaded)) {
                JSValue result = JS_EvalFunction(ctx, loaded);
                if (!JS_IsException(result)) {
                    JS_FreeValue(ctx, result);
                } else {
                    JS_GetException(ctx);
                }
            } else {
                JS_GetException(ctx);
            }
            
            js_free(ctx, bytecode_buf);
        }
        
        JS_FreeValue(ctx, bytecode);
    } else {
        JS_GetException(ctx);
    }
    
    if (size >= 8) {
        uint8_t* fake_bytecode = malloc(size);
        if (fake_bytecode) {
            memcpy(fake_bytecode, data, size);
            
            if (data[0] % 2 == 0) {
                fake_bytecode[0] = 'Q';
                fake_bytecode[1] = 'C';
                fake_bytecode[2] = 'A';
                fake_bytecode[3] = 'M';
            }
            
            JSValue malformed = JS_ReadObject(ctx, fake_bytecode, size,
                                               JS_READ_OBJ_BYTECODE);
            if (!JS_IsException(malformed)) {
                JS_FreeValue(ctx, malformed);
            } else {
                JS_GetException(ctx);
            }
            
            free(fake_bytecode);
        }
    }
    
    const char* eval_script_test = "typeof std !== 'undefined' ? std.evalScript : null";
    JSValue std_check = JS_Eval(ctx, eval_script_test, strlen(eval_script_test),
                                 "<std-check>", 0);
    if (!JS_IsException(std_check)) {
        JS_FreeValue(ctx, std_check);
    } else {
        JS_GetException(ctx);
    }
    
    const char* module_script = "export default 42; export const x = 123;";
    JSValue module_bytecode = JS_Eval(ctx, module_script, strlen(module_script),
                                       "<module-compile>", 
                                       JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    
    if (!JS_IsException(module_bytecode)) {
        size_t mod_bc_len;
        uint8_t* mod_bc_buf = JS_WriteObject(ctx, &mod_bc_len, module_bytecode,
                                              JS_WRITE_OBJ_BYTECODE);
        
        if (mod_bc_buf) {
            JSValue mod_loaded = JS_ReadObject(ctx, mod_bc_buf, mod_bc_len,
                                                JS_READ_OBJ_BYTECODE);
            if (!JS_IsException(mod_loaded)) {
                JSValue mod_result = JS_EvalFunction(ctx, mod_loaded);
                if (!JS_IsException(mod_result)) {
                    JS_FreeValue(ctx, mod_result);
                } else {
                    JS_GetException(ctx);
                }
            } else {
                JS_GetException(ctx);
            }
            
            js_free(ctx, mod_bc_buf);
        }
        
        JS_FreeValue(ctx, module_bytecode);
    } else {
        JS_GetException(ctx);
    }
    
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    
    return 0;
}

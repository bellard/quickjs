#ifndef QUICKJS_LIBC_MIN_H
#define QUICKJS_LIBC_MIN_H

#include <stdlib.h>
#include <stdio.h>
#include "quickjs.h"
#include "cutils.h"

int js_module_set_import_meta(JSContext *ctx, JSValueConst func_val,
                              JS_BOOL use_realpath, JS_BOOL is_main);
void js_std_add_helpers(JSContext *ctx, int argc, char **argv);
void js_std_dump_error(JSContext *ctx);
void js_std_loop(JSContext *ctx);
void js_std_init_handlers(JSRuntime *rt);
JSValue js_load_module_binary(JSContext *ctx, const uint8_t *buf, size_t buf_len);

#endif

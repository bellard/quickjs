/*
 * QuickJS bytecode file wrapper
 */
#ifndef QUICKJS_BYTECODE_H
#define QUICKJS_BYTECODE_H

#include "quickjs.h"

int qjs_bytecode_write_file(JSContext *ctx, const char *filename,
                            JSValueConst obj, int write_flags);
JSValue qjs_bytecode_read_file(JSContext *ctx, const char *filename);

#endif /* QUICKJS_BYTECODE_H */

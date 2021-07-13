/*
 * QuickJS: binary JSON module (test only)
 *
 * Copyright (c) 2017-2019 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "quickjs-libc.h"
#include "cutils.h"

static JSValue js_bjson_read(JSContext *ctx, JSValueConst this_val,
  int argc, JSValueConst *argv)
{
  uint8_t *buf;
  uint64_t pos = 0, len = 0;
  JSValue obj;
  JSValue cb = JS_UNINITIALIZED;
  size_t size;
  int flags;

  if (argc > 1) {
    if (JS_IsFunction(ctx, argv[1])) 
      cb = argv[1];
    else if (JS_ToIndex(ctx, &pos, argv[1]))
      return JS_EXCEPTION;
    if (argc > 2) {
      if (JS_ToIndex(ctx, &len, argv[2]))
        return JS_EXCEPTION;
    }
  }
  buf = JS_GetArrayBuffer(ctx, &size, argv[0]);
  if (!buf)
    return JS_EXCEPTION;

  if (len == 0)
    len = size - pos;

  if (pos + len > size)
    return JS_ThrowRangeError(ctx, "array buffer overflow");
  flags = 0;
  if (argc > 3 && JS_ToBool(ctx, argv[3]))
    flags |= JS_READ_OBJ_REFERENCE;

  if (cb != JS_UNINITIALIZED) {
    cb = JS_DupValue(ctx,cb);
    size_t rest = 0;
    uint8_t *sbuf = buf;
    uint64_t slen = len;
    uint8_t *send = buf + len;
    do {
      obj = JS_ReadObject2(ctx, sbuf, slen, flags, &rest);
      sbuf = send - rest;
      slen = rest;
      JSValue rv = JS_Call(ctx, cb, JS_UNDEFINED, 1, &obj);
      JS_FreeValue(ctx, obj);
      if (JS_IsException(rv)) {
        JS_FreeValue(ctx, cb);
        return rv;
      }
      if (rv == JS_FALSE)
        break;
    } while (rest);
    JS_FreeValue(ctx, cb);
    return JS_NewInt64(ctx, rest);
  }
  else 
    obj = JS_ReadObject(ctx, buf + pos, len, flags);
  return obj;
}

static JSValue js_bjson_write(JSContext *ctx, JSValueConst this_val,
  int argc, JSValueConst *argv)
{
  size_t len;
  uint8_t *buf;
  JSValue array;
  int flags;

  flags = 0;
  if (JS_ToBool(ctx, argv[1]))
    flags |= JS_WRITE_OBJ_REFERENCE;
  buf = JS_WriteObject(ctx, &len, argv[0], flags);
  if (!buf)
    return JS_EXCEPTION;
  array = JS_NewArrayBufferCopy(ctx, buf, len);
  js_free(ctx, buf);
  return array;
}

static const JSCFunctionListEntry js_bjson_funcs[] = {
    JS_CFUNC_DEF("read", 4, js_bjson_read),
    JS_CFUNC_DEF("write", 2, js_bjson_write),
};

static int js_bjson_init(JSContext *ctx, JSModuleDef *m)
{
  return JS_SetModuleExportList(ctx, m, js_bjson_funcs,
    countof(js_bjson_funcs));
}

JSModuleDef *js_init_module_bjson(JSContext *ctx, const char *module_name)
{
  JSModuleDef *m;
  m = JS_NewCModule(ctx, module_name, js_bjson_init);
  if (!m) return NULL;
  JS_AddModuleExportList(ctx, m, js_bjson_funcs, countof(js_bjson_funcs));
  return m;
}

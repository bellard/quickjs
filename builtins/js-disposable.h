#ifndef JS_DISPOSABLE_H
#define JS_DISPOSABLE_H

#include "quickjs.h"

#ifdef __cplusplus
extern "C" {
#endif

JSValue JS_NewSuppressedError(JSContext *ctx, JSValueConst error, JSValueConst suppressed);
JSValue JS_GetSuppressedErrorError(JSContext *ctx, JSValueConst obj);
JSValue JS_GetSuppressedErrorSuppressed(JSContext *ctx, JSValueConst obj);

JSAtom JS_AtomSymbolDispose(JSContext *ctx);
JSAtom JS_AtomSymbolAsyncDispose(JSContext *ctx);

int JS_CallDispose(JSContext *ctx, JSValueConst obj);
int JS_CallAsyncDispose(JSContext *ctx, JSValueConst obj);

#ifdef __cplusplus
}
#endif

#endif

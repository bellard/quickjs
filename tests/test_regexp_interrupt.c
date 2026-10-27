#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "quickjs.h"

typedef struct InterruptState {
    int calls;
    int stop;
} InterruptState;

static void fail(const char *message)
{
    fprintf(stderr, "test_regexp_interrupt: %s\n", message);
    exit(1);
}

static int interrupt_handler(JSRuntime *rt, void *opaque)
{
    InterruptState *state = opaque;

    state->calls++;
    return state->stop;
}

static JSValue eval(JSContext *ctx, const char *source)
{
    return JS_Eval(ctx, source, strlen(source), "<regexp-interrupt>",
                   JS_EVAL_TYPE_GLOBAL);
}

static void eval_setup(JSContext *ctx, const char *source)
{
    JSValue value = eval(ctx, source);

    if (JS_IsException(value))
        fail("setup evaluation failed");
    JS_FreeValue(ctx, value);
}

static void run_case(JSRuntime *rt, JSContext *ctx, const char *expression,
                     int stop, int expect_calls)
{
    InterruptState state = { 0, stop };
    JSValue value;
    int result;

    JS_SetInterruptHandler(rt, interrupt_handler, &state);
    value = eval(ctx, expression);
    JS_SetInterruptHandler(rt, NULL, NULL);

    if (stop) {
        JSValue exception;

        if (!JS_IsException(value)) {
            JS_FreeValue(ctx, value);
            fail("regexp scan ignored an interrupt request");
        }
        exception = JS_GetException(ctx);
        JS_FreeValue(ctx, exception);
    } else {
        if (JS_IsException(value))
            fail("regexp scan failed while interrupt handler continued");
        result = JS_ToBool(ctx, value);
        JS_FreeValue(ctx, value);
        if (result != 1)
            fail("regexp scan returned an unexpected result");
    }
    if (expect_calls && state.calls == 0)
        fail("regexp scan did not poll the interrupt handler");
    if (!expect_calls && state.calls != 0)
        fail("short regexp scan polled the interrupt handler unexpectedly");
}

static void run_long_scan(JSRuntime *rt, JSContext *ctx,
                          const char *expression)
{
    run_case(rt, ctx, expression, 1, 1);
    run_case(rt, ctx, expression, 0, 1);
}

int main(void)
{
    JSRuntime *rt;
    JSContext *ctx;

    rt = JS_NewRuntime();
    if (!rt)
        fail("could not create runtime");
    ctx = JS_NewContext(rt);
    if (!ctx)
        fail("could not create context");

    eval_setup(ctx, "globalThis.subject8 = 'a'.repeat(1024 * 1024);"
                    "globalThis.sparse8 = 'x'.repeat(1024 * 1024);"
                    "globalThis.subject16 = '\\u0100'.repeat(1024 * 1024);");

    run_long_scan(rt, ctx, "!/zzzzzz/.test(subject8)");
    run_long_scan(rt, ctx, "!/\\x61aaaab/.test(subject8)");
    run_long_scan(rt, ctx, "!/abcdef[0-9]+/.test(sparse8)");
    run_long_scan(rt, ctx, "!/[a-z]bcdef/.test(sparse8)");
    run_long_scan(rt, ctx, "!/(^|[^\\\\])\"target\"/.test(subject8)");
    run_long_scan(rt, ctx, "!/(?:^|.)abcd/.test(subject16)");
    run_long_scan(rt, ctx,
                  "subject8.replace(/zzzzzz/g, 'X') === subject8");

    run_case(rt, ctx, "/aaaa/.test(subject8)", 0, 0);

    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return 0;
}

function assert(actual, expected, message) {
    if (arguments.length == 1)
        expected = true;

    if (Object.is(actual, expected))
        return;

    if (actual !== null && expected !== null
    &&  typeof actual == 'object' && typeof expected == 'object'
    &&  actual.toString() === expected.toString())
        return;

    throw Error("assertion failed: got |" + actual + "|" +
                ", expected |" + expected + "|" +
                (message ? " (" + message + ")" : ""));
}

function assert_throws(expected_error, func)
{
    var err = false;
    try {
        func();
    } catch(e) {
        err = true;
        if (!(e instanceof expected_error)) {
            throw Error("unexpected exception type");
        }
    }
    if (!err) {
        throw Error("expected exception");
    }
}

// load more elaborate version of assert if available
try { __loadScript("test_assert.js"); } catch(e) {}

/*----------------*/

function test_op1()
{
    var r, a;
    r = 1 + 2;
    assert(r, 3, "1 + 2 === 3");

    r = 1 - 2;
    assert(r, -1, "1 - 2 === -1");

    r = -1;
    assert(r, -1, "-1 === -1");

    r = +2;
    assert(r, 2, "+2 === 2");

    r = 2 * 3;
    assert(r, 6, "2 * 3 === 6");

    r = 4 / 2;
    assert(r, 2, "4 / 2 === 2");

    r = 4 % 3;
    assert(r, 1, "4 % 3 === 3");

    r = 4 << 2;
    assert(r, 16, "4 << 2 === 16");

    r = 1 << 0;
    assert(r, 1, "1 << 0 === 1");

    r = 1 << 31;
    assert(r, -2147483648, "1 << 31 === -2147483648");

    r = 1 << 32;
    assert(r, 1, "1 << 32 === 1");

    r = (1 << 31) < 0;
    assert(r, true, "(1 << 31) < 0 === true");

    r = -4 >> 1;
    assert(r, -2, "-4 >> 1 === -2");

    r = -4 >>> 1;
    assert(r, 0x7ffffffe, "-4 >>> 1 === 0x7ffffffe");

    r = 1 & 1;
    assert(r, 1, "1 & 1 === 1");

    r = 0 | 1;
    assert(r, 1, "0 | 1 === 1");

    r = 1 ^ 1;
    assert(r, 0, "1 ^ 1 === 0");

    r = ~1;
    assert(r, -2, "~1 === -2");

    r = !1;
    assert(r, false, "!1 === false");

    assert((1 < 2), true, "(1 < 2) === true");

    assert((2 > 1), true, "(2 > 1) === true");

    assert(('b' > 'a'), true, "('b' > 'a') === true");

    assert(2 ** 8, 256, "2 ** 8 === 256");
}

function test_cvt()
{
    assert((NaN | 0) === 0);
    assert((Infinity | 0) === 0);
    assert(((-Infinity) | 0) === 0);
    assert(("12345" | 0) === 12345);
    assert(("0x12345" | 0) === 0x12345);
    assert(((4294967296 * 3 - 4) | 0) === -4);

    assert(("12345" >>> 0) === 12345);
    assert(("0x12345" >>> 0) === 0x12345);
    assert((NaN >>> 0) === 0);
    assert((Infinity >>> 0) === 0);
    assert(((-Infinity) >>> 0) === 0);
    assert(((4294967296 * 3 - 4) >>> 0) === (4294967296 - 4));
    assert((19686109595169230000).toString() === "19686109595169230000");
}

function test_eq()
{
    assert(null == undefined);
    assert(undefined == null);
    assert(true == 1);
    assert(0 == false);
    assert("" == 0);
    assert("123" == 123);
    assert("122" != 123);
    assert((new Number(1)) == 1);
    assert(2 == (new Number(2)));
    assert((new String("abc")) == "abc");
    assert({} != "abc");
}

function test_inc_dec()
{
    var a, r;

    a = 1;
    r = a++;
    assert(r === 1 && a === 2, true, "++");

    a = 1;
    r = ++a;
    assert(r === 2 && a === 2, true, "++");

    a = 1;
    r = a--;
    assert(r === 1 && a === 0, true, "--");

    a = 1;
    r = --a;
    assert(r === 0 && a === 0, true, "--");

    a = {x:true};
    a.x++;
    assert(a.x, 2, "++");

    a = {x:true};
    a.x--;
    assert(a.x, 0, "--");

    a = [true];
    a[0]++;
    assert(a[0], 2, "++");

    a = {x:true};
    r = a.x++;
    assert(r === 1 && a.x === 2, true, "++");

    a = {x:true};
    r = a.x--;
    assert(r === 1 && a.x === 0, true, "--");

    a = [true];
    r = a[0]++;
    assert(r === 1 && a[0] === 2, true, "++");

    a = [true];
    r = a[0]--;
    assert(r === 1 && a[0] === 0, true, "--");
}

function F(x)
{
    this.x = x;
}

function test_op2()
{
    var a, b;
    a = new Object;
    a.x = 1;
    assert(a.x, 1, "new");
    b = new F(2);
    assert(b.x, 2, "new");

    a = {x : 2};
    assert(("x" in a), true, "in");
    assert(("y" in a), false, "in");

    a = {};
    assert((a instanceof Object), true, "instanceof");
    assert((a instanceof String), false, "instanceof");

    assert((typeof 1), "number", "typeof");
    assert((typeof Object), "function", "typeof");
    assert((typeof null), "object", "typeof");
    assert((typeof unknown_var), "undefined", "typeof");

    a = {x: 1, if: 2, async: 3};
    assert(a.if === 2);
    assert(a.async === 3);
}

function test_delete()
{
    var a, err;

    a = {x: 1, y: 1};
    assert((delete a.x), true, "delete");
    assert(("x" in a), false, "delete");

    /* the following are not tested by test262 */
    assert(delete "abc"[100], true);

    err = false;
    try {
        delete null.a;
    } catch(e) {
        err = (e instanceof TypeError);
    }
    assert(err, true, "delete");

    err = false;
    try {
        a = { f() { delete super.a; } };
        a.f();
    } catch(e) {
        err = (e instanceof ReferenceError);
    }
    assert(err, true, "delete");
}

function test_constructor()
{
    function *G() {}
    let ex
    try { new G() } catch (ex_) { ex = ex_ }
    assert(ex instanceof TypeError)
    assert(ex.message, "G is not a constructor")
}

function test_prototype()
{
    var f = function f() { };
    assert(f.prototype.constructor, f, "prototype");

    var g = function g() { };
    /* QuickJS bug */
    Object.defineProperty(g, "prototype", { writable: false });
    assert(g.prototype.constructor, g, "prototype");
}

function test_arguments()
{
    function f2() {
        assert(arguments.length, 2, "arguments");
        assert(arguments[0], 1, "arguments");
        assert(arguments[1], 3, "arguments");
    }
    f2(1, 3);
}

function test_class()
{
    var o;
    class C {
        constructor() {
            this.x = 10;
        }
        f() {
            return 1;
        }
        static F() {
            return -1;
        }
        get y() {
            return 12;
        }
    };
    class D extends C {
        constructor() {
            super();
            this.z = 20;
        }
        g() {
            return 2;
        }
        static G() {
            return -2;
        }
        h() {
            return super.f();
        }
        static H() {
            return super["F"]();
        }
    }

    assert(C.F() === -1);
    assert(Object.getOwnPropertyDescriptor(C.prototype, "y").get.name === "get y");

    o = new C();
    assert(o.f() === 1);
    assert(o.x === 10);

    assert(D.F() === -1);
    assert(D.G() === -2);
    assert(D.H() === -1);

    o = new D();
    assert(o.f() === 1);
    assert(o.g() === 2);
    assert(o.x === 10);
    assert(o.z === 20);
    assert(o.h() === 1);

    /* test class name scope */
    var E1 = class E { static F() { return E; } };
    assert(E1 === E1.F());

    class S {
        static x = 42;
        static y = S.x;
        static z = this.x;
    }
    assert(S.x === 42);
    assert(S.y === 42);
    assert(S.z === 42);

    class P {
        get = () => "123";
        static() { return 42; }
    }
    assert(new P().get() === "123");
    assert(new P().static() === 42);
};

function test_template()
{
    var a, b;
    b = 123;
    a = `abc${b}d`;
    assert(a, "abc123d");

    a = String.raw `abc${b}d`;
    assert(a, "abc123d");

    a = "aaa";
    b = "bbb";
    assert(`aaa${a, b}ccc`, "aaabbbccc");
}

function test_template_skip()
{
    var a = "Bar";
    var { b = `${a + `a${a}` }baz` } = {};
    assert(b, "BaraBarbaz");
}

function test_object_literal()
{
    var x = 0, get = 1, set = 2, async = 3;
    a = { get: 2, set: 3, async: 4, get a(){ return this.get} };
    assert(JSON.stringify(a), '{"get":2,"set":3,"async":4,"a":2}');
    assert(a.a === 2);

    a = { x, get, set, async };
    assert(JSON.stringify(a), '{"x":0,"get":1,"set":2,"async":3}');
}

function test_regexp_skip()
{
    var a, b;
    [a, b = /abc\(/] = [1];
    assert(a === 1);

    [a, b =/abc\(/] = [2];
    assert(a === 2);
}

function test_labels()
{
    do x: { break x; } while(0);
    if (1)
        x: { break x; }
    else
        x: { break x; }
    with ({}) x: { break x; };
    while (0) x: { break x; };
}

function test_labels2()
{
    while (1) label: break
    var i = 0
    while (i < 3) label: {
        if (i > 0)
            break
        i++
    }
    assert(i, 1)
    for (;;) label: break
    for (i = 0; i < 3; i++) label: {
        if (i > 0)
            break
    }
    assert(i, 1)
}

function test_destructuring()
{
    function * g () { return 0; };
    var [x] = g();
    assert(x, void 0);
}

function test_spread()
{
    var x;
    x = [1, 2, ...[3, 4]];
    assert(x.toString(), "1,2,3,4");

    x = [ ...[ , ] ];
    assert(Object.getOwnPropertyNames(x).toString(), "0,length");
}

function test_function_length()
{
    assert( ((a, b = 1, c) => {}).length, 1);
    assert( (([a,b]) => {}).length, 1);
    assert( (({a,b}) => {}).length, 1);
    assert( ((c, [a,b] = 1, d) => {}).length, 1);
}

function test_argument_scope()
{
    var f;
    var c = "global";

    (function() {
        "use strict";
        // XXX: node only throws in strict mode
        f = function(a = eval("var arguments")) {};
        assert_throws(SyntaxError, f);
    })();

    f = function(a = eval("1"), b = arguments[0]) { return b; };
    assert(f(12), 12);

    f = function(a, b = arguments[0]) { return b; };
    assert(f(12), 12);

    f = function(a, b = () => arguments) { return b; };
    assert(f(12)()[0], 12);

    f = function(a = eval("1"), b = () => arguments) { return b; };
    assert(f(12)()[0], 12);

    (function() {
        "use strict";
        f = function(a = this) { return a; };
        assert(f.call(123), 123);

        f = function f(a = f) { return a; };
        assert(f(), f);

        f = function f(a = eval("f")) { return a; };
        assert(f(), f);
    })();

    f = (a = eval("var c = 1"), probe = () => c) => {
        var c = 2;
        assert(c, 2);
        assert(probe(), 1);
    }
    f();

    f = (a = eval("var arguments = 1"), probe = () => arguments) => {
        var arguments = 2;
        assert(arguments, 2);
        assert(probe(), 1);
    }
    f();

    f = function f(a = eval("var c = 1"), b = c, probe = () => c) {
        assert(b, 1);
        assert(c, 1);
        assert(probe(), 1)
    }
    f();

    assert(c, "global");
    f = function f(a, b = c, probe = () => c) {
        eval("var c = 1");
        assert(c, 1);
        assert(b, "global");
        assert(probe(), "global")
    }
    f();
    assert(c, "global");

    f = function f(a = eval("var c = 1"), probe = (d = eval("c")) => d) {
        assert(probe(), 1)
    }
    f();
}

function test_function_expr_name()
{
    var f;

    /* non strict mode test : assignment to the function name silently
       fails */

    f = function myfunc() {
        myfunc = 1;
        return myfunc;
    };
    assert(f(), f);

    f = function myfunc() {
        myfunc = 1;
        (() => {
            myfunc = 1;
        })();
        return myfunc;
    };
    assert(f(), f);

    f = function myfunc() {
        eval("myfunc = 1");
        return myfunc;
    };
    assert(f(), f);

    /* strict mode test : assignment to the function name raises a
       TypeError exception */

    f = function myfunc() {
        "use strict";
        myfunc = 1;
    };
    assert_throws(TypeError, f);

    f = function myfunc() {
        "use strict";
        (() => {
            myfunc = 1;
        })();
    };
    assert_throws(TypeError, f);

    f = function myfunc() {
        "use strict";
        eval("myfunc = 1");
    };
    assert_throws(TypeError, f);
}

function test_parse_semicolon()
{
    /* 'yield' or 'await' may not be considered as a token if the
       previous ';' is missing */
    function *f()
    {
        function func() {
        }
        yield 1;
        var h = x => x + 1
        yield 2;
    }
    async function g()
    {
        function func() {
        }
        await 1;
        var h = x => x + 1
        await 2;
    }
}

function test_parse_arrow_function()
{
    assert(typeof eval("() => {}\n() => {}"), "function");
    assert(eval("() => {}\n+1"), 1);
    assert(typeof eval("x => {}\n() => {}"), "function");
    assert(typeof eval("async () => {}\n() => {}"), "function");
    assert(typeof eval("async x => {}\n() => {}"), "function");
}

/* optional chaining tests not present in test262 */
function test_optional_chaining()
{
    var a, z;
    z = null;
    a = { b: { c: 2 } };
    assert(delete z?.b.c, true);
    assert(delete a?.b.c, true);
    assert(JSON.stringify(a), '{"b":{}}', "optional chaining delete");

    a = { b: { c: 2 } };
    assert(delete z?.b["c"], true);
    assert(delete a?.b["c"], true);
    assert(JSON.stringify(a), '{"b":{}}');

    a = {
        b() { return this._b; },
        _b: { c: 42 }
    };

    assert((a?.b)().c, 42);

    assert((a?.["b"])().c, 42);
}

function test_unicode_ident()
{
    var Ãµ = 3;
    assert(typeof õ, "undefined");
}

/* check global variable optimization */
function test_global_var_opt()
{
    var v2;
    (1, eval)('var gvar1'); /* create configurable global variables */

    gvar1 = 1;
    Object.defineProperty(globalThis, "gvar1", { writable: false });
    gvar1 = 2;
    assert(gvar1, 1);

    Object.defineProperty(globalThis, "gvar1", { get: function() { return "hello" },
                                                 set: function(v) { v2 = v; } });
    assert(gvar1, "hello");
    gvar1 = 3;
    assert(v2, 3);

    Object.defineProperty(globalThis, "gvar1", { value: 4, writable: true, configurable: true });
    assert(gvar1, 4);
    gvar1 = 6;
    
    delete gvar1;
    assert_throws(ReferenceError, function() { return gvar1 });
    gvar1 = 5;
    assert(gvar1, 5);
}

function test_number_literals()
{
    assert(0.1.a, undefined);
    assert(0x1.a, undefined);
    assert(0b1.a, undefined);
    assert(01.a, undefined);
    assert(0o1.a, undefined);
    assert_throws(SyntaxError, () => eval('0.a'));
}

function assert_array_eq(actual, expected, message)
{
    assert(Array.isArray(actual), true, message);
    assert(actual.length, expected.length, message);
    for (var i = 0; i < expected.length; i++) {
        assert(actual[i], expected[i],
               (message ? message + ": " : "") + "index " + i);
    }
}

/* TC39 explicit resource management: `using` declaration */
function test_using()
{
    /* AddDisposableResource validates and snapshots the dispose method at
       declaration; the body must not run on failure. */

    /* Missing Symbol.dispose → TypeError before the body runs. */
    (function() {
        var bodyRan = false;
        var caught;
        try {
            (function() { using x = {}; bodyRan = true; })();
        } catch (e) { caught = e; }
        assert(bodyRan, false);
        assert(caught instanceof TypeError, true);
    })();

    /* Non-object, non-null → TypeError. */
    (function() {
        var bodyRan = false;
        var caught;
        try {
            (function() { using x = 42; bodyRan = true; })();
        } catch (e) { caught = e; }
        assert(bodyRan, false);
        assert(caught instanceof TypeError, true);
    })();

    /* Symbol.dispose getter throwing propagates at declaration. */
    (function() {
        var bodyRan = false;
        var caught;
        try {
            (function() {
                var o = {};
                Object.defineProperty(o, Symbol.dispose, {
                    get() { throw new Error("getter"); }
                });
                using y = o;
                bodyRan = true;
            })();
        } catch (e) { caught = e; }
        assert(bodyRan, false);
        assert(caught instanceof Error, true);
        assert(caught.message, "getter");
    })();

    /* Non-callable dispose → TypeError. */
    (function() {
        var bodyRan = false;
        var caught;
        try {
            (function() {
                using x = { [Symbol.dispose]: 123 };
                bodyRan = true;
            })();
        } catch (e) { caught = e; }
        assert(bodyRan, false);
        assert(caught instanceof TypeError, true);
    })();

    /* null / undefined are spec-permitted and must not throw. */
    (function() {
        var disposed = 0;
        (function() {
            using a = null;
            using b = undefined;
            using c = { [Symbol.dispose]() { disposed++; } };
        })();
        assert(disposed, 1);
    })();

    /* for-of using: validation applies per-iteration. */
    (function() {
        var iters = 0;
        var caught;
        try {
            (function() {
                for (using x of [{ [Symbol.dispose]() {} }, 42]) {
                    iters++;
                }
            })();
        } catch (e) { caught = e; }
        assert(iters, 1);
        assert(caught instanceof TypeError, true);
    })();

    /* for-of using: normal completion must not throw (regression for
       a prior bug where each iteration threw `undefined`). */
    (function() {
        var log = [];
        (function() {
            for (using x of [
                { tag: "a", [Symbol.dispose]() { log.push(this.tag); } },
                { tag: "b", [Symbol.dispose]() { log.push(this.tag); } },
                { tag: "c", [Symbol.dispose]() { log.push(this.tag); } },
            ]) {
                log.push("iter");
            }
        })();
        assert_array_eq(log, ["iter", "a", "iter", "b", "iter", "c"]);
    })();

    /* LIFO disposal order. */
    (function() {
        var log = [];
        (function() {
            using a = { [Symbol.dispose]() { log.push("a"); } };
            using b = { [Symbol.dispose]() { log.push("b"); } };
            using c = { [Symbol.dispose]() { log.push("c"); } };
        })();
        assert_array_eq(log, ["c", "b", "a"]);
    })();

    /* Multiple dispose errors chain as SuppressedError (error=new,
       suppressed=previous). */
    (function() {
        var caught;
        try {
            (function() {
                using a = { [Symbol.dispose]() { throw new Error("a"); } };
                using b = { [Symbol.dispose]() { throw new Error("b"); } };
                using c = { [Symbol.dispose]() { throw new Error("c"); } };
            })();
        } catch (e) { caught = e; }
        /* LIFO: c first → error_state=c. b → Suppressed(b, c).
           a → Suppressed(a, Suppressed(b, c)). */
        assert(caught instanceof SuppressedError, true);
        assert(caught.error.message, "a");
        assert(caught.suppressed instanceof SuppressedError, true);
        assert(caught.suppressed.error.message, "b");
        assert(caught.suppressed.suppressed.message, "c");
    })();

    /* Symbol.dispose is read exactly once at declaration (snapshot). */
    (function() {
        var reads = 0;
        var disposed = 0;
        var target = {};
        Object.defineProperty(target, Symbol.dispose, {
            get() {
                reads++;
                return function() { disposed++; };
            }
        });
        (function() { using x = target; })();
        assert(reads, 1);
        assert(disposed, 1);
    })();

    /* DisposableStack.prototype.move uses the %DisposableStack%
       intrinsic; tampering with or deleting the global binding must
       not redirect it. */
    (function() {
        var saved = globalThis.DisposableStack;
        try {
            var ds = new saved();
            var disposed = 0;
            ds.use({ [Symbol.dispose]() { disposed++; } });
            globalThis.DisposableStack = class Other {};
            var moved = ds.move();
            assert(Object.getPrototypeOf(moved), saved.prototype);
            moved.dispose();
            assert(disposed, 1);
            delete globalThis.DisposableStack;
            var ds2 = new saved();
            ds2.use({ [Symbol.dispose]() {} });
            var moved2 = ds2.move();
            assert(Object.getPrototypeOf(moved2), saved.prototype);
        } finally {
            globalThis.DisposableStack = saved;
        }
    })();

    /* SuppressedError wrapping can't be hijacked via prototype.constructor. */
    (function() {
        var savedCtor = SuppressedError.prototype.constructor;
        try {
            var hijacked = false;
            SuppressedError.prototype.constructor = function() {
                hijacked = true;
                throw new Error("hijack");
            };
            var caught;
            try {
                (function() {
                    using a = { [Symbol.dispose]() { throw new Error("a"); } };
                    using b = { [Symbol.dispose]() { throw new Error("b"); } };
                })();
            } catch (e) { caught = e; }
            assert(hijacked, false);
            assert(caught instanceof SuppressedError, true);
            assert(caught.error.message, "a");
            assert(caught.suppressed.message, "b");
        } finally {
            SuppressedError.prototype.constructor = savedCtor;
        }
    })();
}

/* Async counterpart: `await using` and AsyncDisposableStack. */
async function test_await_using()
{
    /* await using validates the async/sync dispose method at declaration. */
    {
        var bodyRan = false;
        var caught;
        try {
            await (async function() {
                await using x = {};
                bodyRan = true;
            })();
        } catch (e) { caught = e; }
        assert(bodyRan, false);
        assert(caught instanceof TypeError, true);
    }

    /* Falls back to Symbol.dispose when @@asyncDispose is absent. */
    {
        var ok = false;
        await (async function() {
            await using x = { [Symbol.dispose]() {} };
            ok = true;
        })();
        assert(ok, true);
    }

    /* AsyncDisposableStack.disposeAsync must await each dispose's returned
       promise before invoking the next one (regression for a prior bug
       that called all disposes in a single synchronous pass). */
    {
        var log = [];
        var stack = new AsyncDisposableStack();
        stack.use({
            async [Symbol.asyncDispose]() { log.push("outer-called"); }
        });
        stack.use({
            async [Symbol.asyncDispose]() {
                log.push("inner-called");
                await null;
                log.push("inner-after-await");
            }
        });
        var p = stack.disposeAsync();
        log.push("after-sync-dispose");
        await p;
        assert_array_eq(log, [
            "inner-called",
            "after-sync-dispose",
            "inner-after-await",
            "outer-called"
        ]);
    }

    /* disposeAsync called twice without awaiting the first must still
       dispose all resources in LIFO order. */
    {
        var log = [];
        var stack = new AsyncDisposableStack();
        stack.use({ [Symbol.asyncDispose]() { log.push(42); } });
        stack.use({ [Symbol.asyncDispose]() { log.push(43); } });
        stack.disposeAsync();
        assert(stack.disposed, true);
        await stack.disposeAsync();
        assert_array_eq(log, [43, 42]);
    }

    /* LIFO. */
    {
        var log = [];
        var stack = new AsyncDisposableStack();
        stack.use({ [Symbol.asyncDispose]() { log.push("a"); } });
        stack.use({ [Symbol.asyncDispose]() { log.push("b"); } });
        stack.use({ [Symbol.asyncDispose]() { log.push("c"); } });
        await stack.disposeAsync();
        assert_array_eq(log, ["c", "b", "a"]);
    }

    /* SuppressedError chaining across async dispose errors. */
    {
        var stack = new AsyncDisposableStack();
        stack.use({
            async [Symbol.asyncDispose]() { throw new Error("first"); }
        });
        stack.use({
            async [Symbol.asyncDispose]() { throw new Error("second"); }
        });
        var caught;
        try {
            await stack.disposeAsync();
        } catch (e) { caught = e; }
        /* LIFO: second throws → completion=second. first throws →
           Suppressed(error=first, suppressed=second). */
        assert(caught instanceof SuppressedError, true);
        assert(caught.error.message, "first");
        assert(caught.suppressed.message, "second");
    }

    /* disposeAsync always resolves to undefined. */
    {
        var stack = new AsyncDisposableStack();
        stack.use({ [Symbol.asyncDispose]() { return 42; } });
        stack.use({ async [Symbol.asyncDispose]() { return "hello"; } });
        var result = await stack.disposeAsync();
        assert(result, undefined);
    }
}

test_op1();
test_cvt();
test_eq();
test_inc_dec();
test_op2();
test_constructor();
test_delete();
test_prototype();
test_arguments();
test_class();
test_template();
test_template_skip();
test_object_literal();
test_regexp_skip();
test_labels();
test_labels2();
test_destructuring();
test_spread();
test_function_length();
test_argument_scope();
test_function_expr_name();
test_parse_semicolon();
test_optional_chaining();
test_parse_arrow_function();
test_unicode_ident();
test_global_var_opt();
test_number_literals();
test_using();
test_await_using().catch(e => { throw e; });

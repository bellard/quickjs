function assert(actual, expected, message) {
    if (arguments.length == 1)
        expected = true;

    if (actual === expected)
        return;

    if (actual !== null && expected !== null
    &&  typeof actual == 'object' && typeof expected == 'object'
    &&  actual.toString() === expected.toString())
        return;

    throw Error("assertion failed: got |" + actual + "|" +
                ", expected |" + expected + "|" +
                (message ? " (" + message + ")" : ""));
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

function test_prototype()
{
    function f() { }
    assert(f.prototype.constructor, f, "prototype");
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
    var x = 0, get = 1, set = 2; async = 3;
    a = { get: 2, set: 3, async: 4 };
    assert(JSON.stringify(a), '{"get":2,"set":3,"async":4}');

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

test_op1();
test_cvt();
test_eq();
test_inc_dec();
test_op2();
test_delete();
test_prototype();
test_arguments();
test_class();
test_template();
test_template_skip();
test_object_literal();
test_regexp_skip();
test_labels();
test_destructuring();
test_spread();

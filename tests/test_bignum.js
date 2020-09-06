"use strict";

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

function assertThrows(err, func)
{
    var ex;
    ex = false;
    try {
        func();
    } catch(e) {
        ex = true;
        assert(e instanceof err);
    }
    assert(ex, true, "exception expected");
}

// load more elaborate version of assert if available
try { __loadScript("test_assert.js"); } catch(e) {}

/*----------------*/

function bigint_pow(a, n)
{
    var r, i;
    r = 1n;
    for(i = 0n; i < n; i++)
        r *= a;
    return r;
}

/* a must be < b */
function test_less(a, b)
{
    assert(a < b);
    assert(!(b < a));
    assert(a <= b);
    assert(!(b <= a));
    assert(b > a);
    assert(!(a > b));
    assert(b >= a);
    assert(!(a >= b));
    assert(a != b);
    assert(!(a == b));
}

/* a must be numerically equal to b */
function test_eq(a, b)
{
    assert(a == b);
    assert(b == a);
    assert(!(a != b));
    assert(!(b != a));
    assert(a <= b);
    assert(b <= a);
    assert(!(a < b));
    assert(a >= b);
    assert(b >= a);
    assert(!(a > b));
}

function test_bigint1()
{
    var a, r;

    test_less(2n, 3n);
    test_eq(3n, 3n);

    test_less(2, 3n);
    test_eq(3, 3n);

    test_less(2.1, 3n);
    test_eq(Math.sqrt(4), 2n);

    a = bigint_pow(3n, 100n);
    assert((a - 1n) != a);
    assert(a == 515377520732011331036461129765621272702107522001n);
    assert(a == 0x5a4653ca673768565b41f775d6947d55cf3813d1n);

    r = 1n << 31n;
    assert(r, 2147483648n, "1 << 31n === 2147483648n");
    
    r = 1n << 32n;
    assert(r, 4294967296n, "1 << 32n === 4294967296n");
}

function test_bigint2()
{
    assert(BigInt(""), 0n);
    assert(BigInt("  123"), 123n);
    assert(BigInt("  123   "), 123n);
    assertThrows(SyntaxError, () => { BigInt("+") } );
    assertThrows(SyntaxError, () => { BigInt("-") } );
    assertThrows(SyntaxError, () => { BigInt("\x00a") } );
    assertThrows(SyntaxError, () => { BigInt("  123  r") } );
}

function test_divrem(div1, a, b, q)
{
    var div, divrem, t;
    div = BigInt[div1];
    divrem = BigInt[div1 + "rem"];
    assert(div(a, b) == q);
    t = divrem(a, b);
    assert(t[0] == q);
    assert(a == b * q + t[1]);
}

function test_idiv1(div, a, b, r)
{
    test_divrem(div, a, b, r[0]);
    test_divrem(div, -a, b, r[1]);
    test_divrem(div, a, -b, r[2]);
    test_divrem(div, -a, -b, r[3]);
}

/* QuickJS BigInt extensions */
function test_bigint_ext()
{
    var r;
    assert(BigInt.floorLog2(0n) === -1n);
    assert(BigInt.floorLog2(7n) === 2n);

    assert(BigInt.sqrt(0xffffffc000000000000000n) === 17592185913343n);
    r = BigInt.sqrtrem(0xffffffc000000000000000n);
    assert(r[0] === 17592185913343n);
    assert(r[1] === 35167191957503n);

    test_idiv1("tdiv", 3n, 2n, [1n, -1n, -1n, 1n]);
    test_idiv1("fdiv", 3n, 2n, [1n, -2n, -2n, 1n]);
    test_idiv1("cdiv", 3n, 2n, [2n, -1n, -1n, 2n]);
    test_idiv1("ediv", 3n, 2n, [1n, -2n, -1n, 2n]);
}

function test_bigfloat()
{
    var e, a, b, sqrt2;
    
    assert(typeof 1n === "bigint");
    assert(typeof 1l === "bigfloat");
    assert(1 == 1.0l);
    assert(1 !== 1.0l);

    test_less(2l, 3l);
    test_eq(3l, 3l);

    test_less(2, 3l);
    test_eq(3, 3l);

    test_less(2.1, 3l);
    test_eq(Math.sqrt(9), 3l);
    
    test_less(2n, 3l);
    test_eq(3n, 3l);

    e = new BigFloatEnv(128);
    assert(e.prec == 128);
    a = BigFloat.sqrt(2l, e);
    assert(a == BigFloat.parseFloat("0x1.6a09e667f3bcc908b2fb1366ea957d3e", 0, e));
    assert(e.inexact === true);
    assert(BigFloat.fpRound(a) == 0x1.6a09e667f3bcc908b2fb1366ea95l);
    
    b = BigFloatEnv.setPrec(BigFloat.sqrt.bind(null, 2), 128);
    assert(a == b);
}

function test_bigdecimal()
{
    assert(1d === 1d);
    assert(1d !== 2d);
    test_less(1d, 2d);
    test_eq(2d, 2d);
    
    test_less(1, 2d);
    test_eq(2, 2d);

    test_less(1.1, 2d);
    test_eq(Math.sqrt(4), 2d);
    
    test_less(2n, 3d);
    test_eq(3n, 3d);
    
    assert(BigDecimal("1234.1") === 1234.1d);
    assert(BigDecimal("    1234.1") === 1234.1d);
    assert(BigDecimal("    1234.1  ") === 1234.1d);

    assert(BigDecimal(0.1) === 0.1d);
    assert(BigDecimal(123) === 123d);
    assert(BigDecimal(true) === 1d);

    assert(123d + 1d === 124d);
    assert(123d - 1d === 122d);

    assert(3.2d * 3d === 9.6d);
    assert(10d / 2d === 5d);
    assertThrows(RangeError, () => { 10d / 3d } );
    assert(BigDecimal.div(20d, 3d,
                       { roundingMode: "half-even",
                         maximumSignificantDigits: 3 }) === 6.67d);
    assert(BigDecimal.div(20d, 3d,
                       { roundingMode: "half-even",
                         maximumFractionDigits: 3 }) === 6.667d);

    assert(10d % 3d === 1d);
    assert(-10d % 3d === -1d);

    assert(-10d % 3d === -1d);

    assert(1234.5d ** 3d === 1881365963.625d);
    assertThrows(RangeError, () => { 2d ** 3.1d } );
    assertThrows(RangeError, () => { 2d ** -3d } );
    
    assert(BigDecimal.sqrt(2d,
                       { roundingMode: "half-even",
                         maximumSignificantDigits: 4 }) === 1.414d);
    
    assert(BigDecimal.round(3.14159d,
                       { roundingMode: "half-even",
                         maximumFractionDigits: 3 }) === 3.142d);
}

test_bigint1();
test_bigint2();
test_bigint_ext();
test_bigfloat();
test_bigdecimal();

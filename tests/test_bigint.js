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

function test_bigint3()
{
    assert(Number(0xffffffffffffffffn), 18446744073709552000);
    assert(Number(-0xffffffffffffffffn), -18446744073709552000);
    assert(100000000000000000000n == 1e20, true);
    assert(100000000000000000001n == 1e20, false);
    assert((1n << 100n).toString(10), "1267650600228229401496703205376");
    assert((-1n << 100n).toString(36), "-3ewfdnca0n6ld1ggvfgg");
    assert((1n << 100n).toString(8), "2000000000000000000000000000000000");

    assert(0x5a4653ca673768565b41f775n << 78n, 8443945299673273647701379149826607537748959488376832n);
    assert(-0x5a4653ca673768565b41f775n << 78n, -8443945299673273647701379149826607537748959488376832n);
    assert(0x5a4653ca673768565b41f775n >> 78n, 92441n);
    assert(-0x5a4653ca673768565b41f775n >> 78n, -92442n);

    assert(~0x5a653ca6n, -1516584103n);
    assert(0x5a463ca6n | 0x67376856n, 2138537206n);
    assert(0x5a463ca6n & 0x67376856n, 1107699718n);
    assert(0x5a463ca6n ^ 0x67376856n, 1030837488n);

    assert(3213213213213213432453243n / 123434343439n, 26031760073331n);
    assert(-3213213213213213432453243n / 123434343439n, -26031760073331n);
    assert(-3213213213213213432453243n % -123434343439n, -26953727934n);
    assert(3213213213213213432453243n % 123434343439n, 26953727934n);

    assert((-2n) ** 127n, -170141183460469231731687303715884105728n);
    assert((2n) ** 127n, 170141183460469231731687303715884105728n);
    assert((-256n) ** 11n, -309485009821345068724781056n);
    assert((7n) ** 20n, 79792266297612001n);
}

/* pi computation */

/* return floor(log2(a)) for a > 0 and 0 for a = 0 */
function floor_log2(a)
{
    var k_max, a1, k, i;
    k_max = 0n;
    while ((a >> (2n ** k_max)) != 0n) {
        k_max++;
    }
    k = 0n;
    a1 = a;
    for(i = k_max - 1n; i >= 0n; i--) {
        a1 = a >> (2n ** i);
        if (a1 != 0n) {
            a = a1;
            k |= (1n << i);
        }
    }
    return k;
}

/* return ceil(log2(a)) for a > 0 */
function ceil_log2(a)
{
    return floor_log2(a - 1n) + 1n;
}

/* return floor(sqrt(a)) (not efficient but simple) */
function int_sqrt(a)
{
    var l, u, s;
    if (a == 0n)
        return a;
    l = ceil_log2(a);
    u = 1n << ((l + 1n) / 2n);
    /* u >= floor(sqrt(a)) */
    for(;;) {
        s = u;
        u = ((a / s) + s) / 2n;
        if (u >= s)
            break;
    }
    return s;
}

/* return pi * 2**prec */
function calc_pi(prec) {
    const CHUD_A = 13591409n;
    const CHUD_B = 545140134n;
    const CHUD_C = 640320n;
    const CHUD_C3 = 10939058860032000n; /* C^3/24 */
    const CHUD_BITS_PER_TERM = 47.11041313821584202247; /* log2(C/12)*3 */

    /* return [P, Q, G] */
    function chud_bs(a, b, need_G) {
        var c, P, Q, G, P1, Q1, G1, P2, Q2, G2;
        if (a == (b - 1n)) {
            G = (2n * b - 1n) * (6n * b - 1n) * (6n * b - 5n);
            P = G * (CHUD_B * b + CHUD_A);
            if (b & 1n)
                P = -P;
            Q = b * b * b * CHUD_C3;
        } else {
            c = (a + b) >> 1n;
            [P1, Q1, G1] = chud_bs(a, c, true);
            [P2, Q2, G2] = chud_bs(c, b, need_G);
            P = P1 * Q2 + P2 * G1;
            Q = Q1 * Q2;
            if (need_G)
                G = G1 * G2;
            else
                G = 0n;
        }
        return [P, Q, G];
    }

    var n, P, Q, G;
    /* number of serie terms */
    n = BigInt(Math.ceil(Number(prec) / CHUD_BITS_PER_TERM)) + 10n;
    [P, Q, G] = chud_bs(0n, n, false);
    Q = (CHUD_C / 12n) * (Q << prec) / (P + Q * CHUD_A);
    G = int_sqrt(CHUD_C << (2n * prec));
    return (Q * G) >> prec;
}

function compute_pi(n_digits) {
    var r, n_digits, n_bits, out;
    /* we add more bits to reduce the probability of bad rounding for
      the last digits */
    n_bits = BigInt(Math.ceil(n_digits * Math.log2(10))) + 32n;
    r = calc_pi(n_bits);
    r = ((10n ** BigInt(n_digits)) * r) >> n_bits;
    out = r.toString();
    return out[0] + "." + out.slice(1);
}

function test_pi()
{
    assert(compute_pi(2000), "3.14159265358979323846264338327950288419716939937510582097494459230781640628620899862803482534211706798214808651328230664709384460955058223172535940812848111745028410270193852110555964462294895493038196442881097566593344612847564823378678316527120190914564856692346034861045432664821339360726024914127372458700660631558817488152092096282925409171536436789259036001133053054882046652138414695194151160943305727036575959195309218611738193261179310511854807446237996274956735188575272489122793818301194912983367336244065664308602139494639522473719070217986094370277053921717629317675238467481846766940513200056812714526356082778577134275778960917363717872146844090122495343014654958537105079227968925892354201995611212902196086403441815981362977477130996051870721134999999837297804995105973173281609631859502445945534690830264252230825334468503526193118817101000313783875288658753320838142061717766914730359825349042875546873115956286388235378759375195778185778053217122680661300192787661119590921642019893809525720106548586327886593615338182796823030195203530185296899577362259941389124972177528347913151557485724245415069595082953311686172785588907509838175463746493931925506040092770167113900984882401285836160356370766010471018194295559619894676783744944825537977472684710404753464620804668425906949129331367702898915210475216205696602405803815019351125338243003558764024749647326391419927260426992279678235478163600934172164121992458631503028618297455570674983850549458858692699569092721079750930295532116534498720275596023648066549911988183479775356636980742654252786255181841757467289097777279380008164706001614524919217321721477235014144197356854816136115735255213347574184946843852332390739414333454776241686251898356948556209921922218427255025425688767179049460165346680498862723279178608578438382796797668145410095388378636095068006422512520511739298489608412848862694560424196528502221066118630674427862203919494504712371378696095636437191728746776465757396241389086583264599581339047802759009");
}

test_bigint1();
test_bigint2();
test_bigint3();
test_pi();

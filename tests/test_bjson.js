import * as bjson from "./bjson.so";

function assert(b, str)
{
    if (b) {
        return;
    } else {
        throw Error("assertion failed: " + str);
    }
}

function toHex(a)
{
    var i, s = "", tab, v;
    tab = new Uint8Array(a);
    for(i = 0; i < tab.length; i++) {
        v = tab[i].toString(16);
        if (v.length < 2)
            v = "0" + v;
        if (i !== 0)
            s += " ";
        s += v;
    }
    return s;
}

function toStr(a)
{
    var s, i, props, prop;

    switch(typeof(a)) {
    case "object":
        if (a === null)
            return "null";
        if (Array.isArray(a)) {
            s = "[";
            for(i = 0; i < a.length; i++) {
                if (i != 0)
                    s += ",";
                s += toStr(a[i]);
            }
            s += "]";
        } else {
            props = Object.keys(a);
            s = "{";
            for(i = 0; i < props.length; i++) {
                if (i != 0)
                    s += ",";
                prop = props[i];
                s += prop + ":" + toStr(a[prop]);
            }
            s += "}";
        }
        return s;
    case "undefined":
        return "undefined";
    case "string":
        return a.__quote();
    case "number":
    case "bigfloat":
        if (a == 0 && 1 / a < 0)
            return "-0";
        else
            return a.toString();
        break;
    default:
        return a.toString();
    }
}

function bjson_test(a)
{
    var buf, r, a_str, r_str;
    a_str = toStr(a);
    buf = bjson.write(a);
    if (0) {
        print(a_str, "->", toHex(buf));
    }
    r = bjson.read(buf, 0, buf.byteLength);
    r_str = toStr(r);
    if (a_str != r_str) {
        print(a_str);
        print(r_str);
        assert(false);
    }
}

function bjson_test_all()
{
    var obj;
    
    bjson_test({x:1, y:2, if:3});
    bjson_test([1, 2, 3]);
    bjson_test([1.0, "aa", true, false, undefined, null, NaN, -Infinity, -0.0]);
    if (typeof BigInt !== "undefined") {
        bjson_test([BigInt("1"), -BigInt("0x123456789"),
               BigInt("0x123456789abcdef123456789abcdef")]);
    }
    if (typeof BigFloat !== "undefined") {
        BigFloatEnv.setPrec(function () {
            bjson_test([BigFloat("0.1"), BigFloat("-1e30"), BigFloat("0"),
                   BigFloat("-0"), BigFloat("Infinity"), BigFloat("-Infinity"),
                   0.0 / BigFloat("0"), BigFloat.MAX_VALUE,
                   BigFloat.MIN_VALUE]);
        }, 113, 15);
    }
    if (typeof BigDecimal !== "undefined") {
        bjson_test([BigDecimal("0"),
                    BigDecimal("0.8"), BigDecimal("123321312321321e100"),
                    BigDecimal("-1233213123213214332333223332e100"),
                    BigDecimal("1.233e-1000")]);
    }

    /* tested with a circular reference */
    obj = {};
    obj.x = obj;
    try {
        bjson.write(obj);
        assert(false);
    } catch(e) {
        assert(e instanceof TypeError);
    }
}

bjson_test_all();

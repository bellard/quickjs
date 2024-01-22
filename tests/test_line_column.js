"use strict";

function assert(actual, expected, message) {
    if (arguments.length == 1) expected = true;

    if (actual === expected) return;

    if (actual !== null && expected !== null && typeof actual == 'object' &&
        typeof expected == 'object' && actual.toString() === expected.toString())
        return;

    throw Error(
        'assertion failed: got |' + actual + '|' +
        ', expected |' + expected + '|' + (message ? ' (' + message + ')' : ''));
}

/** id not exists -> should be located at id */
function test_line_column1() {
    try {
        eval(`'【';A;`);
    } catch (e) {
        assert(e.lineNumber, 1, 'test_line_column1');
        assert(e.columnNumber, 5, 'test_line_column1');
    }
}

/**
 * memeber call should be located at id:
 * a.b.c() and c is null -> c will be located
 */
function test_line_column2() {
    try {
        eval(`
var a = { b: { c: { d: null }} };
a.b.c.d();
        `);
    } catch (e) {
        assert(e.lineNumber, 3, 'test_line_column2');
        assert(e.columnNumber, 7, 'test_line_column2');
    }
}

/**
 * memeber call should be located at id:
 * a.b.c() and b is null -> c will be located
 */
function test_line_column3() {
    try {
        eval(`
var a = { b: { c: { d: null }} };
a.f.c.d();
        `);
    } catch (e) {
        assert(e.lineNumber, 3, 'test_line_column3');
        assert(e.columnNumber, 5, 'test_line_column3');
    }
}

/** if id not exists -> should be located at id */
function test_line_column4() {
    try {
        eval(`(function(){'use strict';a;}());`);
    } catch (e) {
        assert(e.lineNumber, 1, 'test_line_column4');
        assert(e.columnNumber, 26, 'test_line_column4');
    }
}

/** if id not exists -> should be located at id */
function test_line_column5() {
    try {
        eval(`'【';1+1;new A();`);
    } catch (e) {
        assert(e.lineNumber, 1, 'test_line_column5');
        assert(e.columnNumber, 13, 'test_line_column5');
    }
}

/** new call should be located at 'new' */
function test_line_column6() {
    try {
        eval(`'【';1+1;throw new Error();`);
    } catch (e) {
        assert(e.lineNumber, 1, 'test_line_column6');
        assert(e.columnNumber, 15, 'test_line_column6');
    }
}

/**
 * normal call should be located at function name:
 * a() and a is null or occur error -> a will be located
 */
function test_line_column7() {
    try {
        eval(`1+1;a();`);
    } catch (e) {
        assert(e.lineNumber, 1, 'test_line_column7');
        assert(e.columnNumber, 5, 'test_line_column7');
    }
}

/** 
 * if comment is first line, 
 * the line number of one line should be locate at next line 
 */
function test_line_column8() {
    try {
        eval(`
/**
 * something
 * comment
 * here
 */
1+1;a();
        `);
    } catch (e) {
        assert(e.lineNumber, 7, 'test_line_column8');
        assert(e.columnNumber, 5, 'test_line_column8');
    }
}

/** nest function call */
function test_line_column9() {
    try {
        eval(`(function(){'【'(function(){'use strict';a;}())}())`);
    } catch (e) {
        assert(e.lineNumber, 1, 'test_line_column9');
        assert(e.columnNumber, 41, 'test_line_column9');
    }
}

/** multi function call */
function test_line_column10() {
    try {
        eval(`
function a(){
    throw new Error();
}

function b(){
    a();
}

b();
        `);
    } catch (e) {
        assert(e.lineNumber, 3, 'test_line_column10');
        assert(e.columnNumber, 11, 'test_line_column10');
    }
}

/** syntax error should be located at latest token position */
function test_line_column11() {
    try {
        eval(`
var a = {
    b: if(1){}
}
        `);
    } catch (e) {
        assert(e.lineNumber, 3, 'test_line_column11');
        assert(e.columnNumber, 7, 'test_line_column11');
    }
}

/** string template cases */
function test_line_column12() {
// case 1
    try {
        eval(`
var a = \`\$\{b;\}
1+1
\`;
        `);
    } catch (e) {
        assert(e.lineNumber, 2, 'test_line_column12');
        assert(e.columnNumber, 12, 'test_line_column12');
    }

// case 2
    try {
        eval(`
var a = \`1+1
\$\{b;\}
2+2
\`;
        `);
    } catch (e) {
        assert(e.lineNumber, 3, 'test_line_column12');
        assert(e.columnNumber, 3, 'test_line_column12');
    }

// case 3
    try {
        eval(`
var a = \`1+1
2+2
\${b\}\`;
            `);
    } catch (e) {
        assert(e.lineNumber, 4, 'test_line_column12');
        assert(e.columnNumber, 3, 'test_line_column12');
    }

// case 4
    try {
        eval(`
var a = \`1+1
2+2
\${3+3\}\`;b;
            `);
    } catch (e) {
        assert(e.lineNumber, 4, 'test_line_column12');
        assert(e.columnNumber, 9, 'test_line_column12');
    }
}

/** dynamic Function parse error should be located the latest token */
function test_line_column13() {
    try {
        eval(`Function("===>", "a");`);
    } catch (e) {
        assert(e.lineNumber, 1, 'test_line_column13');
        assert(e.columnNumber, 20, 'test_line_column13');
    }
}

test_line_column1();
test_line_column2();
test_line_column3();
test_line_column4();
test_line_column5();
test_line_column6();
test_line_column7();
test_line_column8();
test_line_column9();
test_line_column10();
test_line_column11();
test_line_column12();
test_line_column13();
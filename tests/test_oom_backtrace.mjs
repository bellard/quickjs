// Test that an out of memory raised while building the backtrace of an
// exception does not crash the process (see issue #529).
//
// The exception object is passed to build_backtrace() by reference. The
// allocations it makes to build the stack string can run the engine out
// of memory, and the out of memory they raise frees the current exception
// - the very object it is in the middle of defining the stack property on.
//
// The loop below keeps the heap full to its limit while the exception is
// built, so the backtrace allocations fail. The error is expected, and
// the process is expected to exit normally.

function assert(actual, expected, message) {
    if (actual === expected)
        return;
    throw Error("assertion failed: got |" + actual + "|" +
                ", expected |" + expected + "|" +
                (message ? " (" + message + ")" : ""));
}

var hold = [];

try {
    for (;;) hold.push('x'.repeat(1000) + hold.length);
} catch (e) {
    // Release the heap before using it: the strings above still hold the
    // limit, and the check below allocates.
    hold = null;
    assert(/out of memory/.test(String(e)), true, String(e));
}

console.log("ok test_oom_backtrace");

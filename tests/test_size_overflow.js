"use strict";

function expect_exception(type, fn)
{
    let caught = false;
    try {
        fn();
    } catch (e) {
        if (!(e instanceof type))
            throw e;
        caught = true;
    }
    if (!caught)
        throw new Error("expected exception");
}

/* Enumeration still returns all keys when the table grows. */
const keys = [];
for (let i = 0; i < 12; i++)
    keys.push("k" + i);
const growing_keys = new Proxy({}, { ownKeys() { return keys; } });
if (Reflect.ownKeys(growing_keys).join(",") !== keys.join(","))
    throw new Error("missing proxy keys");

/* A fake length must not allocate a huge key table before reading index 0. */
const missing_key = new Proxy({}, {
    ownKeys() { return { length: 0x20000000 }; },
});
expect_exception(TypeError, () => Reflect.ownKeys(missing_key));

/* Cleanup must only release the keys that were actually initialized. */
const stop = new Error("stop");
const throwing_key = new Proxy({}, {
    ownKeys() {
        return {
            length: 0x20000000,
            0: "a",
            get 1() { throw stop; },
        };
    },
});
try {
    Reflect.ownKeys(throwing_key);
    throw new Error("expected exception");
} catch (e) {
    if (e !== stop)
        throw e;
}

/* Array methods must reject a count whose element size would wrap. */
expect_exception(InternalError, () =>
    Array.prototype.toReversed.call({ length: 0x20000000 }));

import {assert} from "./assert.js"

const rab = new ArrayBuffer(10, { maxByteLength: 10 });
const src = new Uint8Array(rab, 0);

function f() {
    return 1337;
}

const EvilConstructor = new Proxy(function(){}, {
    get: function(target, prop, receiver) {
        if (prop === 'prototype') {
            rab.resize(0);
            return Uint8Array.prototype;
        }
        return Reflect.get(target, prop, receiver);
    }
});

try {
  let u8 = Reflect.construct(Uint8Array, [src], EvilConstructor);
  print(u8);
  throw Error("Should not get here");
} catch (e) {
  assert(e instanceof RangeError);
}

import {assert} from "./assert.js"

const ab = new ArrayBuffer(10, { maxByteLength: 10 });
function f() {
    return 1337;
}
const evil = f.bind();

Object.defineProperty(evil, "prototype", { get: () => {
    return ab.resize();
} });
let u8 = Reflect.construct(Uint8Array, [ab], evil);
assert(u8.length == 0);

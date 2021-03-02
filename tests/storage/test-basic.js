import * as storage from "storage";
import * as std from "std";
import * as os from "os";

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

const path = __DIR__ + "test.db";
os.remove(path);

function init() {
  let db = storage.open(path);
  db.root = { foo:"foofoo", bar:42, arr: [1,2,3] };
  db.close();
}

function test() {
  let db = storage.open(path);
  let r = db.root;
  let arr = db.root.arr;
  print(JSON.stringify(r));
  print(JSON.stringify(arr));
  assert(r.foo, "foofoo");
  db.close();
}

init();
test();




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

  let index = db.createIndex("string");
  db.root = { 
    index: index
  };
  index.set("a", { key: "a" });
  index.set("b", { key: "b" });
  index.set("c", { key: "c" });

  assert(index.length, 3);
  db.close();
}

function test() {
  let db = storage.open(path);

  let index = db.root.index;
  assert(index.length, 3);
  for( let item of index )
    print(JSON.stringify(item));
  print("---- select() ----");
  for( let item of index.select("b","c") )
    print(JSON.stringify(item));

  db.close();
}

init();
test();




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

export class Account 
{
  constructor() {
    this.balance = 0n;
    this.name = "Foo";
  }
  show() {
    print("account:", this.name);
    print("balance:", this.balance);
  }
}

const path = __DIR__ + "test.db";
os.remove(path);

function init() {

  var inst = new Account();
  inst.show();

  let db = storage.open(path);
  db.root = { 
    inst: inst
  };
  db.close();
}

function test() {
  let db = storage.open(path);
  let r = db.root;
  r.inst.show();
  db.close();
  print("PASSED");
}

init();
test();




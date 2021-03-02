

import * as Storage from "storage";
import * as std from "std";
import * as os from "os";

function assert(actual, expected, message) {
    if (arguments.length == 1)
        expected = true;

    if (actual === expected)
        return;

    if (actual !== null && expected !== null &&  typeof actual == 'object' && typeof expected == 'object' 
        &&  actual.toString() === expected.toString()) return;

    throw Error("assertion failed: got |" + actual + "|" +
                ", expected |" + expected + "|" +
                (message ? " (" + message + ")" : ""));
}

function isObject(object) {
  return object != null && typeof object === 'object';
}

function deepEqual(object1, object2) {
  const keys1 = Object.keys(object1);
  const keys2 = Object.keys(object2);

  if (keys1.length !== keys2.length) 
    return false;

  for (const key of keys1) {
    const val1 = object1[key];
    const val2 = object2[key];
    const areObjects = isObject(val1) && isObject(val2);
    if (areObjects && !deepEqual(val1, val2) 
    || !areObjects && val1 !== val2) 
      return false;
  }

  return true;
}

const path = __DIR__ + "test.db";
os.remove(path);

function init() 
{
  let storage = Storage.open(path);
  
  assert(storage.root, null, "fresh storage has null root");

  let index_int = storage.createIndex("integer"); index_int.set(1,{a:1}); index_int.set(2,{b:2}); index_int.set(3,{c:3});
  let index_date = storage.createIndex("date");   
  let index_long = storage.createIndex("long");   
  let index_float = storage.createIndex("float"); 
  let index_string = storage.createIndex("string"); index_string.set("a",{a:1}); index_string.set("b",{b:2}); index_string.set("c",{c:3});

  storage.root = { 
    //basic types 
    types: 
    {
      tbool: true,
      tinteger: 42,
      tlong: 420n,
      tfloat: 3.1415926,
      tstring: "forty two",
      tarray: [1,2,3],
      tobject: { a:1, b:2, c:3},
      tdate: new Date(2021, 2, 28)
    },
    //indexes 
    indexes: {
      iint: index_int,
      idate: index_date,
      ilong : index_long,
      ifloat: index_float,
      istring : index_string
    }
  };
  storage.close();
}

function test() 
{
  let storage = Storage.open(path);

  assert(!!storage);

  let dbTypes = storage.root.types;

  const types = {
    tbool: true,
    tinteger: 42,
    tlong: 420n,
    tfloat: 3.1415926,
    tstring: "forty two",
    tarray: [1,2,3],
    tobject: { a:1, b:2, c:3},
    tdate: new Date(2021, 2, 28)
  };

  assert(deepEqual(dbTypes,types));

  let indexes = storage.root.indexes;

  assert(indexes.iint.length,3);

  assert(deepEqual(indexes.iint.get(1),{a:1}));
  assert(deepEqual(indexes.iint.get(2),{b:2}));
  assert(deepEqual(indexes.iint.get(3),{c:3}));

  assert(indexes.istring.length,3);

  assert(deepEqual(indexes.istring.get("a"),{a:1}));
  assert(deepEqual(indexes.istring.get("b"),{b:2}));
  assert(deepEqual(indexes.istring.get("c"),{c:3}));

  var r = [];
  for(let obj of indexes.istring)
    r.push(obj);

  assert(deepEqual(r,[{a:1},{b:2},{c:3}]));

  r = [];
  for(let obj of indexes.istring.select("b","c")) // range inclusive
    r.push(obj);
  assert(deepEqual(r,[{b:2},{c:3}]));  

  r = [];
  for(let obj of indexes.istring.select("b",null)) // range, open end 
    r.push(obj);
  assert(deepEqual(r,[{b:2},{c:3}]));  

  r = [];
  for(let obj of indexes.istring.select(null,"b")) // range, open start 
    r.push(obj);
  assert(deepEqual(r,[{a:1},{b:2}]));  

  storage.close();

  print("PASSED");
}

init();
test();
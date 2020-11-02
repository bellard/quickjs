

// "driver" of JSX expressions
JSX = function(tag,atts,kids) {
  return [tag,atts,kids]; // just produce "vnode" tuple
}

function isObject(object) {
  return object != null && typeof object === 'object';
}

function deepEqual(object1, object2) {
  const keys1 = Object.keys(object1);
  const keys2 = Object.keys(object2);

  if (keys1.length !== keys2.length) {
    return false;
  }

  for (const key of keys1) {
    const val1 = object1[key];
    const val2 = object2[key];
    const areObjects = isObject(val1) && isObject(val2);
    if (
      areObjects && !deepEqual(val1, val2) ||
      !areObjects && val1 !== val2
    ) {
      return false;
    }
  }

  return true;
}

function assert(v1,v2) {
  if(!deepEqual(v1,v2)) {
    //console.log(JSON.stringify(v1,v2));
    throw "problem in JSX construct"
  }
}


var t1  = <div>test</div>; 
var t1a = ["div",{},["test"]];
assert(t1,t1a);

var t2  = <h1 id="foo">test</h1>; 
var t2a = ["h1",{id:"foo"},["test"]];

assert(t2,t2a);

var t3  = <div><h1/></div>; 
var t3a = ["div",{},[["h1",{},[]]]];

assert(t3,t3a);


var t4  = <div id="foo" class="bar"><h1>header</h1><button>clicks</button></div>; 
var t4a = ["div",{id:"foo",class:"bar"},[["h1",{},["header"]],["button",{},["clicks"]]]];

assert(t4,t4a);

console.log("JSX test passed!");

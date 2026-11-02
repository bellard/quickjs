/* example of JS and JSON modules */

import { fib } from "./fib_module.js";
import msg from "./message.json";
import msg2 from "./message.json" with { type: "text" };

console.log("Hello World");
console.log("fib(10)=", fib(10));
console.log("msg=", msg);
console.log("msg2=", msg2);

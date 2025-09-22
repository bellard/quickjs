/* example of JS and JSON modules */

import { fib } from "./fib_module.js";
import msg from "./message.json";

console.log("Hello World");
console.log("fib(10)=", fib(10));
console.log("msg=", msg);

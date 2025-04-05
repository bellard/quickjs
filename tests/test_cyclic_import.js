/*---
negative:
  phase: resolution
  type: SyntaxError
---*/
// FIXME(bnoordhuis) shouldn't throw SyntaxError but that's still better
// than segfaulting, see https://github.com/quickjs-ng/quickjs/issues/567
import {assert} from "./assert.js"
import {f} from "./fixture_cyclic_import.js"
export {f}
export function g(x) { return x + 1 }
assert(f(1), 4)

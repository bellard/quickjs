/* Regression test: a Worker kept alive only by a self-cycle can be freed by
   the cycle GC while its pending message is being handled */
import * as std from "std";
import * as os from "os";

if (os.Worker.parent) {
    var largeData = [];
    for (var i = 0; i < 20000; i++) {
        largeData.push("data_item_" + i);
    }
    os.Worker.parent.postMessage(largeData);
} else {
    /* push malloc_size close to the GC threshold so that the allocations
       done while receiving the message trigger the cycle GC */
    var heap_filler = [];
    for (var i = 0; i < 3000; i++) {
        heap_filler.push({a: i, b: i * 2, c: "x".repeat(10)});
    }

    var w = new os.Worker("./test_worker_gc.js");
    w.self_ref = w;
    w.onmessage = function (e) {
        if (e.data.length !== 20000)
            std.exit(1);
        std.exit(0);
    };
    /* the Worker is now only reachable through its self reference */
    w = null;
}

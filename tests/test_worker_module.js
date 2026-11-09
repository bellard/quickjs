/* Worker code for test_worker.js */
import * as std from "std";
import * as os from "os";

var parent = os.Worker.parent;

function handle_msg(e) {
    var ev = e.data;
    //          print("child_recv", JSON.stringify(ev));
    switch(ev.type) {
    case "sab":
        /* modify the SharedArrayBuffer */
        ev.buf[2] = 10;
        parent.postMessage({ type: "sab_done", buf: ev.buf });
        break;
    case "wait_async":
        Atomics.waitAsync(ev.buf, 0, 0, Infinity).value.then( () => { parent.postMessage({ type: "done" }); parent.onmessage = null; /* terminate the worker */ });
        parent.postMessage({ type: "notify", buf: ev.buf });
        break;
    }
}

function worker_main() {
    var i;

    parent.onmessage = handle_msg;
    for(i = 0; i < 10; i++) {
        parent.postMessage({ type: "num", num: i });
    }
}

worker_main();

/* os.Worker API test */
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

var worker;

function test_worker()
{
    var counter;

    worker = new os.Worker("./test_worker_module.js");

    counter = 0;
    worker.onmessage = function (e) {
        var ev = e.data;
//        print("recv", JSON.stringify(ev));
        switch(ev.type) {
        case "num":
            assert(ev.num, counter);
            counter++;
            if (counter == 10) {
                /* test SharedArrayBuffer modification */
                let sab = new SharedArrayBuffer(10);
                let buf = new Uint8Array(sab);
                worker.postMessage({ type: "sab", buf: buf });
            }
            break;
        case "sab_done":
            {
                /* check that the SharedArrayBuffer was modified */
                assert(ev.buf[2], 10);
                /* start the waitAsync() test */
                let sab = new SharedArrayBuffer(4);
                let buf = new Int32Array(sab);
                worker.postMessage({ type: "wait_async", buf: buf });
            }
            break;
        case "notify":
            {
                let buf = ev.buf;
                Atomics.store(buf, 0, 1);
                /* notify the waitAsync() */
                Atomics.notify(buf, 0, 1);
            }
            break;
        case "done":
            /* terminate */
            worker.onmessage = null;
            break;
        }
    };
}


test_worker();

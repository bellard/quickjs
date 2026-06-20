import * as std from "std";
import * as os from "os";

function assert(actual, expected, message) {
    if (arguments.length == 1)
        expected = true;

    if (Object.is(actual, expected))
        return;

    if (actual !== null && expected !== null
    &&  typeof actual == 'object' && typeof expected == 'object'
    &&  actual.toString() === expected.toString())
        return;

    throw Error("assertion failed: got |" + actual + "|" +
                ", expected |" + expected + "|" +
                (message ? " (" + message + ")" : ""));
}

function handle_read(fd_r, fd_w, i)
{
    var buf = new Uint32Array(1);
    var val, len;
    len = os.read(fd_r, buf.buffer, 0, 4);
    os.setReadHandler(fd_r, null);
    val = buf[0];
//    print("read fd=", fd_r, "val=", val, "len=", len);
    assert(val, i);
}

function handle_write(fd_r, fd_w, i)
{
    var buf = new Uint32Array(1);
    buf[0] = i;
    os.write(fd_w, buf.buffer, 0, 4);
    os.setWriteHandler(fd_w, null);
}

function test_rw_handlers(n)
{
    var tab, fd_r, fd_w, i;
    tab = [];
    for(i = 0; i < n; i++) {
        tab[i] = os.pipe();
        fd_r = tab[i][0];
        fd_w = tab[i][1];
        os.setReadHandler(fd_r, handle_read.bind(null, fd_r, fd_w, i));
    }
    for(i = n - 1; i >= 0; i--) {
        fd_r = tab[i][0];
        fd_w = tab[i][1];
        os.setWriteHandler(fd_w, handle_write.bind(null, fd_r, fd_w, i));
    }
}

test_rw_handlers(100);

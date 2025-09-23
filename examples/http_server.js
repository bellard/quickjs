#!/usr/bin/env qjs
///@ts-check
/// <reference path="../doc/globals.d.ts" />
/// <reference path="../doc/os.d.ts" />
/// <reference path="../doc/std.d.ts" />
import * as os from "os";
import * as std from "std";// for std.strerror

const MIMES = new Map([
    ['html', 'text/html'],
    ['txt', 'text/plain'],
    ['css', 'text/css'],
    ['c', 'text/plain'],
    ['h', 'text/plain'],
    ['json', 'application/json'],
    ['mjs', 'application/javascript'],
    ['js', 'application/javascript'],
    ['', 'application/octet-stream'],
]);
/** @template T @param {os.Result<T>} result */
function must(result) {
    if (typeof result === "number" && result < 0) throw new Error(std.strerror(-result));
    return /** @type {T} */ (result)
}
/**@param {os.FileDescriptor} fd */
async function* recvLines(fd) {
    const chunk = new Uint8Array(1);
    let line = '';
    while (await os.recv(fd, chunk.buffer) > 0) {
        const char = String.fromCharCode(...chunk);
        if (char == '\n') {
            yield line;
            line = '';
        } else line += char;
    }
    if (line) yield line;
}
/** @param {os.FileDescriptor} fd @param {string[]} lines */
function sendLines(fd, lines) {
    const buf = Uint8Array.from(lines.join('\r\n'), c => c.charCodeAt(0));
    return os.send(fd, buf.buffer);
}
//USAGE: qjs http_server.js [PORT=8080 [HOST=localhost]]
const [port = "8080", host = "localhost"] = scriptArgs.slice(1);
const [ai] = must(os.getaddrinfo(host, { service: port }));
//if (!ai.length) throw `Unable to getaddrinfo(${host}, ${port})`;
const sock_srv = must(os.socket(os.AF_INET, os.SOCK_STREAM));
must(os.setsockopt(sock_srv, os.SO_REUSEADDR, new Uint32Array([1]).buffer));
must(os.bind(sock_srv, ai));
must(os.listen(sock_srv));
//os.signal(os.SIGINT, ()=>os.close(sock_srv)); // don't work
console.log(`Listening on http://${host}:${port} (${ai.addr}:${ai.port}) ...`);
const openCmd = { linux: "xdg-open", darwin: "open", win32: "start" }[os.platform];
if (openCmd && os.exec) os.exec([openCmd, `http://${host}:${port}`]);
while (true) { // TODO: break on SIG*
    const [sock_cli] = await os.accept(sock_srv);

    const lines = recvLines(sock_cli);
    const [method, path, http_ver] = ((await lines.next()).value || '').split(' ');
    let safe_path = '.' + path.replaceAll(/\.+/g, '.'); // may += index.html later
    console.log(method, safe_path, http_ver);

    const headers = new Map()
    for await (const line of lines) {
        const header = line.trimEnd();
        if (!header) break;
        const sepIdx = header.indexOf(': ');
        headers.set(header.slice(0, sepIdx), header.slice(sepIdx + 2));
    }

    let [obj, err] = os.stat(safe_path);
    if (obj?.mode & os.S_IFDIR && safe_path.endsWith('/') && os.stat(safe_path + 'index.html')[0]) {
        safe_path += 'index.html';
        [obj, err] = os.stat(safe_path);
    }
    if (err) {
        await sendLines(sock_cli, ['HTTP/1.1 404', '', safe_path, 'errno:' + err])
    } else if (obj?.mode & os.S_IFDIR) {
        if (!safe_path.endsWith('/'))
            await sendLines(sock_cli, ['HTTP/1.1 301', `Location: ${safe_path}/`, '']);
        else
            await sendLines(sock_cli, ['HTTP/1.1 200', 'Content-Type: text/html', '',
                os.readdir(safe_path)[0]?.filter(e => e[0] != '.').map(e => `<li><a href="${e}">${e}</a></li>`).join('')
            ]);
    } else {
        const mime = MIMES.get(safe_path.split('.').at(-1) || '') || MIMES.get('');
        await sendLines(sock_cli, ['HTTP/1.1 200', `Content-Type: ${mime}`, '', '']);
        const fd = must(os.open(safe_path));
        const fbuf = new Uint8Array(4096);
        for (let got = 0; (got = os.read(fd, fbuf.buffer, 0, fbuf.byteLength)) > 0;) {
            await os.send(sock_cli, fbuf.buffer, got);
        }
    }

    os.close(sock_cli);
}

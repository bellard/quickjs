#!/usr/bin/env qjs
///@ts-check
/// <reference path="../doc/globals.d.ts" />
/// <reference path="../doc/os.d.ts" />
import * as os from "os";

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
/** @template T @param {os.Result<T>} result @returns {T} */
function must(result) {
    if (typeof result === "number" && result < 0) throw result;
    return /** @type {T} */ (result)
}
/**@param {os.FileDescriptor} fd */
function* recvLines(fd) {
    const chunk = new Uint8Array(1);
    let line = '';
    while (os.recv(fd, chunk.buffer) > 0) {
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
    os.send(fd, buf.buffer);
}
//USAGE: qjs http_server.js [PORT=8080 [HOST=localhost]]
const [port = "8080", host = "localhost"] = scriptArgs.slice(1);
const ai = os.getaddrinfo(host, port);
if (!ai.length) throw `Unable to getaddrinfo(${host}, ${port})`;

const sock_srv = must(os.socket(os.AF_INET, os.SOCK_STREAM));
must(os.setsockopt(sock_srv, os.SO_REUSEADDR, new Uint32Array([1]).buffer));
must(os.bind(sock_srv, ai[0]));
must(os.listen(sock_srv));

console.log(`Listening on http://${ai[0].addr}:${ai[0].port} ...`);

while (true) { // TODO: break on SIG*
    const [sock_cli] = os.accept(sock_srv);

    const lines = recvLines(sock_cli);
    const [method, path, http_ver] = (lines.next().value || '').split(' ');
    let safe_path = '.' + path.replaceAll(/\.+/g, '.'); // may += index.html later
    console.log(method, safe_path, http_ver);

    const headers = new Map()
    for (const line of lines) {
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
        sendLines(sock_cli, ['HTTP/1.1 404', '', safe_path, 'errno:' + err])
    } else if (obj?.mode & os.S_IFDIR) {
        if (!safe_path.endsWith('/'))
            sendLines(sock_cli, ['HTTP/1.1 301', `Location: ${safe_path}/`, '']);
        else
            sendLines(sock_cli, ['HTTP/1.1 200', 'Content-Type: text/html', '',
                os.readdir(safe_path)[0]?.filter(e => e[0] != '.').map(e => `<li><a href="${e}">${e}</a></li>`).join('')
            ]);
    } else {
        const mime = MIMES.get(safe_path.split('.').at(-1) || '') || MIMES.get('');
        sendLines(sock_cli, ['HTTP/1.1 200', `Content-Type: ${mime}`, '', '']);
        const fd = must(os.open(safe_path));
        const fbuf = new Uint8Array(4096);
        for (let got = 0; (got = os.read(fd, fbuf.buffer, 0, fbuf.byteLength)) > 0;) {
            os.send(sock_cli, fbuf.buffer, got);
        }
    }

    os.close(sock_cli);
}

os.close(sock_srv);

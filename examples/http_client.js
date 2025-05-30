#!/usr/bin/env qjs
///@ts-check
/// <reference path="../doc/globals.d.ts" />
/// <reference path="../doc/os.d.ts" />
import * as os from "os";
/** @template T @param {os.Result<T>} result @returns {T} */
function must(result) {
    if (typeof result === "number" && result < 0) throw result;
    return /** @type {T} */ (result)
}
/** @param {os.FileDescriptor} fd @param {string[]} lines */
function sendLines(fd, lines) {
    const buf = Uint8Array.from(lines.join('\r\n'), c => c.charCodeAt(0));
    os.send(fd, buf.buffer);
}
const [host = "example.com", port = "80"] = scriptArgs.slice(1);
const ai = os.getaddrinfo(host, port).filter(ai => ai.family == os.AF_INET && ai.port); // TODO too much/invalid result
if (!ai.length) throw `Unable to getaddrinfo(${host}, ${port})`;
const sockfd = must(os.socket(os.AF_INET, os.SOCK_STREAM));
must(os.connect(sockfd, ai[0]))
sendLines(sockfd, ["GET / HTTP/1.0", `Host: ${host}`, "Connection: close", "", ""]);

const chunk = new Uint8Array(4096);
while (os.recv(sockfd, chunk.buffer) > 0) {
    console.log(String.fromCharCode(...chunk));
}
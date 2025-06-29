#!/usr/bin/env qjs
///@ts-check
/// <reference path="../doc/globals.d.ts" />
/// <reference path="../doc/os.d.ts" />
/// <reference path="../doc/std.d.ts" />
import * as os from "os";
import * as std from "std";

/** @template T @param {os.Result<T>} result */
function must(result) {
    if (typeof result === "number" && result < 0) throw new Error(std.strerror(-result));
    return /** @type {T} */ (result)
}
//USAGE: client.js wttr.in/paris
const uriRegexp = /^(?<host>[A-Za-z0-9\-\.]+)(?<port>:[0-9]+)?(?<query>.*)$/;
const { host = "bellard.org", port = ":80", query = "/" } = scriptArgs[1]?.match(uriRegexp)?.groups || {};
console.log("sending GET on",{ host, port, query })
const [addr] = must(os.getaddrinfo(host, { service: port.slice(1) }));
const sockfd = must(os.socket(addr.family, addr.socktype));
await os.connect(sockfd, addr);
const httpReq = Uint8Array.from(`GET ${query||'/'} HTTP/1.0\r\nHost: ${host}\r\nUser-Agent: curl\r\n\r\n`, c => c.charCodeAt(0))
must(await os.send(sockfd, httpReq.buffer) > 0);
const chunk = new Uint8Array(512);
const recvd = await os.recv(sockfd, chunk.buffer);
console.log([...chunk.slice(0, recvd)].map(c => String.fromCharCode(c)).join(''));

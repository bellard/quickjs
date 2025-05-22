#!/usr/bin/env qjs
///@ts-check
/// <reference path="../doc/globals.d.ts" />
/// <reference path="../doc/os.d.ts" />
import * as os from "os";
/** @template T @param {os.Result<T>} result */
function must(result) {
    if (typeof result === "number" && result < 0) throw result;
    return /** @type {T} */ (result)
}

const sockfd = must(os.socket(os.AF_INET, os.SOCK_STREAM));
await os.connect(sockfd, os.getaddrinfo("bellard.org",'80')[0]);
const httpReq = ["GET / HTTP/1.0", "", ""].join('\r\n')
must(await os.send(sockfd, Uint8Array.from(httpReq, c => c.charCodeAt(0)).buffer) > 0);
const chunk = new Uint8Array(512);
const recvd = await os.recv(sockfd, chunk.buffer);
console.log([...chunk.slice(0,recvd)].map(c => String.fromCharCode(c)).join(''));

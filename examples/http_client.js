#!/usr/bin/env qjs
///@ts-check
/// <reference path="../doc/globals.d.ts" />
/// <reference path="../doc/os.d.ts" />
import * as os from "os";
const spam = ()=>console.log("spam", os.setTimeout(spam, 1000))
/** @param {os.FileDescriptor} fd @param {string} str */
const send = (fd, str) => os.send(fd, Uint8Array.from(str, c => c.charCodeAt(0)).buffer);
/** @template T @param {os.Result<T>} result @returns {T} */
function must(result) {
    if (typeof result === "number" && result < 0) throw result;
    return /** @type {T} */ (result)
}

const [host = "example.com", port = "80"] = scriptArgs.slice(1);
const ai = os.getaddrinfo(host, port).filter(ai => ai.family == os.AF_INET && ai.port); // TODO too much/invalid result
if (!ai.length) throw `Unable to getaddrinfo(${host}, ${port})`;
const sockfd = must(os.socket(os.AF_INET, os.SOCK_STREAM));
//spam();
const c = await os.connect(sockfd, { addr: "51.15.168.198", port: 80});
console.log({ c });
//const s = await send(sockfd, ["GET / HTTP/1.0", `Host: ${host}`, "Connection: close", "", ""].join('\r\n'));
//console.log({ s });

//const chunk = new Uint8Array(4096);
//const r = await os.recv(sockfd, chunk.buffer);
//console.log({ r });

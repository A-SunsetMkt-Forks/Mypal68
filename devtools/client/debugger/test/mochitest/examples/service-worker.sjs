/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const body = `
dump("Starting\\n");

self.addEventListener("activate", event => {
  dump("Activate\\n");
  event.waitUntil(self.clients.claim());
});

self.addEventListener("fetch", event => {
  const url = event.request.url;
  dump("Fetch: " + url + "\\n");
  if (url.includes("whatever")) {
    const response = new Response("Service worker response STATUS", { statusText: "OK" });
    event.respondWith(response);
  }
});
`;

function handleRequest(request, response) {
  response.setHeader("Content-Type", "text/javascript");

  const arr = /setStatus=(.*)/.exec(request.queryString);
  if (arr) {
    setState("status", arr[1]);
  }

  const status = getState("status");

  let newBody = body.replace("STATUS", status);

  if (status == "stuckInstall") {
newBody += `
self.addEventListener("install", event => {
  dump("Install\\n");
  event.waitUntil(new Promise(resolve => {}));
});
`;
  }

  response.write(newBody);
}

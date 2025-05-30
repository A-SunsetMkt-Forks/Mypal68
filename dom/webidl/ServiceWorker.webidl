/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * http://slightlyoff.github.io/ServiceWorker/spec/service_worker/index.html#service-worker-obj
 *
 */

// Still unclear what should be subclassed.
// https://github.com/slightlyoff/ServiceWorker/issues/189
[Func="ServiceWorkerVisible",
 // FIXME(nsm): Bug 1113522. This is exposed to satisfy webidl constraints, but it won't actually work.
 Exposed=(Window,Worker)]
interface ServiceWorker : EventTarget {
  readonly attribute USVString scriptURL;
  readonly attribute ServiceWorkerState state;

  attribute EventHandler onstatechange;

  [Throws]
  void postMessage(any message, sequence<object> transferable);
  [Throws]
  void postMessage(any message, optional StructuredSerializeOptions options = {});
};

ServiceWorker includes AbstractWorker;

enum ServiceWorkerState {
  // https://github.com/w3c/ServiceWorker/issues/1162
  "parsed",

  "installing",
  "installed",
  "activating",
  "activated",
  "redundant"
};

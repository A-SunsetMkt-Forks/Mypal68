/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

class ConsoleCommands {
  constructor({ devToolsClient, proxy, currentTarget }) {
    this.devToolsClient = devToolsClient;
    this.proxy = proxy;
    this.currentTarget = currentTarget;
  }

  evaluateJSAsync(expression, options) {
    return this.proxy.webConsoleFront.evaluateJSAsync(expression, options);
  }

}

module.exports = ConsoleCommands;

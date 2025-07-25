/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

var Services = require("Services");
var WebConsole = require("devtools/client/webconsole/webconsole");

loader.lazyRequireGetter(
  this,
  "BrowserConsoleManager",
  "devtools/client/webconsole/browser-console-manager",
  true
);

/**
 * A BrowserConsole instance is an interactive console initialized *per target*
 * that displays console log data as well as provides an interactive terminal to
 * manipulate the target's document content.
 *
 * This object only wraps the iframe that holds the Browser Console UI. This is
 * meant to be an integration point between the Firefox UI and the Browser Console
 * UI and features.
 *
 * This object extends the WebConsole object located in webconsole.js
 */
class BrowserConsole extends WebConsole {
  /*
   * @constructor
   * @param object target
   *        The target that the browser console will connect to.
   * @param nsIDOMWindow iframeWindow
   *        The window where the browser console UI is already loaded.
   * @param nsIDOMWindow chromeWindow
   *        The window of the browser console owner.
   */
  constructor(target, iframeWindow, chromeWindow) {
    super(null, iframeWindow, chromeWindow, true);

    this._browserConsoleTarget = target;
    this._bcInitializer = null;
    this._bcDestroyer = null;
  }

  get currentTarget() {
    return this._browserConsoleTarget;
  }

  /**
   * Initialize the Browser Console instance.
   *
   * @return object
   *         A promise for the initialization.
   */
  init() {
    if (this._bcInitializer) {
      return this._bcInitializer;
    }

    // Only add the shutdown observer if we've opened a Browser Console window.
    ShutdownObserver.init();

    this._bcInitializer = super.init();
    return this._bcInitializer;
  }

  /**
   * Destroy the object.
   *
   * @return object
   *         A promise object that is resolved once the Browser Console is closed.
   */
  destroy() {
    if (this._bcDestroyer) {
      return this._bcDestroyer;
    }

    this._bcDestroyer = (async () => {
      await super.destroy();
      await this.currentTarget.destroy();
      this.chromeWindow.close();
    })();

    return this._bcDestroyer;
  }
}

/**
 * The ShutdownObserver listens for app shutdown and saves the current state
 * of the Browser Console for session restore.
 */
var ShutdownObserver = {
  _initialized: false,

  init() {
    if (this._initialized) {
      return;
    }

    Services.obs.addObserver(this, "quit-application-granted");

    this._initialized = true;
  },

  observe(message, topic) {
    if (topic == "quit-application-granted") {
      BrowserConsoleManager.storeBrowserConsoleSessionState();
      this.uninit();
    }
  },

  uninit() {
    Services.obs.removeObserver(this, "quit-application-granted");
  },
};

module.exports = BrowserConsole;

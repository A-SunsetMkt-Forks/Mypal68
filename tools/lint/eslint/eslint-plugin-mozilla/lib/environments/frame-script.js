/**
 * @fileoverview Defines the environment for frame scripts.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

"use strict";

module.exports = {
  globals: {
    addMessageListener: false,
    addWeakMessageListener: false,
    atob: false,
    btoa: false,
    chromeOuterWindowID: false,
    content: false,
    docShell: false,
    processMessageManager: false,
    removeMessageListener: false,
    removeWeakMessageListener: false,
    sendAsyncMessage: false,
    sendSyncMessage: false,
    tabEventTarget: false,
    RPMGetBoolPref: false,
    RPMSetBoolPref: false,
    RPMGetFormatURLPref: false,
    RPMIsWindowPrivate: false,
    RPMSendAsyncMessage: false,
    RPMAddMessageListener: false,
    RPMRemoveMessageListener: false,
  },
};

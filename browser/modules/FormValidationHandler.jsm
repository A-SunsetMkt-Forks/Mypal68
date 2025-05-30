/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Chrome side handling of form validation popup.
 */

"use strict";

var EXPORTED_SYMBOLS = ["FormValidationHandler"];

var FormValidationHandler = {
  _panel: null,
  _anchor: null,

  /*
   * Public apis
   */

  uninit() {
    this._panel = null;
    this._anchor = null;
  },

  hidePopup() {
    this._hidePopup();
  },

  /*
   * Events
   */

  // Listeners are added in BrowserGlue.jsm
  receiveMessage(aMessage) {
    let window = aMessage.target.ownerGlobal;
    let json = aMessage.json;
    let tabBrowser = window.gBrowser;
    switch (aMessage.name) {
      case "FormValidation:ShowPopup":
        // target is the <browser>, make sure we're receiving a message
        // from the foreground tab.
        if (tabBrowser && aMessage.target != tabBrowser.selectedBrowser) {
          return;
        }
        this._showPopup(window, json);
        break;
      case "FormValidation:HidePopup":
        this._hidePopup();
        break;
    }
  },

  observe(aSubject, aTopic, aData) {
    this._hidePopup();
  },

  handleEvent(aEvent) {
    switch (aEvent.type) {
      case "FullZoomChange":
      case "TextZoomChange":
      case "ZoomChangeUsingMouseWheel":
      case "scroll":
        this._hidePopup();
        break;
      case "popuphiding":
        this._onPopupHiding(aEvent);
        break;
    }
  },

  /*
   * Internal
   */

  _onPopupHiding(aEvent) {
    aEvent.originalTarget.removeEventListener("popuphiding", this, true);
    let tabBrowser = aEvent.originalTarget.ownerGlobal.gBrowser;
    tabBrowser.selectedBrowser.removeEventListener("scroll", this, true);
    tabBrowser.selectedBrowser.removeEventListener("FullZoomChange", this);
    tabBrowser.selectedBrowser.removeEventListener("TextZoomChange", this);
    tabBrowser.selectedBrowser.removeEventListener(
      "ZoomChangeUsingMouseWheel",
      this
    );

    this._panel.hidden = true;
    this._panel = null;
    this._anchor.hidden = true;
    this._anchor = null;
  },

  /*
   * Shows the form validation popup at a specified position or updates the
   * messaging and position if the popup is already displayed.
   *
   * @aWindow - the chrome window
   * @aPanelData - Object that contains popup information
   *  aPanelData stucture detail:
   *   contentRect - the bounding client rect of the target element. If
   *    content is remote, this is relative to the browser, otherwise its
   *    relative to the window.
   *   position - popup positional string constants.
   *   message - the form element validation message text.
   */
  _showPopup(aWindow, aPanelData) {
    let previouslyShown = !!this._panel;
    this._panel = aWindow.document.getElementById("invalid-form-popup");
    this._panel.firstChild.textContent = aPanelData.message;
    this._panel.hidden = false;

    let tabBrowser = aWindow.gBrowser;
    this._anchor = tabBrowser.selectedBrowser.popupAnchor;
    this._anchor.style.left = aPanelData.contentRect.left + "px";
    this._anchor.style.top = aPanelData.contentRect.top + "px";
    this._anchor.style.width = aPanelData.contentRect.width + "px";
    this._anchor.style.height = aPanelData.contentRect.height + "px";
    this._anchor.hidden = false;

    // Display the panel if it isn't already visible.
    if (!previouslyShown) {
      // Cleanup after the popup is hidden
      this._panel.addEventListener("popuphiding", this, true);

      // Hide if the user scrolls the page
      tabBrowser.selectedBrowser.addEventListener("scroll", this, true);
      tabBrowser.selectedBrowser.addEventListener("FullZoomChange", this);
      tabBrowser.selectedBrowser.addEventListener("TextZoomChange", this);
      tabBrowser.selectedBrowser.addEventListener(
        "ZoomChangeUsingMouseWheel",
        this
      );

      // Open the popup
      this._panel.openPopup(this._anchor, aPanelData.position, 0, 0, false);
    }
  },

  /*
   * Hide the popup if currently displayed. Will fire an event to onPopupHiding
   * above if visible.
   */
  _hidePopup() {
    if (this._panel) {
      this._panel.hidePopup();
    }
  },
};

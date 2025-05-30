/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This file is loaded into the browser window scope.
/* eslint-env mozilla/browser-window */

var PointerlockFsWarning = {
  _element: null,
  _origin: null,

  /**
   * Timeout object for managing timeout request. If it is started when
   * the previous call hasn't finished, it would automatically cancelled
   * the previous one.
   */
  Timeout: class {
    constructor(func, delay) {
      this._id = 0;
      this._func = func;
      this._delay = delay;
    }
    start() {
      this.cancel();
      this._id = setTimeout(() => this._handle(), this._delay);
    }
    cancel() {
      if (this._id) {
        clearTimeout(this._id);
        this._id = 0;
      }
    }
    _handle() {
      this._id = 0;
      this._func();
    }
    get delay() {
      return this._delay;
    }
  },

  showPointerLock(aOrigin) {
    if (!document.fullscreen) {
      let timeout = Services.prefs.getIntPref(
        "pointer-lock-api.warning.timeout"
      );
      this.show(aOrigin, "pointerlock-warning", timeout, 0);
    }
  },

  showFullScreen(aOrigin) {
    let timeout = Services.prefs.getIntPref("full-screen-api.warning.timeout");
    let delay = Services.prefs.getIntPref("full-screen-api.warning.delay");
    this.show(aOrigin, "fullscreen-warning", timeout, delay);
  },

  // Shows a warning that the site has entered fullscreen or
  // pointer lock for a short duration.
  show(aOrigin, elementId, timeout, delay) {
    if (!this._element) {
      this._element = document.getElementById(elementId);
      // Setup event listeners
      this._element.addEventListener("transitionend", this);
      window.addEventListener("mousemove", this, true);
      // The timeout to hide the warning box after a while.
      this._timeoutHide = new this.Timeout(() => {
        this._state = "hidden";
      }, timeout);
      // The timeout to show the warning box when the pointer is at the top
      this._timeoutShow = new this.Timeout(() => {
        this._state = "ontop";
        this._timeoutHide.start();
      }, delay);
    }

    // Set the strings on the warning UI.
    if (aOrigin) {
      this._origin = aOrigin;
    }
    let uri = Services.io.newURI(this._origin);
    let host = null;
    try {
      host = uri.host;
    } catch (e) {}
    let textElem = this._element.querySelector(
      ".pointerlockfswarning-domain-text"
    );
    if (!host) {
      textElem.setAttribute("hidden", true);
    } else {
      textElem.removeAttribute("hidden");
      let hostElem = this._element.querySelector(
        ".pointerlockfswarning-domain"
      );
      // Document's principal's URI has a host. Display a warning including it.
      let utils = {};
      ChromeUtils.import("resource://gre/modules/DownloadUtils.jsm", utils);
      hostElem.textContent = utils.DownloadUtils.getURIHost(uri.spec)[0];
    }

    this._element.dataset.identity =
      gIdentityHandler.pointerlockFsWarningClassName;

    // User should be allowed to explicitly disable
    // the prompt if they really want.
    if (this._timeoutHide.delay <= 0) {
      return;
    }

    // Explicitly set the last state to hidden to avoid the warning
    // box being hidden immediately because of mousemove.
    this._state = "onscreen";
    this._lastState = "hidden";
    this._timeoutHide.start();
  },

  close() {
    if (!this._element) {
      return;
    }
    // Cancel any pending timeout
    this._timeoutHide.cancel();
    this._timeoutShow.cancel();
    // Reset state of the warning box
    this._state = "hidden";
    this._element.setAttribute("hidden", true);
    // Remove all event listeners
    this._element.removeEventListener("transitionend", this);
    window.removeEventListener("mousemove", this, true);
    // Clear fields
    this._element = null;
    this._timeoutHide = null;
    this._timeoutShow = null;

    // Ensure focus switches away from the (now hidden) warning box.
    // If the user clicked buttons in the warning box, it would have
    // been focused, and any key events would be directed at the (now
    // hidden) chrome document instead of the target document.
    gBrowser.selectedBrowser.focus();
  },

  // State could be one of "onscreen", "ontop", "hiding", and
  // "hidden". Setting the state to "onscreen" and "ontop" takes
  // effect immediately, while setting it to "hidden" actually
  // turns the state to "hiding" before the transition finishes.
  _lastState: null,
  _STATES: ["hidden", "ontop", "onscreen"],
  get _state() {
    for (let state of this._STATES) {
      if (this._element.hasAttribute(state)) {
        return state;
      }
    }
    return "hiding";
  },
  set _state(newState) {
    let currentState = this._state;
    if (currentState == newState) {
      return;
    }
    if (currentState != "hiding") {
      this._lastState = currentState;
      this._element.removeAttribute(currentState);
    }
    if (newState != "hidden") {
      if (currentState != "hidden") {
        this._element.setAttribute(newState, true);
      } else {
        // When the previous state is hidden, the display was none,
        // thus no box was constructed. We need to wait for the new
        // display value taking effect first, otherwise, there won't
        // be any transition. Since requestAnimationFrame callback is
        // generally triggered before any style flush and layout, we
        // should wait for the second animation frame.
        requestAnimationFrame(() => {
          requestAnimationFrame(() => {
            if (this._element) {
              this._element.setAttribute(newState, true);
            }
          });
        });
      }
    }
  },

  handleEvent(event) {
    switch (event.type) {
      case "mousemove": {
        let state = this._state;
        if (state == "hidden") {
          // If the warning box is currently hidden, show it after
          // a short delay if the pointer is at the top.
          if (event.clientY != 0) {
            this._timeoutShow.cancel();
          } else if (this._timeoutShow.delay >= 0) {
            this._timeoutShow.start();
          }
        } else {
          let elemRect = this._element.getBoundingClientRect();
          if (state == "hiding" && this._lastState != "hidden") {
            // If we are on the hiding transition, and the pointer
            // moved near the box, restore to the previous state.
            if (event.clientY <= elemRect.bottom + 50) {
              this._state = this._lastState;
              this._timeoutHide.start();
            }
          } else if (state == "ontop" || this._lastState != "hidden") {
            // State being "ontop" or the previous state not being
            // "hidden" indicates this current warning box is shown
            // in response to user's action. Hide it immediately when
            // the pointer leaves that area.
            if (event.clientY > elemRect.bottom + 50) {
              this._state = "hidden";
              this._timeoutHide.cancel();
            }
          }
        }
        break;
      }
      case "transitionend": {
        if (this._state == "hiding") {
          this._element.setAttribute("hidden", true);
        }
        break;
      }
    }
  },
};

var PointerLock = {
  init() {
    window.messageManager.addMessageListener("PointerLock:Entered", this);
    window.messageManager.addMessageListener("PointerLock:Exited", this);
  },

  receiveMessage(aMessage) {
    switch (aMessage.name) {
      case "PointerLock:Entered": {
        PointerlockFsWarning.showPointerLock(aMessage.data.originNoSuffix);
        break;
      }
      case "PointerLock:Exited": {
        PointerlockFsWarning.close();
        break;
      }
    }
  },
};

var FullScreen = {
  _MESSAGES: [
    "DOMFullscreen:Request",
    "DOMFullscreen:NewOrigin",
    "DOMFullscreen:Exit",
    "DOMFullscreen:Painted",
  ],

  init() {
    // called when we go into full screen, even if initiated by a web page script
    window.addEventListener("fullscreen", this, true);
    window.addEventListener("willenterfullscreen", this, true);
    window.addEventListener("willexitfullscreen", this, true);
    window.addEventListener(
      "MozDOMFullscreen:Entered",
      this,
      /* useCapture */ true,
      /* wantsUntrusted */ false
    );
    window.addEventListener(
      "MozDOMFullscreen:Exited",
      this,
      /* useCapture */ true,
      /* wantsUntrusted */ false
    );
    for (let type of this._MESSAGES) {
      window.messageManager.addMessageListener(type, this);
    }

    if (window.fullScreen) {
      this.toggle();
    }
  },

  uninit() {
    for (let type of this._MESSAGES) {
      window.messageManager.removeMessageListener(type, this);
    }
    this.cleanup();
  },

  willToggle(aWillEnterFullscreen) {
    if (aWillEnterFullscreen) {
      document.documentElement.setAttribute("inFullscreen", true);
    } else {
      document.documentElement.removeAttribute("inFullscreen");
    }
  },

  toggle() {
    var enterFS = window.fullScreen;

    // Toggle the View:FullScreen command, which controls elements like the
    // fullscreen menuitem, and menubars.
    let fullscreenCommand = document.getElementById("View:FullScreen");
    if (enterFS) {
      fullscreenCommand.setAttribute("checked", enterFS);
    } else {
      fullscreenCommand.removeAttribute("checked");
    }

    if (AppConstants.platform == "macosx") {
      // Make sure the menu items are adjusted.
      document.getElementById("enterFullScreenItem").hidden = enterFS;
      document.getElementById("exitFullScreenItem").hidden = !enterFS;
    }

    if (!this._fullScrToggler) {
      this._fullScrToggler = document.getElementById("fullscr-toggler");
      this._fullScrToggler.addEventListener("mouseover", this._expandCallback);
      this._fullScrToggler.addEventListener("dragenter", this._expandCallback);
      this._fullScrToggler.addEventListener("touchmove", this._expandCallback, {
        passive: true,
      });
    }

    if (enterFS) {
      gNavToolbox.setAttribute("inFullscreen", true);
      document.documentElement.setAttribute("inFullscreen", true);
      if (!document.fullscreenElement && this.useLionFullScreen) {
        document.documentElement.setAttribute("OSXLionFullscreen", true);
      }
    } else {
      gNavToolbox.removeAttribute("inFullscreen");
      document.documentElement.removeAttribute("inFullscreen");
      document.documentElement.removeAttribute("OSXLionFullscreen");
    }

    if (!document.fullscreenElement) {
      this._updateToolbars(enterFS);
    }

    if (enterFS) {
      document.addEventListener("keypress", this._keyToggleCallback);
      document.addEventListener("popupshown", this._setPopupOpen);
      document.addEventListener("popuphidden", this._setPopupOpen);
      gURLBar.controller.addQueryListener(this);

      // In DOM fullscreen mode, we hide toolbars with CSS
      if (!document.fullscreenElement) {
        this.hideNavToolbox(true);
      }
    } else {
      this.showNavToolbox(false);
      // This is needed if they use the context menu to quit fullscreen
      this._isPopupOpen = false;
      this.cleanup();
    }

    if (enterFS && !document.fullscreenElement) {
      Services.telemetry.getHistogramById("FX_BROWSER_FULLSCREEN_USED").add(1);
    }
  },

  exitDomFullScreen() {
    document.exitFullscreen();
  },

  handleEvent(event) {
    switch (event.type) {
      case "willenterfullscreen":
        this.willToggle(true);
        break;
      case "willexitfullscreen":
        this.willToggle(false);
        break;
      case "fullscreen":
        this.toggle();
        break;
      case "MozDOMFullscreen:Entered": {
        // The event target is the element which requested the DOM
        // fullscreen. If we were entering DOM fullscreen for a remote
        // browser, the target would be the browser which was the parameter of
        // `remoteFrameFullscreenChanged` call. If the fullscreen
        // request was initiated from an in-process browser, we need
        // to get its corresponding browser here.
        let browser;
        if (event.target.ownerGlobal == window) {
          browser = event.target;
        } else {
          browser = event.target.ownerGlobal.docShell.chromeEventHandler;
        }

        // Addon installation should be cancelled when entering fullscreen for security and usability reasons.
        // Installation prompts in fullscreen can trick the user into installing unwanted addons.
        // In fullscreen the notification box does not have a clear visual association with its parent anymore.
        gXPInstallObserver.removeAllNotifications(browser);

        TelemetryStopwatch.start("FULLSCREEN_CHANGE_MS");
        this.enterDomFullscreen(browser);
        break;
      }
      case "MozDOMFullscreen:Exited":
        TelemetryStopwatch.start("FULLSCREEN_CHANGE_MS");
        this.cleanupDomFullscreen();
        break;
    }
  },

  receiveMessage(aMessage) {
    let browser = aMessage.target;
    switch (aMessage.name) {
      case "DOMFullscreen:Request": {
        window.windowUtils.remoteFrameFullscreenChanged(browser);
        break;
      }
      case "DOMFullscreen:NewOrigin": {
        // Don't show the warning if we've already exited fullscreen.
        if (document.fullscreen) {
          PointerlockFsWarning.showFullScreen(aMessage.data.originNoSuffix);
        }
        break;
      }
      case "DOMFullscreen:Exit": {
        window.windowUtils.remoteFrameFullscreenReverted();
        break;
      }
      case "DOMFullscreen:Painted": {
        Services.obs.notifyObservers(window, "fullscreen-painted");
        TelemetryStopwatch.finish("FULLSCREEN_CHANGE_MS");
        break;
      }
    }
  },

  enterDomFullscreen(aBrowser) {
    if (!document.fullscreenElement) {
      return;
    }

    // If we have a current pointerlock warning shown then hide it
    // before transition.
    PointerlockFsWarning.close();

    // If it is a remote browser, send a message to ask the content
    // to enter fullscreen state. We don't need to do so if it is an
    // in-process browser, since all related document should have
    // entered fullscreen state at this point.
    // This should be done before the active tab check below to ensure
    // that the content document handles the pending request. Doing so
    // before the check is fine since we also check the activeness of
    // the requesting document in content-side handling code.
    if (this._isRemoteBrowser(aBrowser)) {
      aBrowser.messageManager.sendAsyncMessage("DOMFullscreen:Entered");
    }

    // If we've received a fullscreen notification, we have to ensure that the
    // element that's requesting fullscreen belongs to the browser that's currently
    // active. If not, we exit fullscreen since the "full-screen document" isn't
    // actually visible now.
    if (
      !aBrowser ||
      gBrowser.selectedBrowser != aBrowser ||
      // The top-level window has lost focus since the request to enter
      // full-screen was made. Cancel full-screen.
      Services.focus.activeWindow != window
    ) {
      // This function is called synchronously in fullscreen change, so
      // we have to avoid calling exitFullscreen synchronously here.
      setTimeout(() => document.exitFullscreen(), 0);
      return;
    }

    document.documentElement.setAttribute("inDOMFullscreen", true);

    if (gFindBarInitialized) {
      gFindBar.close(true);
    }

    // Exit DOM full-screen mode when switching to a different tab.
    gBrowser.tabContainer.addEventListener("TabSelect", this.exitDomFullScreen);

    // Add listener to detect when the fullscreen window is re-focused.
    // If a fullscreen window loses focus, we show a warning when the
    // fullscreen window is refocused.
    window.addEventListener("activate", this);
  },

  cleanup() {
    if (!window.fullScreen) {
      MousePosTracker.removeListener(this);
      document.removeEventListener("keypress", this._keyToggleCallback);
      document.removeEventListener("popupshown", this._setPopupOpen);
      document.removeEventListener("popuphidden", this._setPopupOpen);
      gURLBar.controller.removeQueryListener(this);
    }
  },

  cleanupDomFullscreen() {
    window.messageManager.broadcastAsyncMessage("DOMFullscreen:CleanUp");

    PointerlockFsWarning.close();
    gBrowser.tabContainer.removeEventListener(
      "TabSelect",
      this.exitDomFullScreen
    );
    window.removeEventListener("activate", this);

    document.documentElement.removeAttribute("inDOMFullscreen");
  },

  _isRemoteBrowser(aBrowser) {
    return gMultiProcessBrowser && aBrowser.getAttribute("remote") == "true";
  },

  getMouseTargetRect() {
    return this._mouseTargetRect;
  },

  // Event callbacks
  _expandCallback() {
    FullScreen.showNavToolbox();
  },
  onMouseEnter() {
    FullScreen.hideNavToolbox();
  },
  _keyToggleCallback(aEvent) {
    // if we can use the keyboard (eg Ctrl+L or Ctrl+E) to open the toolbars, we
    // should provide a way to collapse them too.
    if (aEvent.keyCode == aEvent.DOM_VK_ESCAPE) {
      FullScreen.hideNavToolbox();
    } else if (aEvent.keyCode == aEvent.DOM_VK_F6) {
      // F6 is another shortcut to the address bar, but its not covered in OpenLocation()
      FullScreen.showNavToolbox();
    }
  },

  // Checks whether we are allowed to collapse the chrome
  _isPopupOpen: false,
  _isChromeCollapsed: false,

  _setPopupOpen(aEvent) {
    // Popups should only veto chrome collapsing if they were opened when the chrome was not collapsed.
    // Otherwise, they would not affect chrome and the user would expect the chrome to go away.
    // e.g. we wouldn't want the autoscroll icon firing this event, so when the user
    // toggles chrome when moving mouse to the top, it doesn't go away again.
    let target = aEvent.originalTarget;
    if (target.localName == "tooltip") {
      return;
    }
    if (
      aEvent.type == "popupshown" &&
      !FullScreen._isChromeCollapsed &&
      target.getAttribute("nopreventnavboxhide") != "true"
    ) {
      FullScreen._isPopupOpen = true;
    } else if (aEvent.type == "popuphidden") {
      FullScreen._isPopupOpen = false;
      // Try again to hide toolbar when we close the popup.
      FullScreen.hideNavToolbox(true);
    }
  },

  // UrlbarController listener method
  onViewOpen() {
    if (!this._isChromeCollapsed) {
      this._isPopupOpen = true;
    }
  },

  // UrlbarController listener method
  onViewClose() {
    this._isPopupOpen = false;
    this.hideNavToolbox(true);
  },

  get navToolboxHidden() {
    return this._isChromeCollapsed;
  },

  // Autohide helpers for the context menu item
  getAutohide(aItem) {
    aItem.setAttribute(
      "checked",
      Services.prefs.getBoolPref("browser.fullscreen.autohide")
    );
  },
  setAutohide() {
    Services.prefs.setBoolPref(
      "browser.fullscreen.autohide",
      !Services.prefs.getBoolPref("browser.fullscreen.autohide")
    );
    // Try again to hide toolbar when we change the pref.
    FullScreen.hideNavToolbox(true);
  },

  showNavToolbox(trackMouse = true) {
    this._fullScrToggler.hidden = true;
    gNavToolbox.removeAttribute("fullscreenShouldAnimate");
    gNavToolbox.style.marginTop = "";

    if (!this._isChromeCollapsed) {
      return;
    }

    // Track whether mouse is near the toolbox
    if (trackMouse && !this.useLionFullScreen) {
      let rect = gBrowser.tabpanels.getBoundingClientRect();
      this._mouseTargetRect = {
        top: rect.top + 50,
        bottom: rect.bottom,
        left: rect.left,
        right: rect.right,
      };
      MousePosTracker.addListener(this);
    }

    this._isChromeCollapsed = false;
    Services.obs.notifyObservers(null, "fullscreen-nav-toolbox", "shown");
  },

  hideNavToolbox(aAnimate = false) {
    if (this._isChromeCollapsed) {
      return;
    }
    if (!Services.prefs.getBoolPref("browser.fullscreen.autohide")) {
      return;
    }
    // a popup menu is open in chrome: don't collapse chrome
    if (this._isPopupOpen) {
      return;
    }
    // On OS X Lion we don't want to hide toolbars.
    if (this.useLionFullScreen) {
      return;
    }

    // a textbox in chrome is focused (location bar anyone?): don't collapse chrome
    let focused = document.commandDispatcher.focusedElement;
    if (
      focused &&
      focused.ownerDocument == document &&
      focused.localName == "input"
    ) {
      // But try collapse the chrome again when anything happens which can make
      // it lose the focus. We cannot listen on "blur" event on focused here
      // because that event can be triggered by "mousedown", and hiding chrome
      // would cause the content to move. This combination may split a single
      // click into two actionless halves.
      let retryHideNavToolbox = () => {
        // Wait for at least a frame to give it a chance to be passed down to
        // the content.
        requestAnimationFrame(() => {
          setTimeout(() => {
            // In the meantime, it's possible that we exited fullscreen somehow,
            // so only hide the toolbox if we're still in fullscreen mode.
            if (window.fullScreen) {
              this.hideNavToolbox(aAnimate);
            }
          }, 0);
        });
        window.removeEventListener("keydown", retryHideNavToolbox);
        window.removeEventListener("click", retryHideNavToolbox);
      };
      window.addEventListener("keydown", retryHideNavToolbox);
      window.addEventListener("click", retryHideNavToolbox);
      return;
    }

    this._fullScrToggler.hidden = false;

    if (
      aAnimate &&
      Services.prefs.getBoolPref("toolkit.cosmeticAnimations.enabled")
    ) {
      gNavToolbox.setAttribute("fullscreenShouldAnimate", true);
      // Hide the fullscreen toggler until the transition ends.
      let listener = () => {
        gNavToolbox.removeEventListener("transitionend", listener, true);
        if (this._isChromeCollapsed) {
          this._fullScrToggler.hidden = false;
        }
      };
      gNavToolbox.addEventListener("transitionend", listener, true);
      this._fullScrToggler.hidden = true;
    }

    gNavToolbox.style.marginTop =
      -gNavToolbox.getBoundingClientRect().height + "px";
    this._isChromeCollapsed = true;
    Services.obs.notifyObservers(null, "fullscreen-nav-toolbox", "hidden");

    MousePosTracker.removeListener(this);
  },

  _updateToolbars(aEnterFS) {
    for (let el of document.querySelectorAll(
      "toolbar[fullscreentoolbar=true]"
    )) {
      if (aEnterFS) {
        // Give the main nav bar and the tab bar the fullscreen context menu,
        // otherwise remove context menu to prevent breakage
        el.setAttribute("saved-context", el.getAttribute("context"));
        if (el.id == "nav-bar" || el.id == "TabsToolbar") {
          el.setAttribute("context", "autohide-context");
        } else {
          el.removeAttribute("context");
        }

        // Set the inFullscreen attribute to allow specific styling
        // in fullscreen mode
        el.setAttribute("inFullscreen", true);
      } else {
        if (el.hasAttribute("saved-context")) {
          el.setAttribute("context", el.getAttribute("saved-context"));
          el.removeAttribute("saved-context");
        }
        el.removeAttribute("inFullscreen");
      }
    }

    ToolbarIconColor.inferFromText("fullscreen", aEnterFS);

    // For Lion fullscreen, all fullscreen controls are hidden, don't
    // bother to touch them. If we don't stop here, the following code
    // could cause the native fullscreen button be shown unexpectedly.
    // See bug 1165570.
    if (this.useLionFullScreen) {
      return;
    }

    var fullscreenctls = document.getElementById("window-controls");
    var navbar = document.getElementById("nav-bar");
    var ctlsOnTabbar = window.toolbar.visible;
    if (fullscreenctls.parentNode == navbar && ctlsOnTabbar) {
      fullscreenctls.removeAttribute("flex");
      document.getElementById("TabsToolbar").appendChild(fullscreenctls);
    } else if (fullscreenctls.parentNode.id == "TabsToolbar" && !ctlsOnTabbar) {
      fullscreenctls.setAttribute("flex", "1");
      navbar.appendChild(fullscreenctls);
    }
    fullscreenctls.hidden = !aEnterFS;
  },
};
XPCOMUtils.defineLazyGetter(FullScreen, "useLionFullScreen", function() {
  // We'll only use OS X Lion full screen if we're
  // * on OS X
  // * on Lion or higher (Darwin 11+)
  // * have fullscreenbutton="true"
  return (
    AppConstants.isPlatformAndVersionAtLeast("macosx", 11) &&
    document.documentElement.getAttribute("fullscreenbutton") == "true"
  );
});

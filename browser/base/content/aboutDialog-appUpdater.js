/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Note: this file is included in aboutDialog.xul and preferences/advanced.xul
// if MOZ_UPDATER is defined.

/* import-globals-from aboutDialog.js */

// These two eslint directives should be removed when we remove handling for the
// legacy app updater.
/* eslint-disable prettier/prettier */
/* global AppUpdater, appUpdater, onUnload */

var { XPCOMUtils } = ChromeUtils.import(
  "resource://gre/modules/XPCOMUtils.jsm"
);
XPCOMUtils.defineLazyModuleGetters(this, {
  DownloadUtils: "resource://gre/modules/DownloadUtils.jsm",
  UpdateUtils: "resource://gre/modules/UpdateUtils.jsm",
});

var gAppUpdater;

(() => {

// If the new app updater is preffed off, load the legacy version.
if (!Services.prefs.getBoolPref("browser.aboutDialogNewAppUpdater", false)) {
  Services.scriptloader.loadSubScript(
    "chrome://browser/content/aboutDialog-appUpdater-legacy.js",
    this
  );
  return;
}

XPCOMUtils.defineLazyModuleGetters(this, {
  AppUpdater: "resource:///modules/AppUpdater.jsm",
});

function onUnload(aEvent) {
  if (gAppUpdater) {
    gAppUpdater.stopCurrentCheck();
    gAppUpdater = null;
  }
}

function appUpdater(options = {}) {
  this._appUpdater = new AppUpdater();

  this._appUpdateListener = (status, ...args) => {
    this._onAppUpdateStatus(status, ...args);
  };
  this._appUpdater.addListener(this._appUpdateListener);

  this.options = options;
  this.updateDeck = document.getElementById("updateDeck");

  this.bundle = Services.strings.createBundle(
    "chrome://browser/locale/browser.properties"
  );

  let manualURL = Services.urlFormatter.formatURLPref("app.update.url.manual");
  let manualLink = document.getElementById("manualLink");
  manualLink.textContent = manualURL;
  manualLink.href = manualURL;
  document.getElementById("failedLink").href = manualURL;

  this._appUpdater.check();
}

appUpdater.prototype = {
  stopCurrentCheck() {
    this._appUpdater.removeListener(this._appUpdateListener);
    this._appUpdater.stop();
  },

  get update() {
    return this._appUpdater.update;
  },

  _onAppUpdateStatus(status, ...args) {
    switch (status) {
      case AppUpdater.STATUS.UPDATE_DISABLED_BY_POLICY:
        this.selectPanel("policyDisabled");
        break;
      case AppUpdater.STATUS.READY_FOR_RESTART:
        this.selectPanel("apply");
        break;
      case AppUpdater.STATUS.OTHER_INSTANCE_HANDLING_UPDATES:
        this.selectPanel("otherInstanceHandlingUpdates");
        break;
      case AppUpdater.STATUS.DOWNLOADING:
        if (!args.length) {
          this.downloadStatus = document.getElementById("downloadStatus");
          this.downloadStatus.textContent = DownloadUtils.getTransferTotal(
            0,
            this.update.selectedPatch.size
          );
          this.selectPanel("downloading");
        } else {
          let [progress, max] = args;
          this.downloadStatus.textContent = DownloadUtils.getTransferTotal(
            progress,
            max
          );
        }
        break;
      case AppUpdater.STATUS.STAGING:
        this.selectPanel("applying");
        break;
      case AppUpdater.STATUS.CHECKING:
        this.selectPanel("checkingForUpdates");
        break;
      case AppUpdater.STATUS.NO_UPDATES_FOUND:
        this.selectPanel("noUpdatesFound");
        break;
      case AppUpdater.STATUS.UNSUPPORTED_SYSTEM:
        if (this.update.detailsURL) {
          let unsupportedLink = document.getElementById("unsupportedLink");
          unsupportedLink.href = this.update.detailsURL;
        }
        this.selectPanel("unsupportedSystem");
        break;
      case AppUpdater.STATUS.MANUAL_UPDATE:
        this.selectPanel("manualUpdate");
        break;
      case AppUpdater.STATUS.DOWNLOAD_AND_INSTALL:
        this.selectPanel("downloadAndInstall");
        break;
      case AppUpdater.STATUS.DOWNLOAD_FAILED:
        this.selectPanel("downloadFailed");
        break;
    }
  },

  /**
   * Sets the panel of the updateDeck.
   *
   * @param  aChildID
   *         The id of the deck's child to select, e.g. "apply".
   */
  selectPanel(aChildID) {
    let panel = document.getElementById(aChildID);

    let button = panel.querySelector("button");
    if (button) {
      if (aChildID == "downloadAndInstall") {
        let updateVersion = gAppUpdater.update.displayVersion;
        // Include the build ID if this is an "a#" (nightly or aurora) build
        if (/a\d+$/.test(updateVersion)) {
          let buildID = gAppUpdater.update.buildID;
          let year = buildID.slice(0, 4);
          let month = buildID.slice(4, 6);
          let day = buildID.slice(6, 8);
          updateVersion += ` (${year}-${month}-${day})`;
        }
        button.label = this.bundle.formatStringFromName(
          "update.downloadAndInstallButton.label",
          [updateVersion]
        );
        button.accessKey = this.bundle.GetStringFromName(
          "update.downloadAndInstallButton.accesskey"
        );
      }
      this.updateDeck.selectedPanel = panel;
      if (
        this.options.buttonAutoFocus &&
        (!document.commandDispatcher.focusedElement || // don't steal the focus
          document.commandDispatcher.focusedElement.localName == "button")
      ) {
        // except from the other buttons
        button.focus();
      }
    } else {
      this.updateDeck.selectedPanel = panel;
    }
  },

  /**
   * Check for updates
   */
  checkForUpdates() {
    this._appUpdater.checkForUpdates();
  },

  /**
   * Handles oncommand for the "Restart to Update" button
   * which is presented after the download has been downloaded.
   */
  buttonRestartAfterDownload() {
    if (!this._appUpdater.isReadyForRestart) {
      return;
    }

    gAppUpdater.selectPanel("restarting");

    // Notify all windows that an application quit has been requested.
    let cancelQuit = Cc["@mozilla.org/supports-PRBool;1"].createInstance(
      Ci.nsISupportsPRBool
    );
    Services.obs.notifyObservers(
      cancelQuit,
      "quit-application-requested",
      "restart"
    );

    // Something aborted the quit process.
    if (cancelQuit.data) {
      gAppUpdater.selectPanel("apply");
      return;
    }

    // If already in safe mode restart in safe mode (bug 327119)
    if (Services.appinfo.inSafeMode) {
      Services.startup.restartInSafeMode(Ci.nsIAppStartup.eAttemptQuit);
      return;
    }

    Services.startup.quit(
      Ci.nsIAppStartup.eAttemptQuit | Ci.nsIAppStartup.eRestart
    );
  },

  /**
   * Starts the download of an update mar.
   */
  startDownload() {
    this._appUpdater.startDownload();
  },
};

this.onUnload = onUnload;
this.appUpdater = appUpdater;

})();

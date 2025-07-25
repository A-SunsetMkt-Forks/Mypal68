/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const selectors = require("devtools/client/performance-new/store/selectors");

/**
 * The recording state manages the current state of the recording panel.
 * @param {string} state - A valid state in `recordingState`.
 * @param {object} options
 */
const changeRecordingState = (exports.changeRecordingState = (
  state,
  options = { didRecordingUnexpectedlyStopped: false }
) => ({
  type: "CHANGE_RECORDING_STATE",
  state,
  didRecordingUnexpectedlyStopped: options.didRecordingUnexpectedlyStopped,
}));

/**
 * This is the result of the initial questions about the state of the profiler.
 *
 * @param {boolean} isSupportedPlatform - This is a supported platform.
 * @param {string} recordingState - A valid state in `recordingState`.
 */
exports.reportProfilerReady = (isSupportedPlatform, recordingState) => ({
  type: "REPORT_PROFILER_READY",
  isSupportedPlatform,
  recordingState,
});

/**
 * Dispatch the given action, and then update the recording settings.
 * @param {object} action
 */
function _dispatchAndUpdatePreferences(action) {
  return (dispatch, getState) => {
    if (typeof action !== "object") {
      throw new Error(
        "This function assumes that the dispatched action is a simple object and " +
          "synchronous."
      );
    }
    dispatch(action);
    const setRecordingPreferences = selectors.getSetRecordingPreferencesFn(
      getState()
    );
    const recordingSettings = selectors.getRecordingSettings(getState());
    setRecordingPreferences(recordingSettings);
  };
}

/**
 * Updates the recording settings for the interval.
 * @param {number} interval
 */
exports.changeInterval = interval =>
  _dispatchAndUpdatePreferences({
    type: "CHANGE_INTERVAL",
    interval,
  });

/**
 * Updates the recording settings for the entries.
 * @param {number} entries
 */
exports.changeEntries = entries =>
  _dispatchAndUpdatePreferences({
    type: "CHANGE_ENTRIES",
    entries,
  });

/**
 * Updates the recording settings for the features.
 * @param {object} features
 */
exports.changeFeatures = features =>
  _dispatchAndUpdatePreferences({
    type: "CHANGE_FEATURES",
    features,
  });

/**
 * Updates the recording settings for the threads.
 * @param {array} threads
 */
exports.changeThreads = threads =>
  _dispatchAndUpdatePreferences({
    type: "CHANGE_THREADS",
    threads,
  });

/**
 * Updates the recording settings for the objdirs.
 * @param {array} objdirs
 */
exports.changeObjdirs = objdirs =>
  _dispatchAndUpdatePreferences({
    type: "CHANGE_OBJDIRS",
    objdirs,
  });

/**
 * Receive the values to intialize the store. See the reducer for what values
 * are expected.
 * @param {object} threads
 */
exports.initializeStore = values => ({
  type: "INITIALIZE_STORE",
  ...values,
});

/**
 * Start a new recording with the perfFront and update the internal recording state.
 */
exports.startRecording = () => {
  return (dispatch, getState) => {
    const recordingSettings = selectors.getRecordingSettings(getState());
    const perfFront = selectors.getPerfFront(getState());
    // In the case of the profiler popup, the startProfiler can be synchronous.
    // In order to properly allow the React components to handle the state changes
    // make sure and change the recording state first, then start the profiler.
    dispatch(changeRecordingState("request-to-start-recording"));
    perfFront.startProfiler(recordingSettings);
  };
};

/**
 * Stops the profiler, and opens the profile in a new window.
 * @param {object} window - The current window for the page.
 */
exports.getProfileAndStopProfiler = window => {
  return async (dispatch, getState) => {
    const perfFront = selectors.getPerfFront(getState());
    dispatch(changeRecordingState("request-to-get-profile-and-stop-profiler"));
    const profile = await perfFront.getProfileAndStopProfiler();

    if (window.gClosePopup) {
      // The close popup function only exists when we are in the popup.
      window.gClosePopup();
    }

    const getSymbolTable = selectors.getSymbolTableGetter(getState())(profile);
    const receiveProfile = selectors.getReceiveProfileFn(getState());
    receiveProfile(profile, getSymbolTable);
    dispatch(changeRecordingState("available-to-record"));
  };
};

/**
 * Stops the profiler, but does not try to retrieve the profile.
 */
exports.stopProfilerAndDiscardProfile = () => {
  return async (dispatch, getState) => {
    const perfFront = selectors.getPerfFront(getState());
    dispatch(changeRecordingState("request-to-stop-profiler"));
    perfFront.stopProfilerAndDiscardProfile();
  };
};

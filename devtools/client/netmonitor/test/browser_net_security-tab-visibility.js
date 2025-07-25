/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test that security details tab is visible only when it should.
 */

add_task(async function() {
  const TEST_DATA = [
    {
      desc: "http request",
      uri: "http://example.com" + CORS_SJS_PATH,
      visibleOnNewEvent: false,
      visibleOnSecurityInfo: false,
      visibleOnceComplete: false,
    },
    {
      desc: "working https request",
      uri: "https://example.com" + CORS_SJS_PATH,
      visibleOnNewEvent: false,
      visibleOnSecurityInfo: true,
      visibleOnceComplete: true,
    },
    {
      desc: "broken https request",
      uri: "https://nocert.example.com",
      isBroken: true,
      visibleOnNewEvent: false,
      visibleOnSecurityInfo: true,
      visibleOnceComplete: true,
    },
  ];

  const { tab, monitor } = await initNetMonitor(CUSTOM_GET_URL);
  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getSelectedRequest } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  store.dispatch(Actions.batchEnable(false));

  for (const testcase of TEST_DATA) {
    info("Testing Security tab visibility for " + testcase.desc);
    const onNewItem = monitor.panelWin.api.once(TEST_EVENTS.NETWORK_EVENT);
    const onComplete = testcase.isBroken
      ? waitForSecurityBrokenNetworkEvent()
      : waitForNetworkEvents(monitor, 1);

    info("Performing a request to " + testcase.uri);
    await ContentTask.spawn(tab.linkedBrowser, testcase.uri, async function(
      url
    ) {
      content.wrappedJSObject.performRequests(1, url);
    });

    info("Waiting for new network event.");
    await onNewItem;

    info("Selecting the request.");
    EventUtils.sendMouseEvent(
      { type: "mousedown" },
      document.querySelectorAll(".request-list-item")[0]
    );

    is(
      getSelectedRequest(store.getState()).securityState,
      undefined,
      "Security state has not yet arrived."
    );
    is(
      !!document.querySelector("#security-tab"),
      testcase.visibleOnNewEvent,
      "Security tab is " +
        (testcase.visibleOnNewEvent ? "visible" : "hidden") +
        " after new request was added to the menu."
    );

    if (testcase.visibleOnSecurityInfo) {
      // click security panel to lazy load the securityState
      await waitUntil(() => document.querySelector("#security-tab"));
      EventUtils.sendMouseEvent(
        { type: "click" },
        document.querySelector("#security-tab")
      );
      await waitUntil(() =>
        document.querySelector("#security-panel .security-info-value")
      );
      info("Waiting for security information to arrive.");

      await waitUntil(
        () => !!getSelectedRequest(store.getState()).securityState
      );
      ok(
        getSelectedRequest(store.getState()).securityState,
        "Security state arrived."
      );
    }

    is(
      !!document.querySelector("#security-tab"),
      testcase.visibleOnSecurityInfo,
      "Security tab is " +
        (testcase.visibleOnSecurityInfo ? "visible" : "hidden") +
        " after security information arrived."
    );

    info("Waiting for request to complete.");
    await onComplete;

    is(
      !!document.querySelector("#security-tab"),
      testcase.visibleOnceComplete,
      "Security tab is " +
        (testcase.visibleOnceComplete ? "visible" : "hidden") +
        " after request has been completed."
    );

    info("Clearing requests.");
    store.dispatch(Actions.clearRequests());
  }

  return teardown(monitor);

  /**
   * Returns a promise that's resolved once a request with security issues is
   * completed.
   */
  function waitForSecurityBrokenNetworkEvent() {
    const awaitedEvents = ["UPDATING_EVENT_TIMINGS", "RECEIVED_EVENT_TIMINGS"];

    const promises = awaitedEvents.map(event => {
      return monitor.panelWin.api.once(EVENTS[event]);
    });

    return Promise.all(promises);
  }
});

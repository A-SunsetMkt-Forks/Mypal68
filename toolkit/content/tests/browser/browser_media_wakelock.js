/**
 * Test whether the wakelock state is correct under different situations. However,
 * the lock state of power manager doesn't equal to the actual platform lock.
 * Now we don't have any way to detect whether platform lock is set correctly or
 * not, but we can at least make sure the specific topic's state in power manager
 * is correct.
 */
"use strict";

const PAGE =
  "https://example.com/browser/toolkit/content/tests/browser/file_video.html";
const PAGE2 =
  "https://example.com/browser/toolkit/content/tests/browser/file_videoWithoutAudioTrack.html";
const PAGE3 =
  "https://example.com/browser/toolkit/content/tests/browser/file_mediaPlayback2.html";

const powerManagerService = Cc["@mozilla.org/power/powermanagerservice;1"];
const powerManager = powerManagerService.getService(Ci.nsIPowerManagerService);

function wakeLockObserved(observeTopic, checkFn) {
  return new Promise(resolve => {
    function wakeLockListener() {}
    wakeLockListener.prototype = {
      QueryInterface: ChromeUtils.generateQI([Ci.nsIDOMMozWakeLockListener]),
      callback: (topic, state) => {
        if (topic == observeTopic && checkFn(state)) {
          powerManager.removeWakeLockListener(wakeLockListener.prototype);
          resolve();
        }
      },
    };
    powerManager.addWakeLockListener(wakeLockListener.prototype);
  });
}

function getWakeLockState(topic, needLock, isTabInForeground) {
  const tabState = isTabInForeground ? "foreground" : "background";
  const promise = wakeLockObserved(
    topic,
    state => state == `locked-${tabState}`
  );
  return {
    check: async () => {
      if (needLock) {
        await promise;
        ok(true, `requested '${topic}' wakelock in ${tabState}`);
      } else {
        const lockState = powerManager.getWakeLockState(topic);
        info(`topic=${topic}, state=${lockState}`);
        ok(lockState == "unlocked", `doesn't request lock for '${topic}'`);
      }
    },
  };
}

async function waitUntilVideoStarted({ muted, volume } = {}) {
  const video = content.document.getElementById("v");
  if (!video) {
    ok(false, "can't get media element!");
    return;
  }
  if (muted) {
    video.muted = muted;
  }
  if (volume !== undefined) {
    video.volume = volume;
  }
  ok(
    await video.play().then(
      () => true,
      () => false
    ),
    `video started playing.`
  );
}

async function test_media_wakelock({
  description,
  url,
  videoAttsParams,
  lockAudio,
  lockVideo,
}) {
  info(`- start a new test for '${description}' -`);
  info(`- open new foreground tab -`);
  const tab = await BrowserTestUtils.openNewForegroundTab(window.gBrowser, url);
  const browser = tab.linkedBrowser;

  let audioWakeLock = getWakeLockState("audio-playing", lockAudio, true);
  let videoWakeLock = getWakeLockState("video-playing", lockVideo, true);

  info(`- wait for media starting playing -`);
  await ContentTask.spawn(browser, videoAttsParams, waitUntilVideoStarted);
  await audioWakeLock.check();
  await videoWakeLock.check();

  info(`- switch tab to background -`);
  audioWakeLock = getWakeLockState("audio-playing", lockAudio, false);
  videoWakeLock = getWakeLockState("video-playing", lockVideo, false);
  const tab2 = await BrowserTestUtils.openNewForegroundTab(
    window.gBrowser,
    "about:blank"
  );
  await audioWakeLock.check();
  await videoWakeLock.check();

  info(`- switch tab to foreground again -`);
  audioWakeLock = getWakeLockState("audio-playing", lockAudio, true);
  videoWakeLock = getWakeLockState("video-playing", lockVideo, true);
  await BrowserTestUtils.switchTab(window.gBrowser, tab);
  await audioWakeLock.check();
  await videoWakeLock.check();

  info(`- remove tabs -`);
  BrowserTestUtils.removeTab(tab);
  BrowserTestUtils.removeTab(tab2);
}

add_task(async function start_tests() {
  await test_media_wakelock({
    description: "playing video",
    url: PAGE,
    lockAudio: true,
    lockVideo: true,
  });
  await test_media_wakelock({
    description: "playing muted video",
    url: PAGE,
    videoAttsParams: {
      muted: true,
    },
    lockAudio: false,
    lockVideo: true,
  });
  await test_media_wakelock({
    description: "playing volume=0 video",
    url: PAGE,
    videoAttsParams: {
      volume: 0.0,
    },
    lockAudio: false,
    lockVideo: true,
  });
  await test_media_wakelock({
    description: "playing video without audio track",
    url: PAGE2,
    lockAudio: false,
    lockVideo: false,
  });
  await test_media_wakelock({
    description: "playing only audio",
    url: PAGE3,
    lockAudio: true,
    lockVideo: false,
  });
});

"use strict";

ChromeUtils.defineModuleGetter(
  this,
  "Preferences",
  "resource://gre/modules/Preferences.jsm"
);

const {
  createAppInfo,
  promiseShutdownManager,
  promiseStartupManager,
} = AddonTestUtils;

AddonTestUtils.init(this);

createAppInfo("xpcshell@tests.mozilla.org", "XPCShell", "1", "42");

add_task(async function test_browser_settings() {
  const proxySvc = Ci.nsIProtocolProxyService;

  // Create an object to hold the values to which we will initialize the prefs.
  const PREFS = {
    "network.proxy.type": proxySvc.PROXYCONFIG_SYSTEM,
    "network.proxy.http": "",
    "network.proxy.http_port": 0,
    "network.proxy.share_proxy_settings": false,
    "network.proxy.ftp": "",
    "network.proxy.ftp_port": 0,
    "network.proxy.ssl": "",
    "network.proxy.ssl_port": 0,
    "network.proxy.socks": "",
    "network.proxy.socks_port": 0,
    "network.proxy.socks_version": 5,
    "network.proxy.socks_remote_dns": false,
    "network.proxy.no_proxies_on": "",
    "network.proxy.autoconfig_url": "",
    "signon.autologin.proxy": false,
  };

  async function background() {
    browser.test.onMessage.addListener(async (msg, value) => {
      let apiObj = browser.proxy.settings;
      let result = await apiObj.set({ value });
      if (msg === "set") {
        browser.test.assertTrue(result, "set returns true.");
        browser.test.sendMessage("settingData", await apiObj.get({}));
      } else {
        browser.test.assertFalse(result, "set returns false for a no-op.");
        browser.test.sendMessage("no-op set");
      }
    });
  }

  // Set prefs to our initial values.
  for (let pref in PREFS) {
    Preferences.set(pref, PREFS[pref]);
  }

  registerCleanupFunction(() => {
    // Reset the prefs.
    for (let pref in PREFS) {
      Preferences.reset(pref);
    }
  });

  let extension = ExtensionTestUtils.loadExtension({
    background,
    manifest: {
      permissions: ["proxy"],
    },
    incognitoOverride: "spanning",
    useAddonManager: "temporary",
  });

  await promiseStartupManager();
  await extension.startup();

  async function testSetting(value, expected, expectedValue = value) {
    extension.sendMessage("set", value);
    let data = await extension.awaitMessage("settingData");
    deepEqual(data.value, expectedValue, `The setting has the expected value.`);
    equal(
      data.levelOfControl,
      "controlled_by_this_extension",
      `The setting has the expected levelOfControl.`
    );
    for (let pref in expected) {
      equal(
        Preferences.get(pref),
        expected[pref],
        `${pref} set correctly for ${value}`
      );
    }
  }

  async function testProxy(config, expectedPrefs, expectedConfig = config) {
    // proxy.settings is not supported on Android.
    if (AppConstants.platform === "android") {
      return Promise.resolve();
    }

    let proxyConfig = {
      proxyType: "none",
      autoConfigUrl: "",
      autoLogin: false,
      proxyDNS: false,
      httpProxyAll: false,
      socksVersion: 5,
      passthrough: "",
      http: "",
      ftp: "",
      ssl: "",
      socks: "",
    };

    expectedConfig.proxyType = expectedConfig.proxyType || "system";

    return testSetting(
      config,
      expectedPrefs,
      Object.assign(proxyConfig, expectedConfig)
    );
  }

  await testProxy(
    { proxyType: "none" },
    { "network.proxy.type": proxySvc.PROXYCONFIG_DIRECT }
  );

  await testProxy(
    {
      proxyType: "autoDetect",
      autoLogin: true,
      proxyDNS: true,
    },
    {
      "network.proxy.type": proxySvc.PROXYCONFIG_WPAD,
      "signon.autologin.proxy": true,
      "network.proxy.socks_remote_dns": true,
    }
  );

  await testProxy(
    {
      proxyType: "system",
      autoLogin: false,
      proxyDNS: false,
    },
    {
      "network.proxy.type": proxySvc.PROXYCONFIG_SYSTEM,
      "signon.autologin.proxy": false,
      "network.proxy.socks_remote_dns": false,
    }
  );

  // Verify that proxyType is optional and it defaults to "system".
  await testProxy(
    {
      autoLogin: false,
      proxyDNS: false,
    },
    {
      "network.proxy.type": proxySvc.PROXYCONFIG_SYSTEM,
      "signon.autologin.proxy": false,
      "network.proxy.socks_remote_dns": false,
    }
  );

  await testProxy(
    {
      proxyType: "autoConfig",
      autoConfigUrl: "http://mozilla.org",
    },
    {
      "network.proxy.type": proxySvc.PROXYCONFIG_PAC,
      "network.proxy.autoconfig_url": "http://mozilla.org",
    }
  );

  await testProxy(
    {
      proxyType: "manual",
      http: "http://www.mozilla.org",
      autoConfigUrl: "",
    },
    {
      "network.proxy.type": proxySvc.PROXYCONFIG_MANUAL,
      "network.proxy.http": "www.mozilla.org",
      "network.proxy.http_port": 80,
      "network.proxy.autoconfig_url": "",
    },
    {
      proxyType: "manual",
      http: "www.mozilla.org:80",
      autoConfigUrl: "",
    }
  );

  // When using proxyAll, we expect all proxies to be set to
  // be the same as http.
  await testProxy(
    {
      proxyType: "manual",
      http: "http://www.mozilla.org:8080",
      ftp: "http://www.mozilla.org:1234",
      httpProxyAll: true,
    },
    {
      "network.proxy.type": proxySvc.PROXYCONFIG_MANUAL,
      "network.proxy.http": "www.mozilla.org",
      "network.proxy.http_port": 8080,
      "network.proxy.ftp": "www.mozilla.org",
      "network.proxy.ftp_port": 8080,
      "network.proxy.ssl": "www.mozilla.org",
      "network.proxy.ssl_port": 8080,
      "network.proxy.share_proxy_settings": true,
    },
    {
      proxyType: "manual",
      http: "www.mozilla.org:8080",
      ftp: "www.mozilla.org:8080",
      ssl: "www.mozilla.org:8080",
      socks: "",
      httpProxyAll: true,
    }
  );

  await testProxy(
    {
      proxyType: "manual",
      http: "www.mozilla.org:8080",
      httpProxyAll: false,
      ftp: "www.mozilla.org:8081",
      ssl: "www.mozilla.org:8082",
      socks: "mozilla.org:8083",
      socksVersion: 4,
      passthrough: ".mozilla.org",
    },
    {
      "network.proxy.type": proxySvc.PROXYCONFIG_MANUAL,
      "network.proxy.http": "www.mozilla.org",
      "network.proxy.http_port": 8080,
      "network.proxy.share_proxy_settings": false,
      "network.proxy.ftp": "www.mozilla.org",
      "network.proxy.ftp_port": 8081,
      "network.proxy.ssl": "www.mozilla.org",
      "network.proxy.ssl_port": 8082,
      "network.proxy.socks": "mozilla.org",
      "network.proxy.socks_port": 8083,
      "network.proxy.socks_version": 4,
      "network.proxy.no_proxies_on": ".mozilla.org",
    }
  );

  await testProxy(
    {
      proxyType: "manual",
      http: "http://www.mozilla.org",
      ftp: "ftp://www.mozilla.org",
      ssl: "https://www.mozilla.org",
      socks: "mozilla.org",
      socksVersion: 4,
      passthrough: ".mozilla.org",
    },
    {
      "network.proxy.type": proxySvc.PROXYCONFIG_MANUAL,
      "network.proxy.http": "www.mozilla.org",
      "network.proxy.http_port": 80,
      "network.proxy.share_proxy_settings": false,
      "network.proxy.ftp": "www.mozilla.org",
      "network.proxy.ftp_port": 21,
      "network.proxy.ssl": "www.mozilla.org",
      "network.proxy.ssl_port": 443,
      "network.proxy.socks": "mozilla.org",
      "network.proxy.socks_port": 1080,
      "network.proxy.socks_version": 4,
      "network.proxy.no_proxies_on": ".mozilla.org",
    },
    {
      proxyType: "manual",
      http: "www.mozilla.org:80",
      httpProxyAll: false,
      ftp: "www.mozilla.org:21",
      ssl: "www.mozilla.org:443",
      socks: "mozilla.org:1080",
      socksVersion: 4,
      passthrough: ".mozilla.org",
    }
  );

  await testProxy(
    {
      proxyType: "manual",
      http: "http://www.mozilla.org:80",
      ftp: "ftp://www.mozilla.org:21",
      ssl: "https://www.mozilla.org:443",
      socks: "mozilla.org:1080",
      socksVersion: 4,
      passthrough: ".mozilla.org",
    },
    {
      "network.proxy.type": proxySvc.PROXYCONFIG_MANUAL,
      "network.proxy.http": "www.mozilla.org",
      "network.proxy.http_port": 80,
      "network.proxy.share_proxy_settings": false,
      "network.proxy.ftp": "www.mozilla.org",
      "network.proxy.ftp_port": 21,
      "network.proxy.ssl": "www.mozilla.org",
      "network.proxy.ssl_port": 443,
      "network.proxy.socks": "mozilla.org",
      "network.proxy.socks_port": 1080,
      "network.proxy.socks_version": 4,
      "network.proxy.no_proxies_on": ".mozilla.org",
    },
    {
      proxyType: "manual",
      http: "www.mozilla.org:80",
      httpProxyAll: false,
      ftp: "www.mozilla.org:21",
      ssl: "www.mozilla.org:443",
      socks: "mozilla.org:1080",
      socksVersion: 4,
      passthrough: ".mozilla.org",
    }
  );

  await testProxy(
    {
      proxyType: "manual",
      http: "http://www.mozilla.org:80",
      ftp: "ftp://www.mozilla.org:80",
      ssl: "https://www.mozilla.org:80",
      socks: "mozilla.org:80",
      socksVersion: 4,
      passthrough: ".mozilla.org",
    },
    {
      "network.proxy.type": proxySvc.PROXYCONFIG_MANUAL,
      "network.proxy.http": "www.mozilla.org",
      "network.proxy.http_port": 80,
      "network.proxy.share_proxy_settings": false,
      "network.proxy.ftp": "www.mozilla.org",
      "network.proxy.ftp_port": 80,
      "network.proxy.ssl": "www.mozilla.org",
      "network.proxy.ssl_port": 80,
      "network.proxy.socks": "mozilla.org",
      "network.proxy.socks_port": 80,
      "network.proxy.socks_version": 4,
      "network.proxy.no_proxies_on": ".mozilla.org",
    },
    {
      proxyType: "manual",
      http: "www.mozilla.org:80",
      httpProxyAll: false,
      ftp: "www.mozilla.org:80",
      ssl: "www.mozilla.org:80",
      socks: "mozilla.org:80",
      socksVersion: 4,
      passthrough: ".mozilla.org",
    }
  );

  // Test resetting values.
  await testProxy(
    {
      proxyType: "none",
      http: "",
      ftp: "",
      ssl: "",
      socks: "",
      socksVersion: 5,
      passthrough: "",
    },
    {
      "network.proxy.type": proxySvc.PROXYCONFIG_DIRECT,
      "network.proxy.http": "",
      "network.proxy.http_port": 0,
      "network.proxy.ftp": "",
      "network.proxy.ftp_port": 0,
      "network.proxy.ssl": "",
      "network.proxy.ssl_port": 0,
      "network.proxy.socks": "",
      "network.proxy.socks_port": 0,
      "network.proxy.socks_version": 5,
      "network.proxy.no_proxies_on": "",
    }
  );

  await extension.unload();
  await promiseShutdownManager();
});

add_task(async function test_bad_value_proxy_config() {
  let background =
    AppConstants.platform === "android"
      ? async () => {
          await browser.test.assertRejects(
            browser.proxy.settings.set({
              value: {
                proxyType: "none",
              },
            }),
            /proxy.settings is not supported on android/,
            "proxy.settings.set rejects on Android."
          );

          await browser.test.assertRejects(
            browser.proxy.settings.get({}),
            /proxy.settings is not supported on android/,
            "proxy.settings.get rejects on Android."
          );

          await browser.test.assertRejects(
            browser.proxy.settings.clear({}),
            /proxy.settings is not supported on android/,
            "proxy.settings.clear rejects on Android."
          );

          browser.test.sendMessage("done");
        }
      : async () => {
          await browser.test.assertRejects(
            browser.proxy.settings.set({
              value: {
                proxyType: "abc",
              },
            }),
            /abc is not a valid value for proxyType/,
            "proxy.settings.set rejects with an invalid proxyType value."
          );

          await browser.test.assertRejects(
            browser.proxy.settings.set({
              value: {
                proxyType: "autoConfig",
              },
            }),
            /undefined is not a valid value for autoConfigUrl/,
            "proxy.settings.set for type autoConfig rejects with an empty autoConfigUrl value."
          );

          await browser.test.assertRejects(
            browser.proxy.settings.set({
              value: {
                proxyType: "autoConfig",
                autoConfigUrl: "abc",
              },
            }),
            /abc is not a valid value for autoConfigUrl/,
            "proxy.settings.set rejects with an invalid autoConfigUrl value."
          );

          await browser.test.assertRejects(
            browser.proxy.settings.set({
              value: {
                proxyType: "manual",
                socksVersion: "abc",
              },
            }),
            /abc is not a valid value for socksVersion/,
            "proxy.settings.set rejects with an invalid socksVersion value."
          );

          await browser.test.assertRejects(
            browser.proxy.settings.set({
              value: {
                proxyType: "manual",
                socksVersion: 3,
              },
            }),
            /3 is not a valid value for socksVersion/,
            "proxy.settings.set rejects with an invalid socksVersion value."
          );

          browser.test.sendMessage("done");
        };

  let extension = ExtensionTestUtils.loadExtension({
    background,
    manifest: {
      permissions: ["proxy"],
    },
    incognitoOverride: "spanning",
  });

  await extension.startup();
  await extension.awaitMessage("done");
  await extension.unload();
});

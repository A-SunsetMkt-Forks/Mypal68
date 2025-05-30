"use strict";

const STARTUP_APIS = ["backgroundPage"];

const STARTUP_MODULES = [
  "resource://gre/modules/Extension.jsm",
  "resource://gre/modules/ExtensionCommon.jsm",
  "resource://gre/modules/ExtensionParent.jsm",
  // FIXME: This is only loaded at startup for new extension installs.
  // Otherwise the data comes from the startup cache. We should test for
  // this.
  "resource://gre/modules/ExtensionPermissions.jsm",
  "resource://gre/modules/ExtensionProcessScript.jsm",
  "resource://gre/modules/ExtensionUtils.jsm",
];

if (!Services.prefs.getBoolPref("extensions.webextensions.remote")) {
  STARTUP_MODULES.push(
    "resource://gre/modules/ExtensionChild.jsm",
    "resource://gre/modules/ExtensionPageChild.jsm"
  );
}

AddonTestUtils.init(this);

// Tests that only the minimal set of API scripts and modules are loaded at
// startup for a simple extension.
add_task(async function test_loaded_scripts() {
  await ExtensionTestUtils.startAddonManager();

  let extension = ExtensionTestUtils.loadExtension({
    useAddonManager: "temporary",
    background() {},
    manifest: {},
  });

  await extension.startup();

  const { apiManager } = ExtensionParent;

  const loadedAPIs = Array.from(apiManager.modules.values())
    .filter(m => m.loaded || m.asyncLoaded)
    .map(m => m.namespaceName);

  deepEqual(
    loadedAPIs.sort(),
    STARTUP_APIS,
    "No extra APIs should be loaded at startup for a simple extension"
  );

  let loadedModules = Cu.loadedModules.filter(url =>
    url.startsWith("resource://gre/modules/Extension")
  );

  deepEqual(
    loadedModules.sort(),
    STARTUP_MODULES.sort(),
    "No extra extension modules should be loaded at startup for a simple extension"
  );

  await extension.unload();
});

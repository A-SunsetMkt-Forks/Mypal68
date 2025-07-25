/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test for Web Console commands registration.

add_task(async function() {
  const tab = await addTab("data:text/html,<div id=quack></div>");
  const target = await getTargetForTab(tab);

  const webConsoleFront = await target.getFront("console");

  // Fetch WebConsoleCommands so that it is available for next Content Tasks
  await ContentTask.spawn(gBrowser.selectedBrowser, null, async function() {
    const { require } = ChromeUtils.import(
      "resource://devtools/shared/Loader.jsm"
    );
    const {
      WebConsoleCommands,
    } = require("devtools/server/actors/webconsole/utils");

    // Bind the symbol on this in order to make it available for next tasks
    this.WebConsoleCommands = WebConsoleCommands;
  });

  await registerNewCommand(webConsoleFront);
  await wrapCommand(webConsoleFront);
  await unregisterCommand(webConsoleFront);
  await registerAccessor(webConsoleFront);
  await unregisterAfterOverridingTwice(webConsoleFront);
});

async function evaluateJSAndCheckResult(webConsoleFront, input, expected) {
  const response = await webConsoleFront.evaluateJSAsync(input);
  checkObject(response, expected);
}

async function registerNewCommand(webConsoleFront) {
  await ContentTask.spawn(gBrowser.selectedBrowser, null, async function() {
    this.WebConsoleCommands.register("setFoo", (owner, value) => {
      owner.window.foo = value;
      return "ok";
    });

    ok(
      this.WebConsoleCommands.hasCommand("setFoo"),
      "The command should be registered"
    );
  });

  const command = "setFoo('bar')";
  await evaluateJSAndCheckResult(webConsoleFront, command, {
    input: command,
    result: "ok",
  });

  await ContentTask.spawn(gBrowser.selectedBrowser, null, async function() {
    is(content.top.foo, "bar", "top.foo should equal to 'bar'");
  });
}

async function wrapCommand(webConsoleFront) {
  await ContentTask.spawn(gBrowser.selectedBrowser, null, async function() {
    const origKeys = this.WebConsoleCommands.getCommand("keys");

    const newKeys = (...args) => {
      const [, arg0] = args;
      if (arg0 === ">o_/") {
        return "bang!";
      }
      return origKeys(...args);
    };

    this.WebConsoleCommands.register("keys", newKeys);
    is(
      this.WebConsoleCommands.getCommand("keys"),
      newKeys,
      "the keys() command should have been replaced"
    );

    this.origKeys = origKeys;
  });

  await evaluateJSAndCheckResult(webConsoleFront, "keys('>o_/')", {
    result: "bang!",
  });

  await evaluateJSAndCheckResult(webConsoleFront, "keys({foo: 'bar'})", {
    result: {
      _grip: {
        class: "Array",
        preview: {
          items: ["foo"],
        },
      },
    },
  });

  await ContentTask.spawn(gBrowser.selectedBrowser, null, async function() {
    this.WebConsoleCommands.register("keys", this.origKeys);
    is(
      this.WebConsoleCommands.getCommand("keys"),
      this.origKeys,
      "the keys() command should be restored"
    );
    delete this.origKeys;
  });
}

async function unregisterCommand(webConsoleFront) {
  await ContentTask.spawn(gBrowser.selectedBrowser, null, async function() {
    this.WebConsoleCommands.unregister("setFoo");
  });

  await evaluateJSAndCheckResult(webConsoleFront, "setFoo", {
    input: "setFoo",
    result: {
      type: "undefined",
    },
    exceptionMessage: /setFoo is not defined/,
  });
}

async function registerAccessor(webConsoleFront) {
  await ContentTask.spawn(gBrowser.selectedBrowser, null, async function() {
    this.WebConsoleCommands.register("$foo", {
      get(owner) {
        const foo = owner.window.document.getElementById("quack");
        return owner.makeDebuggeeValue(foo);
      },
    });
  });

  const command = "$foo.textContent = '>o_/'";
  await evaluateJSAndCheckResult(webConsoleFront, command, {
    input: command,
    result: ">o_/",
  });

  await ContentTask.spawn(gBrowser.selectedBrowser, null, async function() {
    is(
      content.document.getElementById("quack").textContent,
      ">o_/",
      '#foo textContent should equal to ">o_/"'
    );
    this.WebConsoleCommands.unregister("$foo");
    ok(
      !this.WebConsoleCommands.hasCommand("$foo"),
      "$foo should be unregistered"
    );
  });
}

async function unregisterAfterOverridingTwice(webConsoleFront) {
  await ContentTask.spawn(gBrowser.selectedBrowser, null, async function() {
    this.WebConsoleCommands.register("keys", (owner, obj) => "command 1");
  });

  info("checking the value of the first override");
  await evaluateJSAndCheckResult(webConsoleFront, "keys('foo');", {
    result: "command 1",
  });

  await ContentTask.spawn(gBrowser.selectedBrowser, null, async function() {
    const orig = this.WebConsoleCommands.getCommand("keys");
    this.WebConsoleCommands.register("keys", (owner, obj) => {
      if (obj === "quack") {
        return "bang!";
      }
      return orig(owner, obj);
    });
  });

  info("checking the values after the second override");
  await evaluateJSAndCheckResult(webConsoleFront, "keys({});", {
    result: "command 1",
  });
  await evaluateJSAndCheckResult(webConsoleFront, "keys('quack');", {
    result: "bang!",
  });

  await ContentTask.spawn(gBrowser.selectedBrowser, null, async function() {
    this.WebConsoleCommands.unregister("keys");
  });

  info(
    "checking the value after unregistration (should restore " +
      "the original command)"
  );
  await evaluateJSAndCheckResult(webConsoleFront, "keys({});", {
    result: {
      _grip: {
        class: "Array",
        preview: { items: [] },
      },
    },
  });
}

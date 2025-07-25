/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests that the console waits for more input instead of evaluating
// when valid, but incomplete, statements are present upon pressing enter
// -or- when the user ends a line with shift + enter.

"use strict";

const TEST_URI =
  "http://example.com/browser/devtools/client/webconsole/" +
  "test/browser/test-console.html";

const SHOULD_ENTER_MULTILINE = [
  { input: "function foo() {" },
  { input: "var a = 1," },
  { input: "var a = 1;", shiftKey: true },
  { input: "function foo() { }", shiftKey: true },
  { input: "function" },
  { input: "(x) =>" },
  { input: "let b = {" },
  { input: "let a = [" },
  { input: "{" },
  { input: "{ bob: 3343," },
  { input: "function x(y=" },
  { input: "Array.from(" },
  // shift + enter creates a new line despite parse errors
  { input: "{2,}", shiftKey: true },
];
const SHOULD_EXECUTE = [
  { input: "function foo() { }" },
  { input: "var a = 1;" },
  { input: "function foo() { var a = 1; }" },
  { input: '"asdf"' },
  { input: "99 + 3" },
  { input: "1, 2, 3" },
  // errors
  { input: "function f(x) { let y = 1, }" },
  { input: "function f(x=,) {" },
  { input: "{2,}" },
];

const SINGLE_LINE_DATA = {
  timestamp: null,
  category: "devtools.main",
  method: "execute_js",
  object: "webconsole",
  value: null,
  extra: {
    lines: "1",
  },
};

const DATA = [
  SINGLE_LINE_DATA,
  SINGLE_LINE_DATA,
  SINGLE_LINE_DATA,
  SINGLE_LINE_DATA,
  SINGLE_LINE_DATA,
  SINGLE_LINE_DATA,
  SINGLE_LINE_DATA,
  SINGLE_LINE_DATA,
  SINGLE_LINE_DATA,
  {
    timestamp: null,
    category: "devtools.main",
    method: "execute_js",
    object: "webconsole",
    value: null,
    extra: {
      lines: "3",
    },
  },
];

add_task(async function() {
  // Ensure no events have been logged
  const hud = await openNewTabAndConsole(TEST_URI);

  for (const { input, shiftKey } of SHOULD_ENTER_MULTILINE) {
    setInputValue(hud, input);
    EventUtils.synthesizeKey("VK_RETURN", { shiftKey });

    // We need to remove the spaces at the end of the input since code mirror do some
    // automatic indent in some case.
    const newValue = getInputValue(hud).replace(/ +$/g, "");
    is(newValue, input + "\n", "A new line was added");
  }

  for (const { input, shiftKey } of SHOULD_EXECUTE) {
    setInputValue(hud, input);
    EventUtils.synthesizeKey("VK_RETURN", { shiftKey });

    await waitFor(() => !getInputValue(hud));
    is(getInputValue(hud), "", "Input is cleared");
  }

  await executeAndWaitForMessage(
    hud,
    "document.\nlocation.\nhref",
    TEST_URI,
    ".result"
  );
});

/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

// Testing that clicking the pick button switches the toolbox to the inspector
// panel.

const TEST_URI =
  "data:text/html;charset=UTF-8," + "<p>Switch to inspector on pick</p>";
const DATA = [
  {
    timestamp: 3562,
    category: "devtools.main",
    method: "enter",
    object: "webconsole",
    extra: {
      host: "bottom",
      start_state: "initial_panel",
      panel_name: "webconsole",
      cold: "true",
      message_count: "0",
      width: "1300",
    },
  },
  {
    timestamp: 3671,
    category: "devtools.main",
    method: "exit",
    object: "webconsole",
    extra: {
      host: "bottom",
      width: "1300",
      panel_name: "webconsole",
      next_panel: "inspector",
      reason: "inspect_dom",
    },
  },
  {
    timestamp: 3671,
    category: "devtools.main",
    method: "enter",
    object: "inspector",
    extra: {
      host: "bottom",
      start_state: "inspect_dom",
      panel_name: "inspector",
      cold: "true",
      width: "1300",
    },
  },
];

add_task(async function() {
  const tab = await addTab(TEST_URI);
  const toolbox = await openToolbox(tab);

  await startPickerAndAssertSwitchToInspector(toolbox);

  info("Stoppping element picker.");
  await toolbox.nodePicker.stop();

  checkResults();
});

async function openToolbox(tab) {
  info("Opening webconsole.");
  const target = await TargetFactory.forTab(tab);
  return gDevTools.showToolbox(target, "webconsole");
}

async function startPickerAndAssertSwitchToInspector(toolbox) {
  info("Clicking element picker button.");
  const pickButton = toolbox.doc.querySelector("#command-button-pick");
  pickButton.click();

  info("Waiting for inspector to be selected.");
  await toolbox.once("inspector-selected");
  is(toolbox.currentToolId, "inspector", "Switched to the inspector");
}

function checkResults() {
  for (const i in DATA) {
    const [timestamp, category, method, object, value, extra] = events[i];
    const expected = DATA[i];

    // ignore timestamp
    ok(timestamp > 0, "timestamp is greater than 0");
    is(category, expected.category, "category is correct");
    is(method, expected.method, "method is correct");
    is(object, expected.object, "object is correct");
    is(value, null, "value is correct");
    ok(extra.width > 0, "width is greater than 0");

    checkExtra("host", extra, expected);
    checkExtra("start_state", extra, expected);
    checkExtra("reason", extra, expected);
    checkExtra("panel_name", extra, expected);
    checkExtra("next_panel", extra, expected);
    checkExtra("message_count", extra, expected);
    checkExtra("cold", extra, expected);
  }
}

function checkExtra(propName, extra, expected) {
  if (extra[propName]) {
    is(extra[propName], expected.extra[propName], `${propName} is correct`);
  }
}

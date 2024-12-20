/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 */

enum ScrollState {"started", "stopped"};

dictionary ScrollViewChangeEventInit : EventInit {
  ScrollState state = "started";
};

[ChromeOnly,
 Exposed=Window]
interface ScrollViewChangeEvent : Event {
  constructor(DOMString type,
              optional ScrollViewChangeEventInit eventInit = {});

  readonly attribute ScrollState state;
};

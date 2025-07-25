/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * http://dev.w3.org/2011/webrtc/editor/webrtc.html#idl-def-RTCSessionDescription
 */

enum RTCSdpType {
  "offer",
  "pranswer",
  "answer",
  "rollback"
};

dictionary RTCSessionDescriptionInit {
  RTCSdpType type;
  DOMString sdp = "";
};

[Pref="media.peerconnection.enabled",
 JSImplementation="@mozilla.org/dom/rtcsessiondescription;1",
 Exposed=Window]
interface RTCSessionDescription {
  [Throws]
  constructor(optional RTCSessionDescriptionInit descriptionInitDict = {});

  // These should be readonly, but writing causes deprecation warnings for a bit
  attribute RTCSdpType type;
  attribute DOMString sdp;

  [Default] object toJSON();
};

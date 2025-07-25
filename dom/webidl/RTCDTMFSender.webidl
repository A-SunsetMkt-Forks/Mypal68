/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://www.w3.org/TR/webrtc/#rtcdtmfsender
 */

[Exposed=Window]
interface RTCDTMFSender : EventTarget {
    [Throws]
    void insertDTMF(DOMString tones,
                    optional unsigned long duration = 100,
                    optional unsigned long interToneGap = 70);
    attribute EventHandler  ontonechange;
    readonly attribute DOMString toneBuffer;
};

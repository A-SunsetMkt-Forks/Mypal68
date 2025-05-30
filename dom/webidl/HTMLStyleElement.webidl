/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * http://www.whatwg.org/specs/web-apps/current-work/#the-style-element
 * http://www.whatwg.org/specs/web-apps/current-work/#other-elements,-attributes-and-apis
 */

[Exposed=Window]
interface HTMLStyleElement : HTMLElement {
  [HTMLConstructor] constructor();

           [Pure]
           attribute boolean disabled;
           [CEReactions, SetterThrows, Pure]
           attribute DOMString media;
           [CEReactions, SetterThrows, Pure]
           attribute DOMString type;
};
HTMLStyleElement includes LinkStyle;


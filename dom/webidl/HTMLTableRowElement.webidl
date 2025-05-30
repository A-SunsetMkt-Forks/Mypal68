/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * http://www.whatwg.org/specs/web-apps/current-work/
 *
 * © Copyright 2004-2011 Apple Computer, Inc., Mozilla Foundation, and
 * Opera Software ASA. You are granted a license to use, reproduce
 * and create derivative works of this document.
 */

[Exposed=Window]
interface HTMLTableRowElement : HTMLElement {
  [HTMLConstructor] constructor();

  readonly attribute long rowIndex;
  readonly attribute long sectionRowIndex;
  readonly attribute HTMLCollection cells;
  [Throws]
  HTMLElement insertCell(optional long index = -1);
  [CEReactions, Throws]
  void deleteCell(long index);
};

partial interface HTMLTableRowElement {
           [CEReactions, SetterThrows]
           attribute DOMString align;
           [CEReactions, SetterThrows]
           attribute DOMString ch;
           [CEReactions, SetterThrows]
           attribute DOMString chOff;
           [CEReactions, SetterThrows]
           attribute DOMString vAlign;

  [CEReactions, SetterThrows]
           attribute [TreatNullAs=EmptyString] DOMString bgColor;
};

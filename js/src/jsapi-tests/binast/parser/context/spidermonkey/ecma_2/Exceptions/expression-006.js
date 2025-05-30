/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


/**
   File Name:          expression-006.js
   Corresponds to:     11.2.2-1-n.js
   ECMA Section:       11.2.2. The new operator
   Description:

   http://scopus/bugsplat/show_bug.cgi?id=327765

   Author:             christine@netscape.com
   Date:               12 november 1997
*/
var SECTION = "expression-006.js";
var VERSION = "JS1_4";
var TITLE   = "The new operator";
var BUGNUMBER="327765";

startTest();
writeHeaderToLog( SECTION + " "+ TITLE);

var result = "Failed";
var exception = "No exception thrown";
var expect = "Passed";

try {
  var OBJECT = new Object();
  result = new OBJECT();
} catch ( e ) {
  result = expect;
  exception = e.toString();
}

new TestCase(
  SECTION,
  "OBJECT = new Object; result = new OBJECT()" +
  " (threw " + exception +")",
  expect,
  result );

test();

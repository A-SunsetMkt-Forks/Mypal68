// |reftest| skip -- callable-boundary-realms is not supported
// Copyright (C) 2021 Rick Waldron. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
esid: sec-realm.prototype.evaluate
description: >
  The value of Realm.prototype.evaluate.name is 'evaluate'
info: |
  Every built-in function object, including constructors, has a "name" property
  whose value is a String. Unless otherwise specified, this value is the name
  that is given to the function in this specification.

  Unless otherwise specified, the "name" property of a built-in function
  object has the attributes
  { [[Writable]]: false, [[Enumerable]]: false, [[Configurable]]: true }.

includes: [propertyHelper.js]
features: [callable-boundary-realms]
---*/

verifyProperty(Realm.prototype.evaluate, "name", {
  value: "evaluate",
  enumerable: false,
  writable: false,
  configurable: true,
});

reportCompare(0, 0);

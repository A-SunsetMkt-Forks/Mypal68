// Copyright (C) 2019 Caio Lima (Igalia SL). All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
description: Every new evaluation of a class creates a different brand (private setter)
esid: sec-privatefieldget
info: |
  ClassTail : ClassHeritage { ClassBody }
    ...
    11. Let proto be ObjectCreate(protoParent).
    ...
    31. If PrivateBoundIdentifiers of ClassBody contains a Private Name P such that the P's [[Kind]] field is either "method" or "accessor",
      a. Set F.[[PrivateBrand]] to proto.
    ...

  PrivateBrandCheck(O, P)
    1. If O.[[PrivateBrands]] does not contain an entry e such that SameValue(e, P.[[Brand]]) is true,
      a. Throw a TypeError exception.
features: [class, class-methods-private]
---*/

let createAndInstantiateClass = function () {
  class C {
    set #m(v) { this._v = v; }

    access(o, v) {
      o.#m = v;
    }
  }

  let c = new C();
  return c;
};

let c1 = createAndInstantiateClass();
let c2 = createAndInstantiateClass();

c1.access(c1, 'test262');
assert.sameValue(c1._v, 'test262');
c2.access(c2, 'test262');
assert.sameValue(c2._v, 'test262');

assert.throws(TypeError, function() {
  c1.access(c2, 'foo');
}, 'invalid access of c1 private method');

assert.throws(TypeError, function() {
  c2.access(c1, 'foo');
}, 'invalid access of c2 private method');

reportCompare(0, 0);

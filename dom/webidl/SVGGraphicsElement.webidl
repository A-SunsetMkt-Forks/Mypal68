/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * http://www.w3.org/TR/SVG2/
 *
 * Copyright © 2012 W3C® (MIT, ERCIM, Keio), All Rights Reserved. W3C
 * liability, trademark and document use rules apply.
 */

dictionary SVGBoundingBoxOptions {
  boolean fill = true;
  boolean stroke = false;
  boolean markers = false;
  boolean clipped = false;
};

[Exposed=Window]
interface SVGGraphicsElement : SVGElement {
  [Pure]
  attribute boolean autofocus;

  readonly attribute SVGAnimatedTransformList transform;

  readonly attribute SVGElement? nearestViewportElement;
  readonly attribute SVGElement? farthestViewportElement;

  [NewObject]
  SVGRect getBBox(optional SVGBoundingBoxOptions aOptions = {});
  // Not implemented
  // SVGRect getStrokeBBox();
  SVGMatrix? getCTM();
  SVGMatrix? getScreenCTM();
  [Throws]
  SVGMatrix getTransformToElement(SVGGraphicsElement element);
};

SVGGraphicsElement includes SVGTests;

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://drafts.csswg.org/css-fonts/#om-fontfeaturevalues
 */

// https://drafts.csswg.org/css-fonts/#om-fontfeaturevalues
// but we don't implement anything remotely resembling the spec.
[Exposed=Window]
interface CSSFontFeatureValuesRule : CSSRule {
  [SetterThrows]
  attribute UTF8String fontFamily;

  // Not yet implemented
  //  readonly attribute CSSFontFeatureValuesMap annotation;
  //  readonly attribute CSSFontFeatureValuesMap ornaments;
  //  readonly attribute CSSFontFeatureValuesMap stylistic;
  //  readonly attribute CSSFontFeatureValuesMap swash;
  //  readonly attribute CSSFontFeatureValuesMap characterVariant;
  //  readonly attribute CSSFontFeatureValuesMap styleset;
};

partial interface CSSFontFeatureValuesRule {
  // Gecko addition?
  [SetterThrows]
  attribute UTF8String valueText;
};

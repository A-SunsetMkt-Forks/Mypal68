/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://drafts.csswg.org/css-conditional/#the-cssconditionrule-interface
 */

// https://drafts.csswg.org/css-conditional/#the-cssconditionrule-interface
[Exposed=Window]
interface CSSConditionRule : CSSGroupingRule {
  [SetterThrows]
  attribute UTF8String conditionText;
};

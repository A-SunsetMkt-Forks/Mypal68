// |reftest| error:SyntaxError
// This file was procedurally generated from the following sources:
// - src/assignment-target-type/primaryexpression-asyncfunctionexpression.case
// - src/assignment-target-type/invalid/parenthesized.template
/*---
description: PrimaryExpression AsyncFunctionExpression; Return invalid. (ParenthesizedExpression)
esid: sec-grouping-operator-static-semantics-assignmenttargettype
flags: [generated]
negative:
  phase: parse
  type: SyntaxError
info: |
    ParenthesizedExpression: (Expression)

    Return AssignmentTargetType of Expression.
---*/

$DONOTEVALUATE();

function _() {
  (async function () {}) = 1;
}

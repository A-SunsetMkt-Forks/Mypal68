// |reftest| error:SyntaxError
// This file was procedurally generated from the following sources:
// - src/assignment-target-type/shortcircuitexpression-question-assignmentexpression-else-assignmentexpression-1.case
// - src/assignment-target-type/invalid/parenthesized.template
/*---
description: Static Semantics AssignmentTargetType, Return invalid. (ParenthesizedExpression)
esid: sec-grouping-operator-static-semantics-assignmenttargettype
flags: [generated]
negative:
  phase: parse
  type: SyntaxError
info: |
    ParenthesizedExpression: (Expression)

    Return AssignmentTargetType of Expression.

    ShortCircuitExpression ? AssignmentExpression : AssignmentExpression
    Static Semantics AssignmentTargetType, Return invalid.

---*/

$DONOTEVALUATE();

function _() {
  ((1 ? 2 : 3)) = 1;
}

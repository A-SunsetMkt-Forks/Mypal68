// |reftest| error:SyntaxError
// This file was procedurally generated from the following sources:
// - src/assignment-target-type/bitwiseandexpression-bitwise-and-equalityexpression-2.case
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

    BitwiseANDExpression: BitwiseANDExpression & EqualityExpression
    Static Semantics AssignmentTargetType, Return invalid.

---*/

$DONOTEVALUATE();

function _() {
  (true & false) = 1;
}

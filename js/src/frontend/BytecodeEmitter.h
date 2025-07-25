/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* JS bytecode generation. */

#ifndef frontend_BytecodeEmitter_h
#define frontend_BytecodeEmitter_h

#include "mozilla/Assertions.h"  // MOZ_ASSERT
#include "mozilla/Attributes.h"  // MOZ_STACK_CLASS, MOZ_ALWAYS_INLINE, MOZ_NEVER_INLINE, MOZ_RAII
#include "mozilla/Maybe.h"   // mozilla::Maybe, mozilla::Some
#include "mozilla/Span.h"    // mozilla::Span
#include "mozilla/Vector.h"  // mozilla::Vector

#include <functional>  // std::function
#include <stddef.h>    // ptrdiff_t
#include <stdint.h>    // uint16_t, uint32_t

#include "jsapi.h"  // CompletionKind

#include "frontend/AbstractScopePtr.h"           // ScopeIndex
#include "frontend/BCEParserHandle.h"            // BCEParserHandle
#include "frontend/BytecodeControlStructures.h"  // NestableControl
#include "frontend/BytecodeOffset.h"             // BytecodeOffset
#include "frontend/BytecodeSection.h"  // BytecodeSection, PerScriptData, CGScopeList
#include "frontend/DestructuringFlavor.h"  // DestructuringFlavor
#include "frontend/EitherParser.h"         // EitherParser
#include "frontend/ErrorReporter.h"        // ErrorReporter
#include "frontend/FullParseHandler.h"     // FullParseHandler
#include "frontend/IteratorKind.h"         // IteratorKind
#include "frontend/JumpList.h"             // JumpList, JumpTarget
#include "frontend/NameAnalysisTypes.h"    // NameLocation
#include "frontend/NameCollections.h"      // AtomIndexMap
#include "frontend/ParseNode.h"            // ParseNode and subclasses
#include "frontend/Parser.h"               // Parser, PropListType
#include "frontend/ParserAtom.h"           // TaggedParserAtomIndex
#include "frontend/PrivateOpEmitter.h"     // PrivateOpEmitter
#include "frontend/ScriptIndex.h"          // ScriptIndex
#include "frontend/SharedContext.h"        // SharedContext, TopLevelFunction
#include "frontend/SourceNotes.h"          // SrcNoteType
#include "frontend/TokenStream.h"          // TokenPos
#include "frontend/ValueUsage.h"           // ValueUsage
#include "js/RootingAPI.h"                 // JS::Rooted, JS::Handle
#include "js/TypeDecls.h"                  // jsbytecode
#include "vm/BuiltinObjectKind.h"          // BuiltinObjectKind
#include "vm/BytecodeUtil.h"               // JSOp
#include "vm/CheckIsObjectKind.h"          // CheckIsObjectKind
#include "vm/FunctionPrefixKind.h"         // FunctionPrefixKind
#include "vm/GeneratorResumeKind.h"        // GeneratorResumeKind
#include "vm/JSFunction.h"                 // JSFunction
#include "vm/JSScript.h"       // JSScript, BaseScript, MemberInitializers
#include "vm/Runtime.h"        // ReportOutOfMemory
#include "vm/SharedStencil.h"  // GCThingIndex
#include "vm/StencilEnums.h"   // TryNoteKind
#include "vm/StringType.h"     // JSAtom
#include "vm/ThrowMsgKind.h"   // ThrowMsgKind, ThrowCondition

namespace js {
namespace frontend {

class CallOrNewEmitter;
class ClassEmitter;
class ElemOpEmitter;
class EmitterScope;
class NestableControl;
class PropertyEmitter;
class PropOpEmitter;
class OptionalEmitter;
class TDZCheckCache;
class TryEmitter;
class ScriptStencil;

enum class ValueIsOnStack { Yes, No };

struct MOZ_STACK_CLASS BytecodeEmitter {
  // Context shared between parsing and bytecode generation.
  SharedContext* const sc = nullptr;

  JSContext* const cx = nullptr;

  // Enclosing function or global context.
  BytecodeEmitter* const parent = nullptr;

  BytecodeSection bytecodeSection_;

 public:
  BytecodeSection& bytecodeSection() { return bytecodeSection_; }
  const BytecodeSection& bytecodeSection() const { return bytecodeSection_; }

 private:
  PerScriptData perScriptData_;

 public:
  PerScriptData& perScriptData() { return perScriptData_; }
  const PerScriptData& perScriptData() const { return perScriptData_; }

 private:
  // switchToMain sets this to the bytecode offset of the main section.
  mozilla::Maybe<uint32_t> mainOffset_ = {};

 public:
  // Private storage for parser wrapper. DO NOT REFERENCE INTERNALLY. May not be
  // initialized. Use |parser| instead.
  mozilla::Maybe<EitherParser> ep_ = {};
  BCEParserHandle* parser = nullptr;

  CompilationState& compilationState;

  uint32_t maxFixedSlots = 0; /* maximum number of fixed frame slots so far */

  // Index into scopeList of the body scope.
  GCThingIndex bodyScopeIndex = ScopeNote::NoScopeIndex;

  EmitterScope* varEmitterScope = nullptr;
  NestableControl* innermostNestableControl = nullptr;
  EmitterScope* innermostEmitterScope_ = nullptr;
  TDZCheckCache* innermostTDZCheckCache = nullptr;

  // When compiling in self-hosted mode, we have special intrinsics that act as
  // decorators for exported functions. To keeps things simple, we only allow
  // these to target the last top-level function emitted. This field tracks that
  // function.
  FunctionBox* prevSelfHostedTopLevelFunction = nullptr;

#ifdef DEBUG
  bool unstableEmitterScope = false;

  friend class AutoCheckUnstableEmitterScope;
#endif

  ParserAtomsTable& parserAtoms() { return compilationState.parserAtoms; }
  const ParserAtomsTable& parserAtoms() const {
    return compilationState.parserAtoms;
  }

  EmitterScope* innermostEmitterScope() const {
    MOZ_ASSERT(!unstableEmitterScope);
    return innermostEmitterScopeNoCheck();
  }
  EmitterScope* innermostEmitterScopeNoCheck() const {
    return innermostEmitterScope_;
  }

  // When parsing internal code such as self-hosted functions or synthetic
  // class constructors, we do not emit breakpoint and srcnote data since there
  // is no direcly corresponding user-visible sources.
  const bool suppressBreakpointsAndSourceNotes = false;

  // Script contains finally block.
  bool hasTryFinally = false;

  enum EmitterMode {
    Normal,

    // Emit JSOp::GetIntrinsic instead of JSOp::GetName and assert that
    // JSOp::GetName and JSOp::*GName don't ever get emitted. See the comment
    // for the field |selfHostingMode| in Parser.h for details.
    SelfHosting,

    // Check the static scope chain of the root function for resolving free
    // variable accesses in the script.
    LazyFunction
  };

  const EmitterMode emitterMode = Normal;

  mozilla::Maybe<uint32_t> scriptStartOffset = {};

  // The end location of a function body that is being emitted.
  mozilla::Maybe<uint32_t> functionBodyEndPos = {};

  // Jump target just before the final CheckReturn opcode in a derived class
  // constructor body.
  JumpList endOfDerivedClassConstructorBody = {};

  /*
   * Note that BytecodeEmitters are magic: they own the arena "top-of-stack"
   * space above their tempMark points. This means that you cannot alloc from
   * tempLifoAlloc and save the pointer beyond the next BytecodeEmitter
   * destruction.
   */
 private:
  // Internal constructor, for delegation use only.
  BytecodeEmitter(BytecodeEmitter* parent, SharedContext* sc,
                  CompilationState& compilationState, EmitterMode emitterMode);

  void initFromBodyPosition(TokenPos bodyPosition);

  /*
   * Helper for reporting that we have insufficient args.  pluralizer
   * should be "s" if requiredArgs is anything other than "1" and ""
   * if requiredArgs is "1".
   */
  void reportNeedMoreArgsError(ParseNode* pn, const char* errorName,
                               const char* requiredArgs, const char* pluralizer,
                               const ListNode* argsList);

 public:
  BytecodeEmitter(BytecodeEmitter* parent, BCEParserHandle* handle,
                  SharedContext* sc, CompilationState& compilationState,
                  EmitterMode emitterMode = Normal);

  BytecodeEmitter(BytecodeEmitter* parent, const EitherParser& parser,
                  SharedContext* sc, CompilationState& compilationState,
                  EmitterMode emitterMode = Normal);

  template <typename Unit>
  BytecodeEmitter(BytecodeEmitter* parent,
                  Parser<FullParseHandler, Unit>* parser, SharedContext* sc,
                  CompilationState& compilationState,
                  EmitterMode emitterMode = Normal)
      : BytecodeEmitter(parent, EitherParser(parser), sc, compilationState,
                        emitterMode) {}

  [[nodiscard]] bool init();
  [[nodiscard]] bool init(TokenPos bodyPosition);

  template <typename T>
  T* findInnermostNestableControl() const;

  template <typename T, typename Predicate /* (T*) -> bool */>
  T* findInnermostNestableControl(Predicate predicate) const;

  NameLocation lookupName(TaggedParserAtomIndex name);

  // See EmitterScope::lookupPrivate for details around brandLoc
  bool lookupPrivate(TaggedParserAtomIndex name, NameLocation& loc,
                     mozilla::Maybe<NameLocation>& brandLoc);

  // To implement Annex B and the formal parameter defaults scope semantics
  // requires accessing names that would otherwise be shadowed. This method
  // returns the access location of a name that is known to be bound in a
  // target scope.
  mozilla::Maybe<NameLocation> locationOfNameBoundInScope(
      TaggedParserAtomIndex name, EmitterScope* target);

  // Get the location of a name known to be bound in a given scope,
  // starting at the source scope.
  template <typename T>
  mozilla::Maybe<NameLocation> locationOfNameBoundInScopeType(
      TaggedParserAtomIndex name, EmitterScope* source);

  // Get the location of a name known to be bound in the function scope,
  // starting at the source scope.
  mozilla::Maybe<NameLocation> locationOfNameBoundInFunctionScope(
      TaggedParserAtomIndex name) {
    return locationOfNameBoundInScopeType<FunctionScope>(
        name, innermostEmitterScope());
  }

  void setVarEmitterScope(EmitterScope* emitterScope) {
    MOZ_ASSERT(emitterScope);
    MOZ_ASSERT(!varEmitterScope);
    varEmitterScope = emitterScope;
  }

  AbstractScopePtr outermostScope() const {
    return perScriptData().gcThingList().firstScope();
  }
  AbstractScopePtr innermostScope() const;
  ScopeIndex innermostScopeIndex() const;

  [[nodiscard]] MOZ_ALWAYS_INLINE bool makeAtomIndex(TaggedParserAtomIndex atom,
                                                     GCThingIndex* indexp) {
    MOZ_ASSERT(perScriptData().atomIndices());
    AtomIndexMap::AddPtr p = perScriptData().atomIndices()->lookupForAdd(atom);
    if (p) {
      *indexp = GCThingIndex(p->value());
      return true;
    }

    GCThingIndex index;
    if (!perScriptData().gcThingList().append(atom, &index)) {
      return false;
    }

    // `atomIndices()` uses uint32_t instead of GCThingIndex, because
    // GCThingIndex isn't trivial type.
    if (!perScriptData().atomIndices()->add(p, atom, index.index)) {
      ReportOutOfMemory(cx);
      return false;
    }

    *indexp = index;
    return true;
  }

  bool isInLoop();
  [[nodiscard]] bool checkSingletonContext();

  bool needsImplicitThis();

  size_t countThisEnvironmentHops();
  [[nodiscard]] bool emitThisEnvironmentCallee();
  [[nodiscard]] bool emitSuperBase();

  uint32_t mainOffset() const { return *mainOffset_; }

  bool inPrologue() const { return mainOffset_.isNothing(); }

  void switchToMain() {
    MOZ_ASSERT(inPrologue());
    mainOffset_.emplace(bytecodeSection().code().length());
  }

  void setFunctionBodyEndPos(uint32_t pos) {
    functionBodyEndPos = mozilla::Some(pos);
  }

  void setScriptStartOffsetIfUnset(uint32_t pos) {
    if (scriptStartOffset.isNothing()) {
      scriptStartOffset = mozilla::Some(pos);
    }
  }

  void reportError(ParseNode* pn, unsigned errorNumber, ...);
  void reportError(const mozilla::Maybe<uint32_t>& maybeOffset,
                   unsigned errorNumber, ...);

  // Fill in a ScriptStencil using this BCE data.
  bool intoScriptStencil(ScriptIndex scriptIndex);

  // If pn contains a useful expression, return true with *answer set to true.
  // If pn contains a useless expression, return true with *answer set to
  // false. Return false on error.
  //
  // The caller should initialize *answer to false and invoke this function on
  // an expression statement or similar subtree to decide whether the tree
  // could produce code that has any side effects.  For an expression
  // statement, we define useless code as code with no side effects, because
  // the main effect, the value left on the stack after the code executes,
  // will be discarded by a pop bytecode.
  [[nodiscard]] bool checkSideEffects(ParseNode* pn, bool* answer);

#ifdef DEBUG
  [[nodiscard]] bool checkStrictOrSloppy(JSOp op);
#endif

  // Add TryNote to the tryNoteList array. The start and end offset are
  // relative to current section.
  [[nodiscard]] bool addTryNote(TryNoteKind kind, uint32_t stackDepth,
                                BytecodeOffset start, BytecodeOffset end);

  // Indicates the emitter should not generate location or debugger source
  // notes. This lets us avoid generating notes for non-user code.
  bool skipLocationSrcNotes() const {
    return inPrologue() || suppressBreakpointsAndSourceNotes;
  }
  bool skipBreakpointSrcNotes() const {
    return inPrologue() || suppressBreakpointsAndSourceNotes;
  }

  // Append a new source note of the given type (and therefore size) to the
  // notes dynamic array, updating noteCount. Return the new note's index
  // within the array pointed at by current->notes as outparam.
  [[nodiscard]] bool newSrcNote(SrcNoteType type, unsigned* indexp = nullptr);
  [[nodiscard]] bool newSrcNote2(SrcNoteType type, ptrdiff_t operand,
                                 unsigned* indexp = nullptr);

  [[nodiscard]] bool newSrcNoteOperand(ptrdiff_t operand);

  // Control whether emitTree emits a line number note.
  enum EmitLineNumberNote { EMIT_LINENOTE, SUPPRESS_LINENOTE };

  // Emit code for the tree rooted at pn.
  [[nodiscard]] bool emitTree(ParseNode* pn,
                              ValueUsage valueUsage = ValueUsage::WantValue,
                              EmitLineNumberNote emitLineNote = EMIT_LINENOTE);

  [[nodiscard]] bool emitOptionalTree(
      ParseNode* pn, OptionalEmitter& oe,
      ValueUsage valueUsage = ValueUsage::WantValue);

  [[nodiscard]] bool emitDeclarationInstantiation(ParseNode* body);

  // Emit global, eval, or module code for tree rooted at body. Always
  // encompasses the entire source.
  [[nodiscard]] bool emitScript(ParseNode* body);

  // Calculate the `nslots` value for BCEScriptStencil constructor parameter.
  // Fails if it overflows.
  [[nodiscard]] bool getNslots(uint32_t* nslots);

  // Emit function code for the tree rooted at body.
  [[nodiscard]] bool emitFunctionScript(FunctionNode* funNode);

  [[nodiscard]] bool markStepBreakpoint();
  [[nodiscard]] bool markSimpleBreakpoint();
  [[nodiscard]] bool updateLineNumberNotes(uint32_t offset);
  [[nodiscard]] bool updateSourceCoordNotes(uint32_t offset);

  JSOp strictifySetNameOp(JSOp op);

  [[nodiscard]] bool emitCheck(JSOp op, ptrdiff_t delta,
                               BytecodeOffset* offset);

  // Emit one bytecode.
  [[nodiscard]] bool emit1(JSOp op);

  // Emit two bytecodes, an opcode (op) with a byte of immediate operand
  // (op1).
  [[nodiscard]] bool emit2(JSOp op, uint8_t op1);

  // Emit three bytecodes, an opcode with two bytes of immediate operands.
  [[nodiscard]] bool emit3(JSOp op, jsbytecode op1, jsbytecode op2);

  // Helper to duplicate one or more stack values. |slotFromTop| is the value's
  // depth on the JS stack, as measured from the top. |count| is the number of
  // values to duplicate, in theiro original order.
  [[nodiscard]] bool emitDupAt(unsigned slotFromTop, unsigned count = 1);

  // Helper to emit JSOp::Pop or JSOp::PopN.
  [[nodiscard]] bool emitPopN(unsigned n);

  // Helper to emit JSOp::Swap or JSOp::Pick.
  [[nodiscard]] bool emitPickN(uint8_t n);

  // Helper to emit JSOp::Swap or JSOp::Unpick.
  [[nodiscard]] bool emitUnpickN(uint8_t n);

  // Helper to emit JSOp::CheckIsObj.
  [[nodiscard]] bool emitCheckIsObj(CheckIsObjectKind kind);

  // Helper to emit JSOp::BuiltinObject.
  [[nodiscard]] bool emitBuiltinObject(BuiltinObjectKind kind);

  // Push whether the value atop of the stack is non-undefined and non-null.
  [[nodiscard]] bool emitPushNotUndefinedOrNull();

  // Emit a bytecode followed by an uint16 immediate operand stored in
  // big-endian order.
  [[nodiscard]] bool emitUint16Operand(JSOp op, uint32_t operand);

  // Emit a bytecode followed by an uint32 immediate operand.
  [[nodiscard]] bool emitUint32Operand(JSOp op, uint32_t operand);

  // Emit (1 + extra) bytecodes, for N bytes of op and its immediate operand.
  [[nodiscard]] bool emitN(JSOp op, size_t extra,
                           BytecodeOffset* offset = nullptr);

  [[nodiscard]] bool emitDouble(double dval);
  [[nodiscard]] bool emitNumberOp(double dval);

  [[nodiscard]] bool emitBigIntOp(BigIntLiteral* bigint);

  [[nodiscard]] bool emitThisLiteral(ThisLiteral* pn);
  [[nodiscard]] bool emitGetFunctionThis(NameNode* thisName);
  [[nodiscard]] bool emitGetFunctionThis(
      const mozilla::Maybe<uint32_t>& offset);
  [[nodiscard]] bool emitGetThisForSuperBase(UnaryNode* superBase);
  [[nodiscard]] bool emitSetThis(BinaryNode* setThisNode);
  [[nodiscard]] bool emitCheckDerivedClassConstructorReturn();

  // Handle jump opcodes and jump targets.
  [[nodiscard]] bool emitJumpTargetOp(JSOp op, BytecodeOffset* off);
  [[nodiscard]] bool emitJumpTarget(JumpTarget* target);
  [[nodiscard]] bool emitJumpNoFallthrough(JSOp op, JumpList* jump);
  [[nodiscard]] bool emitJump(JSOp op, JumpList* jump);
  void patchJumpsToTarget(JumpList jump, JumpTarget target);
  [[nodiscard]] bool emitJumpTargetAndPatch(JumpList jump);

  [[nodiscard]] bool emitCall(
      JSOp op, uint16_t argc,
      const mozilla::Maybe<uint32_t>& sourceCoordOffset);
  [[nodiscard]] bool emitCall(JSOp op, uint16_t argc, ParseNode* pn = nullptr);
  [[nodiscard]] bool emitCallIncDec(UnaryNode* incDec);

  mozilla::Maybe<uint32_t> getOffsetForLoop(ParseNode* nextpn);

  enum class GotoKind { Break, Continue };
  [[nodiscard]] bool emitGoto(NestableControl* target, JumpList* jumplist,
                              GotoKind kind);

  [[nodiscard]] bool emitGCIndexOp(JSOp op, GCThingIndex index);

  [[nodiscard]] bool emitAtomOp(JSOp op, TaggedParserAtomIndex atom);
  [[nodiscard]] bool emitAtomOp(JSOp op, GCThingIndex atomIndex);

  [[nodiscard]] bool emitArrayLiteral(ListNode* array);
  [[nodiscard]] bool emitArray(ParseNode* arrayHead, uint32_t count);

  [[nodiscard]] bool emitInternedScopeOp(GCThingIndex index, JSOp op);
  [[nodiscard]] bool emitInternedObjectOp(GCThingIndex index, JSOp op);
  [[nodiscard]] bool emitObjectPairOp(GCThingIndex index1, GCThingIndex index2,
                                      JSOp op);
  [[nodiscard]] bool emitRegExp(GCThingIndex index);

  [[nodiscard]] MOZ_NEVER_INLINE bool emitFunction(FunctionNode* funNode,
                                                   bool needsProto = false);
  [[nodiscard]] MOZ_NEVER_INLINE bool emitObject(ListNode* objNode);

  [[nodiscard]] bool emitHoistedFunctionsInList(ListNode* stmtList);

  // Can we use the object-literal writer either in singleton-object mode (with
  // values) or in template mode (field names only, no values) for the property
  // list?
  void isPropertyListObjLiteralCompatible(ListNode* obj, bool* withValues,
                                          bool* withoutValues);
  bool isArrayObjLiteralCompatible(ParseNode* arrayHead);

  [[nodiscard]] bool emitPropertyList(ListNode* obj, PropertyEmitter& pe,
                                      PropListType type);

  [[nodiscard]] bool emitPropertyListObjLiteral(ListNode* obj,
                                                ObjLiteralFlags flags,
                                                bool useObjLiteralValues);

  [[nodiscard]] bool emitDestructuringRestExclusionSetObjLiteral(
      ListNode* pattern);

  [[nodiscard]] bool emitObjLiteralArray(ParseNode* arrayHead);

  // Is a field value OBJLITERAL-compatible?
  [[nodiscard]] bool isRHSObjLiteralCompatible(ParseNode* value);

  [[nodiscard]] bool emitObjLiteralValue(ObjLiteralWriter& writer,
                                         ParseNode* value);

  mozilla::Maybe<MemberInitializers> setupMemberInitializers(
      ListNode* classMembers, FieldPlacement placement);
  [[nodiscard]] bool emitCreateFieldKeys(ListNode* obj,
                                         FieldPlacement placement);
  [[nodiscard]] bool emitCreateMemberInitializers(ClassEmitter& ce,
                                                  ListNode* obj,
                                                  FieldPlacement placement);
  const MemberInitializers& findMemberInitializersForCall();
  [[nodiscard]] bool emitInitializeInstanceMembers();
  [[nodiscard]] bool emitInitializeStaticFields(ListNode* classMembers);

  [[nodiscard]] bool emitPrivateMethodInitializers(ClassEmitter& ce,
                                                   ListNode* obj);
  [[nodiscard]] bool emitPrivateMethodInitializer(
      ClassEmitter& ce, ParseNode* prop, ParseNode* propName,
      TaggedParserAtomIndex storedMethodAtom, AccessorType accessorType);

  // To catch accidental misuse, emitUint16Operand/emit3 assert that they are
  // not used to unconditionally emit JSOp::GetLocal. Variable access should
  // instead be emitted using EmitVarOp. In special cases, when the caller
  // definitely knows that a given local slot is unaliased, this function may be
  // used as a non-asserting version of emitUint16Operand.
  [[nodiscard]] bool emitLocalOp(JSOp op, uint32_t slot);

  [[nodiscard]] bool emitArgOp(JSOp op, uint16_t slot);
  [[nodiscard]] bool emitEnvCoordOp(JSOp op, EnvironmentCoordinate ec);

  [[nodiscard]] bool emitGetNameAtLocation(TaggedParserAtomIndex name,
                                           const NameLocation& loc);
  [[nodiscard]] bool emitGetName(TaggedParserAtomIndex name) {
    return emitGetNameAtLocation(name, lookupName(name));
  }
  [[nodiscard]] bool emitGetName(NameNode* name);
  [[nodiscard]] bool emitGetPrivateName(NameNode* name);
  [[nodiscard]] bool emitGetPrivateName(TaggedParserAtomIndex name);

  [[nodiscard]] bool emitTDZCheckIfNeeded(TaggedParserAtomIndex name,
                                          const NameLocation& loc,
                                          ValueIsOnStack isOnStack);

  [[nodiscard]] bool emitNameIncDec(UnaryNode* incDec);

  [[nodiscard]] bool emitDeclarationList(ListNode* declList);
  [[nodiscard]] bool emitSingleDeclaration(ListNode* declList, NameNode* decl,
                                           ParseNode* initializer);
  [[nodiscard]] bool emitAssignmentRhs(ParseNode* rhs,
                                       TaggedParserAtomIndex anonFunctionName);
  [[nodiscard]] bool emitAssignmentRhs(uint8_t offset);

  [[nodiscard]] bool emitPrepareIteratorResult();
  [[nodiscard]] bool emitFinishIteratorResult(bool done);
  [[nodiscard]] bool iteratorResultShape(GCThingIndex* outShape);

  // Convert and add `writer` data to stencil.
  // Iff it suceeds, `outIndex` out parameter is initialized to the index of the
  // object in GC things vector.
  [[nodiscard]] bool addObjLiteralData(ObjLiteralWriter& writer,
                                       GCThingIndex* outIndex);

  [[nodiscard]] bool emitGetDotGeneratorInInnermostScope() {
    return emitGetDotGeneratorInScope(*innermostEmitterScope());
  }
  [[nodiscard]] bool emitGetDotGeneratorInScope(EmitterScope& currentScope);

  [[nodiscard]] bool allocateResumeIndex(BytecodeOffset offset,
                                         uint32_t* resumeIndex);
  [[nodiscard]] bool allocateResumeIndexRange(
      mozilla::Span<BytecodeOffset> offsets, uint32_t* firstResumeIndex);

  [[nodiscard]] bool emitInitialYield(UnaryNode* yieldNode);
  [[nodiscard]] bool emitYield(UnaryNode* yieldNode);
  [[nodiscard]] bool emitYieldOp(JSOp op);
  [[nodiscard]] bool emitYieldStar(ParseNode* iter);
  [[nodiscard]] bool emitAwaitInInnermostScope() {
    return emitAwaitInScope(*innermostEmitterScope());
  }
  [[nodiscard]] bool emitAwaitInInnermostScope(UnaryNode* awaitNode);
  [[nodiscard]] bool emitAwaitInScope(EmitterScope& currentScope);

  [[nodiscard]] bool emitPushResumeKind(GeneratorResumeKind kind);

  [[nodiscard]] bool emitPropLHS(PropertyAccess* prop);
  [[nodiscard]] bool emitPropIncDec(UnaryNode* incDec);

  [[nodiscard]] bool emitComputedPropertyName(UnaryNode* computedPropName);

  [[nodiscard]] bool emitObjAndKey(ParseNode* exprOrSuper, ParseNode* key,
                                   ElemOpEmitter& eoe);

  // Emit bytecode to put operands for a JSOp::GetElem/CallElem/SetElem/DelElem
  // opcode onto the stack in the right order. In the case of SetElem, the
  // value to be assigned must already be pushed.
  enum class EmitElemOption { Get, Call, IncDec, CompoundAssign, Ref };
  [[nodiscard]] bool emitElemOperands(PropertyByValue* elem,
                                      EmitElemOption opts);

  [[nodiscard]] bool emitElemObjAndKey(PropertyByValue* elem, bool isSuper,
                                       ElemOpEmitter& eoe);
  [[nodiscard]] bool emitElemOpBase(JSOp op);

  [[nodiscard]] bool emitElemIncDec(UnaryNode* incDec);
  [[nodiscard]] bool emitObjAndPrivateName(PrivateMemberAccess* elem,
                                           ElemOpEmitter& eoe);
  [[nodiscard]] bool emitPrivateIncDec(UnaryNode* incDec);

  [[nodiscard]] bool emitCatch(BinaryNode* catchClause);
  [[nodiscard]] bool emitIf(TernaryNode* ifNode);
  [[nodiscard]] bool emitWith(BinaryNode* withNode);

  [[nodiscard]] MOZ_NEVER_INLINE bool emitLabeledStatement(
      const LabeledStatement* labeledStmt);
  [[nodiscard]] MOZ_NEVER_INLINE bool emitLexicalScope(
      LexicalScopeNode* lexicalScope);
  [[nodiscard]] bool emitLexicalScopeBody(
      ParseNode* body, EmitLineNumberNote emitLineNote = EMIT_LINENOTE);
  [[nodiscard]] MOZ_NEVER_INLINE bool emitSwitch(SwitchStatement* switchStmt);
  [[nodiscard]] MOZ_NEVER_INLINE bool emitTry(TryNode* tryNode);

  [[nodiscard]] bool emitGoSub(JumpList* jump);

  // emitDestructuringLHSRef emits the lhs expression's reference.
  // If the lhs expression is object property |OBJ.prop|, it emits |OBJ|.
  // If it's object element |OBJ[ELEM]|, it emits |OBJ| and |ELEM|.
  // If there's nothing to evaluate for the reference, it emits nothing.
  // |emitted| parameter receives the number of values pushed onto the stack.
  [[nodiscard]] bool emitDestructuringLHSRef(ParseNode* target,
                                             size_t* emitted);

  // emitSetOrInitializeDestructuring assumes the lhs expression's reference
  // and the to-be-destructured value has been pushed on the stack.  It emits
  // code to destructure a single lhs expression (either a name or a compound
  // []/{} expression).
  [[nodiscard]] bool emitSetOrInitializeDestructuring(ParseNode* target,
                                                      DestructuringFlavor flav);

  // emitDestructuringObjRestExclusionSet emits the property exclusion set
  // for the rest-property in an object pattern.
  [[nodiscard]] bool emitDestructuringObjRestExclusionSet(ListNode* pattern);

  // emitDestructuringOps assumes the to-be-destructured value has been
  // pushed on the stack and emits code to destructure each part of a [] or
  // {} lhs expression.
  [[nodiscard]] bool emitDestructuringOps(ListNode* pattern,
                                          DestructuringFlavor flav);
  [[nodiscard]] bool emitDestructuringOpsArray(ListNode* pattern,
                                               DestructuringFlavor flav);
  [[nodiscard]] bool emitDestructuringOpsObject(ListNode* pattern,
                                                DestructuringFlavor flav);

  enum class CopyOption { Filtered, Unfiltered };

  // Calls either the |CopyDataProperties| or the
  // |CopyDataPropertiesUnfiltered| intrinsic function, consumes three (or
  // two in the latter case) elements from the stack.
  [[nodiscard]] bool emitCopyDataProperties(CopyOption option);

  // emitIterator expects the iterable to already be on the stack.
  // It will replace that stack value with the corresponding iterator
  [[nodiscard]] bool emitIterator();

  [[nodiscard]] bool emitAsyncIterator();

  // Pops iterator from the top of the stack. Pushes the result of |.next()|
  // onto the stack.
  [[nodiscard]] bool emitIteratorNext(
      const mozilla::Maybe<uint32_t>& callSourceCoordOffset,
      IteratorKind kind = IteratorKind::Sync, bool allowSelfHosted = false);
  [[nodiscard]] bool emitIteratorCloseInScope(
      EmitterScope& currentScope, IteratorKind iterKind = IteratorKind::Sync,
      CompletionKind completionKind = CompletionKind::Normal,
      bool allowSelfHosted = false);
  [[nodiscard]] bool emitIteratorCloseInInnermostScope(
      IteratorKind iterKind = IteratorKind::Sync,
      CompletionKind completionKind = CompletionKind::Normal,
      bool allowSelfHosted = false) {
    return emitIteratorCloseInScope(*innermostEmitterScope(), iterKind,
                                    completionKind, allowSelfHosted);
  }

  template <typename InnerEmitter>
  [[nodiscard]] bool wrapWithDestructuringTryNote(int32_t iterDepth,
                                                  InnerEmitter emitter);

  [[nodiscard]] bool defineHoistedTopLevelFunctions(ParseNode* body);

  // Check if the value on top of the stack is "undefined". If so, replace
  // that value on the stack with the value defined by |defaultExpr|.
  // |pattern| is a lhs node of the default expression.  If it's an
  // identifier and |defaultExpr| is an anonymous function, |SetFunctionName|
  // is called at compile time.
  [[nodiscard]] bool emitDefault(ParseNode* defaultExpr, ParseNode* pattern);

  [[nodiscard]] bool emitAnonymousFunctionWithName(ParseNode* node,
                                                   TaggedParserAtomIndex name);

  [[nodiscard]] bool emitAnonymousFunctionWithComputedName(
      ParseNode* node, FunctionPrefixKind prefixKind);

  [[nodiscard]] bool setFunName(FunctionBox* fun, TaggedParserAtomIndex name);
  [[nodiscard]] bool emitInitializer(ParseNode* initializer,
                                     ParseNode* pattern);

  [[nodiscard]] bool emitCallSiteObjectArray(ListNode* cookedOrRaw,
                                             GCThingIndex* outArrayIndex);
  [[nodiscard]] bool emitCallSiteObject(CallSiteNode* callSiteObj);
  [[nodiscard]] bool emitTemplateString(ListNode* templateString);
  [[nodiscard]] bool emitAssignmentOrInit(ParseNodeKind kind, ParseNode* lhs,
                                          ParseNode* rhs);
  [[nodiscard]] bool emitShortCircuitAssignment(AssignmentNode* node);

  [[nodiscard]] bool emitReturn(UnaryNode* returnNode);
  [[nodiscard]] bool emitExpressionStatement(UnaryNode* exprStmt);
  [[nodiscard]] bool emitStatementList(ListNode* stmtList);

  [[nodiscard]] bool emitDeleteName(UnaryNode* deleteNode);
  [[nodiscard]] bool emitDeleteProperty(UnaryNode* deleteNode);
  [[nodiscard]] bool emitDeleteElement(UnaryNode* deleteNode);
  [[nodiscard]] bool emitDeleteExpression(UnaryNode* deleteNode);

  // Optional methods which emit Optional Jump Target
  [[nodiscard]] bool emitOptionalChain(UnaryNode* expr, ValueUsage valueUsage);
  [[nodiscard]] bool emitCalleeAndThisForOptionalChain(UnaryNode* expr,
                                                       CallNode* callNode,
                                                       CallOrNewEmitter& cone);
  [[nodiscard]] bool emitDeleteOptionalChain(UnaryNode* deleteNode);

  // Optional methods which emit a shortCircuit jump. They need to be called by
  // a method which emits an Optional Jump Target, see below.
  [[nodiscard]] bool emitOptionalDotExpression(PropertyAccessBase* expr,
                                               PropOpEmitter& poe, bool isSuper,
                                               OptionalEmitter& oe);
  [[nodiscard]] bool emitOptionalElemExpression(PropertyByValueBase* elem,
                                                ElemOpEmitter& eoe,
                                                bool isSuper,
                                                OptionalEmitter& oe);
  [[nodiscard]] bool emitOptionalPrivateExpression(
      PrivateMemberAccessBase* privateExpr, PrivateOpEmitter& xoe,
      OptionalEmitter& oe);
  [[nodiscard]] bool emitOptionalCall(CallNode* callNode, OptionalEmitter& oe,
                                      ValueUsage valueUsage);
  [[nodiscard]] bool emitDeletePropertyInOptChain(PropertyAccessBase* propExpr,
                                                  OptionalEmitter& oe);
  [[nodiscard]] bool emitDeleteElementInOptChain(PropertyByValueBase* elemExpr,
                                                 OptionalEmitter& oe);

  // |op| must be JSOp::Typeof or JSOp::TypeofExpr.
  [[nodiscard]] bool emitTypeof(UnaryNode* typeofNode, JSOp op);

  [[nodiscard]] bool emitUnary(UnaryNode* unaryNode);
  [[nodiscard]] bool emitRightAssociative(ListNode* node);
  [[nodiscard]] bool emitLeftAssociative(ListNode* node);
  [[nodiscard]] bool emitPrivateInExpr(ListNode* node);
  [[nodiscard]] bool emitShortCircuit(ListNode* node);
  [[nodiscard]] bool emitSequenceExpr(
      ListNode* node, ValueUsage valueUsage = ValueUsage::WantValue);

  [[nodiscard]] MOZ_NEVER_INLINE bool emitIncOrDec(UnaryNode* incDec);

  [[nodiscard]] bool emitConditionalExpression(
      ConditionalExpression& conditional,
      ValueUsage valueUsage = ValueUsage::WantValue);

  bool isOptimizableSpreadArgument(ParseNode* expr);

  [[nodiscard]] ParseNode* getCoordNode(ParseNode* callNode,
                                        ParseNode* calleeNode, JSOp op,
                                        ListNode* argsList);

  [[nodiscard]] bool emitArguments(ListNode* argsList, bool isCall,
                                   bool isSpread, CallOrNewEmitter& cone);
  [[nodiscard]] bool emitCallOrNew(
      CallNode* callNode, ValueUsage valueUsage = ValueUsage::WantValue);
  [[nodiscard]] bool emitSelfHostedCallFunction(CallNode* callNode);
  [[nodiscard]] bool emitSelfHostedResumeGenerator(BinaryNode* callNode);
  [[nodiscard]] bool emitSelfHostedForceInterpreter();
  [[nodiscard]] bool emitSelfHostedAllowContentIter(BinaryNode* callNode);
  [[nodiscard]] bool emitSelfHostedDefineDataProperty(BinaryNode* callNode);
  [[nodiscard]] bool emitSelfHostedGetPropertySuper(BinaryNode* callNode);
  [[nodiscard]] bool emitSelfHostedHasOwn(BinaryNode* callNode);
  [[nodiscard]] bool emitSelfHostedToNumeric(BinaryNode* callNode);
  [[nodiscard]] bool emitSelfHostedToString(BinaryNode* callNode);
  [[nodiscard]] bool emitSelfHostedGetBuiltinConstructor(BinaryNode* callNode);
  [[nodiscard]] bool emitSelfHostedGetBuiltinPrototype(BinaryNode* callNode);
  [[nodiscard]] bool emitSelfHostedGetBuiltinSymbol(BinaryNode* callNode);
  [[nodiscard]] bool emitSelfHostedSetIsInlinableLargeFunction(
      BinaryNode* callNode);
  [[nodiscard]] bool emitSelfHostedSetCanonicalName(BinaryNode* callNode);
#ifdef DEBUG
  [[nodiscard]] bool checkSelfHostedExpectedTopLevel(BinaryNode* callNode,
                                                     ParseNode* node);
  [[nodiscard]] bool checkSelfHostedUnsafeGetReservedSlot(BinaryNode* callNode);
  [[nodiscard]] bool checkSelfHostedUnsafeSetReservedSlot(BinaryNode* callNode);
#endif

  [[nodiscard]] bool emitDo(BinaryNode* doNode);
  [[nodiscard]] bool emitWhile(BinaryNode* whileNode);

  [[nodiscard]] bool emitFor(
      ForNode* forNode, const EmitterScope* headLexicalEmitterScope = nullptr);
  [[nodiscard]] bool emitCStyleFor(ForNode* forNode,
                                   const EmitterScope* headLexicalEmitterScope);
  [[nodiscard]] bool emitForIn(ForNode* forNode,
                               const EmitterScope* headLexicalEmitterScope);
  [[nodiscard]] bool emitForOf(ForNode* forNode,
                               const EmitterScope* headLexicalEmitterScope);

  [[nodiscard]] bool emitInitializeForInOrOfTarget(TernaryNode* forHead);

  [[nodiscard]] bool emitBreak(TaggedParserAtomIndex label);
  [[nodiscard]] bool emitContinue(TaggedParserAtomIndex label);

  [[nodiscard]] bool emitFunctionFormalParameters(ListNode* paramsBody);
  [[nodiscard]] bool emitInitializeFunctionSpecialNames();
  [[nodiscard]] bool emitLexicalInitialization(NameNode* name);
  [[nodiscard]] bool emitLexicalInitialization(TaggedParserAtomIndex name);

  // Emit bytecode for the spread operator.
  //
  // emitSpread expects the current index (I) of the array, the array itself
  // and the iterator to be on the stack in that order (iterator on the bottom).
  // It will pop the iterator and I, then iterate over the iterator by calling
  // |.next()| and put the results into the I-th element of array with
  // incrementing I, then push the result I (it will be original I +
  // iteration count). The stack after iteration will look like |ARRAY INDEX|.
  [[nodiscard]] bool emitSpread(bool allowSelfHosted = false);

  enum class ClassNameKind {
    // The class name is defined through its BindingIdentifier, if present.
    BindingName,

    // The class is anonymous and has a statically inferred name.
    InferredName,

    // The class is anonymous and has a dynamically computed name.
    ComputedName
  };

  [[nodiscard]] bool emitClass(
      ClassNode* classNode, ClassNameKind nameKind = ClassNameKind::BindingName,
      TaggedParserAtomIndex nameForAnonymousClass =
          TaggedParserAtomIndex::null());

  [[nodiscard]] bool emitSuperElemOperands(
      PropertyByValue* elem, EmitElemOption opts = EmitElemOption::Get);
  [[nodiscard]] bool emitSuperGetElem(PropertyByValue* elem,
                                      bool isCall = false);

  [[nodiscard]] bool emitCalleeAndThis(ParseNode* callee, ParseNode* call,
                                       CallOrNewEmitter& cone);

  [[nodiscard]] bool emitOptionalCalleeAndThis(ParseNode* callee,
                                               CallNode* call,
                                               CallOrNewEmitter& cone,
                                               OptionalEmitter& oe);

  [[nodiscard]] bool emitExportDefault(BinaryNode* exportNode);

  [[nodiscard]] bool emitReturnRval() { return emit1(JSOp::RetRval); }

  [[nodiscard]] bool emitCheckPrivateField(ThrowCondition throwCondition,
                                           ThrowMsgKind msgKind) {
    return emit3(JSOp::CheckPrivateField, uint8_t(throwCondition),
                 uint8_t(msgKind));
  }

  [[nodiscard]] bool emitNewPrivateName(TaggedParserAtomIndex bindingName,
                                        TaggedParserAtomIndex symbolName);

  template <class ClassMemberType>
  [[nodiscard]] bool emitNewPrivateNames(ListNode* classMembers);

  [[nodiscard]] bool emitNewPrivateNames(TaggedParserAtomIndex privateBrandName,
                                         ListNode* classMembers);

  [[nodiscard]] js::UniquePtr<ImmutableScriptData> createImmutableScriptData(
      JSContext* cx);

 private:
  [[nodiscard]] bool allowSelfHostedIter(ParseNode* parseNode);

  [[nodiscard]] bool emitSelfHostedGetBuiltinConstructorOrPrototype(
      BinaryNode* callNode, bool isConstructor);

 public:
#if defined(DEBUG) || defined(JS_JITSPEW)
  void dumpAtom(TaggedParserAtomIndex index) const;
#endif
};

class MOZ_RAII AutoCheckUnstableEmitterScope {
#ifdef DEBUG
  bool prev_;
  BytecodeEmitter* bce_;
#endif

 public:
  AutoCheckUnstableEmitterScope() = delete;
  explicit AutoCheckUnstableEmitterScope(BytecodeEmitter* bce)
#ifdef DEBUG
      : bce_(bce)
#endif
  {
#ifdef DEBUG
    prev_ = bce_->unstableEmitterScope;
    bce_->unstableEmitterScope = true;
#endif
  }
  ~AutoCheckUnstableEmitterScope() {
#ifdef DEBUG
    bce_->unstableEmitterScope = prev_;
#endif
  }
};

} /* namespace frontend */
} /* namespace js */

#endif /* frontend_BytecodeEmitter_h */

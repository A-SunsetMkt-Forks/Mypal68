/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/arm64/Lowering-arm64.h"

#include "mozilla/MathAlgorithms.h"

#include "jit/arm64/Assembler-arm64.h"
#include "jit/Lowering.h"
#include "jit/MIR.h"
#include "jit/shared/Lowering-shared-inl.h"

using namespace js;
using namespace js::jit;

using mozilla::FloorLog2;

LBoxAllocation LIRGeneratorARM64::useBoxFixed(MDefinition* mir, Register reg1,
                                              Register, bool useAtStart) {
  MOZ_ASSERT(mir->type() == MIRType::Value);

  ensureDefined(mir);
  return LBoxAllocation(LUse(reg1, mir->virtualRegister(), useAtStart));
}

LAllocation LIRGeneratorARM64::useByteOpRegister(MDefinition* mir) {
  return useRegister(mir);
}

LAllocation LIRGeneratorARM64::useByteOpRegisterAtStart(MDefinition* mir) {
  return useRegisterAtStart(mir);
}

LAllocation LIRGeneratorARM64::useByteOpRegisterOrNonDoubleConstant(
    MDefinition* mir) {
  return useRegisterOrNonDoubleConstant(mir);
}

void LIRGenerator::visitBox(MBox* box) {
  MDefinition* opd = box->getOperand(0);

  // If the operand is a constant, emit near its uses.
  if (opd->isConstant() && box->canEmitAtUses()) {
    emitAtUses(box);
    return;
  }

  if (opd->isConstant()) {
    define(new (alloc()) LValue(opd->toConstant()->toJSValue()), box,
           LDefinition(LDefinition::BOX));
  } else {
    LBox* ins = new (alloc()) LBox(useRegister(opd), opd->type());
    define(ins, box, LDefinition(LDefinition::BOX));
  }
}

void LIRGenerator::visitUnbox(MUnbox* unbox) {
  MDefinition* box = unbox->getOperand(0);
  MOZ_ASSERT(box->type() == MIRType::Value);

  LUnboxBase* lir;
  if (IsFloatingPointType(unbox->type())) {
    lir = new (alloc())
        LUnboxFloatingPoint(useRegisterAtStart(box), unbox->type());
  } else if (unbox->fallible()) {
    // If the unbox is fallible, load the Value in a register first to
    // avoid multiple loads.
    lir = new (alloc()) LUnbox(useRegisterAtStart(box));
  } else {
    // FIXME: It should be possible to useAtStart() here, but the DEBUG
    // code in CodeGenerator::visitUnbox() needs to handle non-Register
    // cases. ARM64 doesn't have an Operand type.
    lir = new (alloc()) LUnbox(useRegisterAtStart(box));
  }

  if (unbox->fallible()) {
    assignSnapshot(lir, unbox->bailoutKind());
  }

  define(lir, unbox);
}

void LIRGenerator::visitReturnImpl(MDefinition* opd, bool isGenerator) {
  MOZ_ASSERT(opd->type() == MIRType::Value);

  LReturn* ins = new (alloc()) LReturn(isGenerator);
  ins->setOperand(0, useFixed(opd, JSReturnReg));
  add(ins);
}

// x = !y
void LIRGeneratorARM64::lowerForALU(LInstructionHelper<1, 1, 0>* ins,
                                    MDefinition* mir, MDefinition* input) {
  ins->setOperand(
      0, ins->snapshot() ? useRegister(input) : useRegisterAtStart(input));
  define(
      ins, mir,
      LDefinition(LDefinition::TypeFrom(mir->type()), LDefinition::REGISTER));
}

// z = x+y
void LIRGeneratorARM64::lowerForALU(LInstructionHelper<1, 2, 0>* ins,
                                    MDefinition* mir, MDefinition* lhs,
                                    MDefinition* rhs) {
  ins->setOperand(0,
                  ins->snapshot() ? useRegister(lhs) : useRegisterAtStart(lhs));
  ins->setOperand(1, ins->snapshot() ? useRegisterOrConstant(rhs)
                                     : useRegisterOrConstantAtStart(rhs));
  define(
      ins, mir,
      LDefinition(LDefinition::TypeFrom(mir->type()), LDefinition::REGISTER));
}

void LIRGeneratorARM64::lowerForFPU(LInstructionHelper<1, 1, 0>* ins,
                                    MDefinition* mir, MDefinition* input) {
  ins->setOperand(0, useRegisterAtStart(input));
  define(
      ins, mir,
      LDefinition(LDefinition::TypeFrom(mir->type()), LDefinition::REGISTER));
}

template <size_t Temps>
void LIRGeneratorARM64::lowerForFPU(LInstructionHelper<1, 2, Temps>* ins,
                                    MDefinition* mir, MDefinition* lhs,
                                    MDefinition* rhs) {
  ins->setOperand(0, useRegisterAtStart(lhs));
  ins->setOperand(1, useRegisterAtStart(rhs));
  define(
      ins, mir,
      LDefinition(LDefinition::TypeFrom(mir->type()), LDefinition::REGISTER));
}

template void LIRGeneratorARM64::lowerForFPU(LInstructionHelper<1, 2, 0>* ins,
                                             MDefinition* mir, MDefinition* lhs,
                                             MDefinition* rhs);
template void LIRGeneratorARM64::lowerForFPU(LInstructionHelper<1, 2, 1>* ins,
                                             MDefinition* mir, MDefinition* lhs,
                                             MDefinition* rhs);

// These all currently have codegen that depends on reuse but only because the
// masm API depends on that.  We need new three-address masm APIs, for both
// constant and variable rhs.
//
// MAdd => LAddI64
// MSub => LSubI64
// MBitAnd, MBitOr, MBitXor => LBitOpI64
void LIRGeneratorARM64::lowerForALUInt64(
    LInstructionHelper<INT64_PIECES, 2 * INT64_PIECES, 0>* ins,
    MDefinition* mir, MDefinition* lhs, MDefinition* rhs) {
  ins->setInt64Operand(0, useInt64RegisterAtStart(lhs));
  ins->setInt64Operand(INT64_PIECES, useInt64RegisterOrConstantAtStart(rhs));
  defineInt64(ins, mir);
}

void LIRGeneratorARM64::lowerForMulInt64(LMulI64* ins, MMul* mir,
                                         MDefinition* lhs, MDefinition* rhs) {
  ins->setInt64Operand(LMulI64::Lhs, useInt64RegisterAtStart(lhs));
  ins->setInt64Operand(LMulI64::Rhs, useInt64RegisterOrConstantAtStart(rhs));
  defineInt64(ins, mir);
}

template <size_t Temps>
void LIRGeneratorARM64::lowerForShiftInt64(
    LInstructionHelper<INT64_PIECES, INT64_PIECES + 1, Temps>* ins,
    MDefinition* mir, MDefinition* lhs, MDefinition* rhs) {
  ins->setInt64Operand(0, useInt64RegisterAtStart(lhs));

  static_assert(LShiftI64::Rhs == INT64_PIECES,
                "Assume Rhs is located at INT64_PIECES.");
  static_assert(LRotateI64::Count == INT64_PIECES,
                "Assume Count is located at INT64_PIECES.");

  ins->setOperand(INT64_PIECES, useRegisterOrConstantAtStart(rhs));
  defineInt64(ins, mir);
}

template void LIRGeneratorARM64::lowerForShiftInt64(
    LInstructionHelper<INT64_PIECES, INT64_PIECES + 1, 0>* ins,
    MDefinition* mir, MDefinition* lhs, MDefinition* rhs);
template void LIRGeneratorARM64::lowerForShiftInt64(
    LInstructionHelper<INT64_PIECES, INT64_PIECES + 1, 1>* ins,
    MDefinition* mir, MDefinition* lhs, MDefinition* rhs);

void LIRGeneratorARM64::lowerForCompareI64AndBranch(MTest* mir, MCompare* comp,
                                                    JSOp op, MDefinition* left,
                                                    MDefinition* right,
                                                    MBasicBlock* ifTrue,
                                                    MBasicBlock* ifFalse) {
  auto* lir = new (alloc())
      LCompareI64AndBranch(comp, op, useInt64Register(left),
                           useInt64RegisterOrConstant(right), ifTrue, ifFalse);
  add(lir, mir);
}

void LIRGeneratorARM64::lowerForBitAndAndBranch(LBitAndAndBranch* baab,
                                                MInstruction* mir,
                                                MDefinition* lhs,
                                                MDefinition* rhs) {
  baab->setOperand(0, useRegisterAtStart(lhs));
  baab->setOperand(1, useRegisterOrConstantAtStart(rhs));
  add(baab, mir);
}

void LIRGeneratorARM64::lowerWasmBuiltinTruncateToInt32(
    MWasmBuiltinTruncateToInt32* ins) {
  MDefinition* opd = ins->input();
  MOZ_ASSERT(opd->type() == MIRType::Double || opd->type() == MIRType::Float32);

  if (opd->type() == MIRType::Double) {
    define(new (alloc()) LWasmBuiltinTruncateDToInt32(
               useRegister(opd), useFixed(ins->tls(), WasmTlsReg),
               LDefinition::BogusTemp()),
           ins);
    return;
  }

  define(new (alloc()) LWasmBuiltinTruncateFToInt32(
             useRegister(opd), useFixed(ins->tls(), WasmTlsReg),
             LDefinition::BogusTemp()),
         ins);
}

void LIRGeneratorARM64::lowerUntypedPhiInput(MPhi* phi, uint32_t inputPosition,
                                             LBlock* block, size_t lirIndex) {
  lowerTypedPhiInput(phi, inputPosition, block, lirIndex);
}

void LIRGeneratorARM64::lowerForShift(LInstructionHelper<1, 2, 0>* ins,
                                      MDefinition* mir, MDefinition* lhs,
                                      MDefinition* rhs) {
  ins->setOperand(0, useRegister(lhs));
  ins->setOperand(1, useRegisterOrConstant(rhs));
  define(ins, mir);
}

void LIRGeneratorARM64::lowerDivI(MDiv* div) {
  if (div->isUnsigned()) {
    lowerUDiv(div);
    return;
  }

  if (div->rhs()->isConstant()) {
    LAllocation lhs = useRegister(div->lhs());
    int32_t rhs = div->rhs()->toConstant()->toInt32();
    int32_t shift = mozilla::FloorLog2(mozilla::Abs(rhs));

    if (rhs != 0 && uint32_t(1) << shift == mozilla::Abs(rhs)) {
      LDivPowTwoI* lir = new (alloc()) LDivPowTwoI(lhs, shift, rhs < 0);
      if (div->fallible()) {
        assignSnapshot(lir, div->bailoutKind());
      }
      define(lir, div);
      return;
    }
    if (rhs != 0) {
      LDivConstantI* lir = new (alloc()) LDivConstantI(lhs, rhs, temp());
      if (div->fallible()) {
        assignSnapshot(lir, div->bailoutKind());
      }
      define(lir, div);
      return;
    }
  }

  LDivI* lir = new (alloc())
      LDivI(useRegister(div->lhs()), useRegister(div->rhs()), temp());
  if (div->fallible()) {
    assignSnapshot(lir, div->bailoutKind());
  }
  define(lir, div);
}

void LIRGeneratorARM64::lowerNegI(MInstruction* ins, MDefinition* input,
                                  int32_t inputNo) {
  define(new (alloc()) LNegI(useRegisterAtStart(input)), ins);
}

void LIRGeneratorARM64::lowerMulI(MMul* mul, MDefinition* lhs,
                                  MDefinition* rhs) {
  LMulI* lir = new (alloc()) LMulI;
  if (mul->fallible()) {
    assignSnapshot(lir, mul->bailoutKind());
  }
  lowerForALU(lir, mul, lhs, rhs);
}

void LIRGeneratorARM64::lowerModI(MMod* mod) {
  if (mod->isUnsigned()) {
    lowerUMod(mod);
    return;
  }

  if (mod->rhs()->isConstant()) {
    int32_t rhs = mod->rhs()->toConstant()->toInt32();
    int32_t shift = FloorLog2(rhs);
    if (rhs > 0 && 1 << shift == rhs) {
      LModPowTwoI* lir =
          new (alloc()) LModPowTwoI(useRegister(mod->lhs()), shift);
      if (mod->fallible()) {
        assignSnapshot(lir, mod->bailoutKind());
      }
      define(lir, mod);
      return;
    } else if (shift < 31 && (1 << (shift + 1)) - 1 == rhs) {
      LModMaskI* lir = new (alloc())
          LModMaskI(useRegister(mod->lhs()), temp(), temp(), shift + 1);
      if (mod->fallible()) {
        assignSnapshot(lir, mod->bailoutKind());
      }
      define(lir, mod);
    }
  }

  LModI* lir =
      new (alloc()) LModI(useRegister(mod->lhs()), useRegister(mod->rhs()));
  if (mod->fallible()) {
    assignSnapshot(lir, mod->bailoutKind());
  }
  define(lir, mod);
}

void LIRGeneratorARM64::lowerDivI64(MDiv* div) {
  if (div->isUnsigned()) {
    lowerUDivI64(div);
    return;
  }

  LDivOrModI64* lir = new (alloc())
      LDivOrModI64(useRegister(div->lhs()), useRegister(div->rhs()));
  defineInt64(lir, div);
}

void LIRGeneratorARM64::lowerUDivI64(MDiv* div) {
  LUDivOrModI64* lir = new (alloc())
      LUDivOrModI64(useRegister(div->lhs()), useRegister(div->rhs()));
  defineInt64(lir, div);
}

void LIRGeneratorARM64::lowerUModI64(MMod* mod) {
  LUDivOrModI64* lir = new (alloc())
      LUDivOrModI64(useRegister(mod->lhs()), useRegister(mod->rhs()));
  defineInt64(lir, mod);
}

void LIRGeneratorARM64::lowerWasmBuiltinDivI64(MWasmBuiltinDivI64* div) {
  MOZ_CRASH("We don't use runtime div for this architecture");
}

void LIRGeneratorARM64::lowerModI64(MMod* mod) {
  if (mod->isUnsigned()) {
    lowerUModI64(mod);
    return;
  }

  LDivOrModI64* lir = new (alloc())
      LDivOrModI64(useRegister(mod->lhs()), useRegister(mod->rhs()));
  defineInt64(lir, mod);
}

void LIRGeneratorARM64::lowerWasmBuiltinModI64(MWasmBuiltinModI64* mod) {
  MOZ_CRASH("We don't use runtime mod for this architecture");
}

void LIRGenerator::visitPowHalf(MPowHalf* ins) {
  MDefinition* input = ins->input();
  MOZ_ASSERT(input->type() == MIRType::Double);
  LPowHalfD* lir = new (alloc()) LPowHalfD(useRegister(input));
  define(lir, ins);
}

LTableSwitch* LIRGeneratorARM64::newLTableSwitch(const LAllocation& in,
                                                 const LDefinition& inputCopy,
                                                 MTableSwitch* tableswitch) {
  return new (alloc()) LTableSwitch(in, inputCopy, temp(), tableswitch);
}

LTableSwitchV* LIRGeneratorARM64::newLTableSwitchV(MTableSwitch* tableswitch) {
  return new (alloc()) LTableSwitchV(useBox(tableswitch->getOperand(0)), temp(),
                                     tempDouble(), temp(), tableswitch);
}

void LIRGeneratorARM64::lowerUrshD(MUrsh* mir) {
  MDefinition* lhs = mir->lhs();
  MDefinition* rhs = mir->rhs();

  MOZ_ASSERT(lhs->type() == MIRType::Int32);
  MOZ_ASSERT(rhs->type() == MIRType::Int32);

  LUrshD* lir = new (alloc())
      LUrshD(useRegister(lhs), useRegisterOrConstant(rhs), temp());
  define(lir, mir);
}

void LIRGeneratorARM64::lowerPowOfTwoI(MPow* mir) {
  int32_t base = mir->input()->toConstant()->toInt32();
  MDefinition* power = mir->power();

  auto* lir = new (alloc()) LPowOfTwoI(base, useRegister(power));
  assignSnapshot(lir, mir->bailoutKind());
  define(lir, mir);
}

void LIRGeneratorARM64::lowerBigIntLsh(MBigIntLsh* ins) {
  auto* lir = new (alloc()) LBigIntLsh(
      useRegister(ins->lhs()), useRegister(ins->rhs()), temp(), temp(), temp());
  define(lir, ins);
  assignSafepoint(lir, ins);
}

void LIRGeneratorARM64::lowerBigIntRsh(MBigIntRsh* ins) {
  auto* lir = new (alloc()) LBigIntRsh(
      useRegister(ins->lhs()), useRegister(ins->rhs()), temp(), temp(), temp());
  define(lir, ins);
  assignSafepoint(lir, ins);
}

void LIRGeneratorARM64::lowerBigIntDiv(MBigIntDiv* ins) {
  auto* lir = new (alloc()) LBigIntDiv(useRegister(ins->lhs()),
                                       useRegister(ins->rhs()), temp(), temp());
  define(lir, ins);
  assignSafepoint(lir, ins);
}

void LIRGeneratorARM64::lowerBigIntMod(MBigIntMod* ins) {
  auto* lir = new (alloc()) LBigIntMod(useRegister(ins->lhs()),
                                       useRegister(ins->rhs()), temp(), temp());
  define(lir, ins);
  assignSafepoint(lir, ins);
}

void LIRGenerator::visitWasmNeg(MWasmNeg* ins) {
  switch (ins->type()) {
    case MIRType::Int32:
      define(new (alloc()) LNegI(useRegisterAtStart(ins->input())), ins);
      break;
    case MIRType::Float32:
      define(new (alloc()) LNegF(useRegisterAtStart(ins->input())), ins);
      break;
    case MIRType::Double:
      define(new (alloc()) LNegD(useRegisterAtStart(ins->input())), ins);
      break;
    default:
      MOZ_CRASH("unexpected type");
  }
}

void LIRGeneratorARM64::lowerUDiv(MDiv* div) {
  LAllocation lhs = useRegister(div->lhs());
  if (div->rhs()->isConstant()) {
    int32_t rhs = div->rhs()->toConstant()->toInt32();
    int32_t shift = mozilla::FloorLog2(mozilla::Abs(rhs));

    if (rhs != 0 && uint32_t(1) << shift == mozilla::Abs(rhs)) {
      LDivPowTwoI* lir = new (alloc()) LDivPowTwoI(lhs, shift, false);
      if (div->fallible()) {
        assignSnapshot(lir, div->bailoutKind());
      }
      define(lir, div);
      return;
    }

    LUDivConstantI* lir = new (alloc()) LUDivConstantI(lhs, rhs, temp());
    if (div->fallible()) {
      assignSnapshot(lir, div->bailoutKind());
    }
    define(lir, div);
    return;
  }

  // Generate UDiv
  LAllocation rhs = useRegister(div->rhs());
  LDefinition remainder = LDefinition::BogusTemp();
  if (!div->canTruncateRemainder()) {
    remainder = temp();
  }

  LUDiv* lir = new (alloc()) LUDiv(lhs, rhs, remainder);
  if (div->fallible()) {
    assignSnapshot(lir, div->bailoutKind());
  }
  define(lir, div);
}

void LIRGeneratorARM64::lowerUMod(MMod* mod) {
  LUMod* lir = new (alloc())
      LUMod(useRegister(mod->getOperand(0)), useRegister(mod->getOperand(1)));
  if (mod->fallible()) {
    assignSnapshot(lir, mod->bailoutKind());
  }
  define(lir, mod);
}

void LIRGenerator::visitWasmUnsignedToDouble(MWasmUnsignedToDouble* ins) {
  MOZ_ASSERT(ins->input()->type() == MIRType::Int32);
  LWasmUint32ToDouble* lir =
      new (alloc()) LWasmUint32ToDouble(useRegisterAtStart(ins->input()));
  define(lir, ins);
}

void LIRGenerator::visitWasmUnsignedToFloat32(MWasmUnsignedToFloat32* ins) {
  MOZ_ASSERT(ins->input()->type() == MIRType::Int32);
  LWasmUint32ToFloat32* lir =
      new (alloc()) LWasmUint32ToFloat32(useRegisterAtStart(ins->input()));
  define(lir, ins);
}

void LIRGenerator::visitAsmJSLoadHeap(MAsmJSLoadHeap* ins) {
  MDefinition* base = ins->base();
  MOZ_ASSERT(base->type() == MIRType::Int32);

  MDefinition* boundsCheckLimit = ins->boundsCheckLimit();
  MOZ_ASSERT_IF(ins->needsBoundsCheck(),
                boundsCheckLimit->type() == MIRType::Int32);

  LAllocation baseAlloc = useRegisterAtStart(base);

  LAllocation limitAlloc = ins->needsBoundsCheck()
                               ? useRegisterAtStart(boundsCheckLimit)
                               : LAllocation();

  // We have no memory-base value, meaning that HeapReg is to be used as the
  // memory base.  This follows from the definition of
  // FunctionCompiler::maybeLoadMemoryBase() in WasmIonCompile.cpp.
  MOZ_ASSERT(!ins->hasMemoryBase());
  auto* lir = new (alloc()) LAsmJSLoadHeap(baseAlloc, limitAlloc);
  define(lir, ins);
}

void LIRGenerator::visitAsmJSStoreHeap(MAsmJSStoreHeap* ins) {
  MDefinition* base = ins->base();
  MOZ_ASSERT(base->type() == MIRType::Int32);

  MDefinition* boundsCheckLimit = ins->boundsCheckLimit();
  MOZ_ASSERT_IF(ins->needsBoundsCheck(),
                boundsCheckLimit->type() == MIRType::Int32);

  LAllocation baseAlloc = useRegisterAtStart(base);

  LAllocation limitAlloc = ins->needsBoundsCheck()
                               ? useRegisterAtStart(boundsCheckLimit)
                               : LAllocation();

  // See comment in LIRGenerator::visitAsmJSStoreHeap just above.
  MOZ_ASSERT(!ins->hasMemoryBase());
  add(new (alloc()) LAsmJSStoreHeap(baseAlloc, useRegisterAtStart(ins->value()),
                                    limitAlloc),
      ins);
}

void LIRGenerator::visitWasmCompareExchangeHeap(MWasmCompareExchangeHeap* ins) {
  MDefinition* base = ins->base();
  MOZ_ASSERT(base->type() == MIRType::Int32);

  // Note, the access type may be Int64 here.

  LWasmCompareExchangeHeap* lir = new (alloc())
      LWasmCompareExchangeHeap(useRegister(base), useRegister(ins->oldValue()),
                               useRegister(ins->newValue()));

  define(lir, ins);
}

void LIRGenerator::visitWasmAtomicExchangeHeap(MWasmAtomicExchangeHeap* ins) {
  MDefinition* base = ins->base();
  MOZ_ASSERT(base->type() == MIRType::Int32);

  // Note, the access type may be Int64 here.

  LWasmAtomicExchangeHeap* lir = new (alloc())
      LWasmAtomicExchangeHeap(useRegister(base), useRegister(ins->value()));
  define(lir, ins);
}

void LIRGenerator::visitWasmAtomicBinopHeap(MWasmAtomicBinopHeap* ins) {
  MDefinition* base = ins->base();
  MOZ_ASSERT(base->type() == MIRType::Int32);

  // Note, the access type may be Int64 here.

  if (!ins->hasUses()) {
    LWasmAtomicBinopHeapForEffect* lir =
        new (alloc()) LWasmAtomicBinopHeapForEffect(useRegister(base),
                                                    useRegister(ins->value()),
                                                    /* flagTemp= */ temp());
    add(lir, ins);
    return;
  }

  LWasmAtomicBinopHeap* lir = new (alloc())
      LWasmAtomicBinopHeap(useRegister(base), useRegister(ins->value()),
                           /* temp= */ LDefinition::BogusTemp(),
                           /* flagTemp= */ temp());
  define(lir, ins);
}

void LIRGeneratorARM64::lowerTruncateDToInt32(MTruncateToInt32* ins) {
  MDefinition* opd = ins->input();
  MOZ_ASSERT(opd->type() == MIRType::Double);
  define(new (alloc())
             LTruncateDToInt32(useRegister(opd), LDefinition::BogusTemp()),
         ins);
}

void LIRGeneratorARM64::lowerTruncateFToInt32(MTruncateToInt32* ins) {
  MDefinition* opd = ins->input();
  MOZ_ASSERT(opd->type() == MIRType::Float32);
  define(new (alloc())
             LTruncateFToInt32(useRegister(opd), LDefinition::BogusTemp()),
         ins);
}

void LIRGenerator::visitAtomicTypedArrayElementBinop(
    MAtomicTypedArrayElementBinop* ins) {
  MOZ_ASSERT(ins->arrayType() != Scalar::Uint8Clamped);
  MOZ_ASSERT(ins->arrayType() != Scalar::Float32);
  MOZ_ASSERT(ins->arrayType() != Scalar::Float64);

  MOZ_ASSERT(ins->elements()->type() == MIRType::Elements);
  MOZ_ASSERT(ins->index()->type() == MIRType::IntPtr);

  const LUse elements = useRegister(ins->elements());
  const LAllocation index =
      useRegisterOrIndexConstant(ins->index(), ins->arrayType());

  LAllocation value = useRegister(ins->value());

  if (Scalar::isBigIntType(ins->arrayType())) {
    LInt64Definition temp1 = tempInt64();
    LInt64Definition temp2 = tempInt64();

    // Case 1: the result of the operation is not used.
    //
    // We can omit allocating the result BigInt.

    if (ins->isForEffect()) {
      auto* lir = new (alloc()) LAtomicTypedArrayElementBinopForEffect64(
          elements, index, value, temp1, temp2);
      add(lir, ins);
      return;
    }

    // Case 2: the result of the operation is used.

    auto* lir = new (alloc())
        LAtomicTypedArrayElementBinop64(elements, index, value, temp1, temp2);
    define(lir, ins);
    assignSafepoint(lir, ins);
    return;
  }

  if (ins->isForEffect()) {
    auto* lir = new (alloc())
        LAtomicTypedArrayElementBinopForEffect(elements, index, value, temp());
    add(lir, ins);
    return;
  }

  LDefinition tempDef1 = temp();
  LDefinition tempDef2 = LDefinition::BogusTemp();
  if (ins->arrayType() == Scalar::Uint32) {
    tempDef2 = temp();
  }

  LAtomicTypedArrayElementBinop* lir = new (alloc())
      LAtomicTypedArrayElementBinop(elements, index, value, tempDef1, tempDef2);

  define(lir, ins);
}

void LIRGenerator::visitCompareExchangeTypedArrayElement(
    MCompareExchangeTypedArrayElement* ins) {
  MOZ_ASSERT(ins->arrayType() != Scalar::Float32);
  MOZ_ASSERT(ins->arrayType() != Scalar::Float64);

  MOZ_ASSERT(ins->elements()->type() == MIRType::Elements);
  MOZ_ASSERT(ins->index()->type() == MIRType::IntPtr);

  const LUse elements = useRegister(ins->elements());
  const LAllocation index =
      useRegisterOrIndexConstant(ins->index(), ins->arrayType());

  const LAllocation newval = useRegister(ins->newval());
  const LAllocation oldval = useRegister(ins->oldval());

  if (Scalar::isBigIntType(ins->arrayType())) {
    LInt64Definition temp1 = tempInt64();
    LInt64Definition temp2 = tempInt64();

    auto* lir = new (alloc()) LCompareExchangeTypedArrayElement64(
        elements, index, oldval, newval, temp1, temp2);
    define(lir, ins);
    assignSafepoint(lir, ins);
    return;
  }

  // If the target is an FPReg then we need a temporary at the CodeGenerator
  // level for creating the result.

  LDefinition outTemp = LDefinition::BogusTemp();
  if (ins->arrayType() == Scalar::Uint32) {
    outTemp = temp();
  }

  LCompareExchangeTypedArrayElement* lir =
      new (alloc()) LCompareExchangeTypedArrayElement(elements, index, oldval,
                                                      newval, outTemp);

  define(lir, ins);
}

void LIRGenerator::visitAtomicExchangeTypedArrayElement(
    MAtomicExchangeTypedArrayElement* ins) {
  MOZ_ASSERT(ins->elements()->type() == MIRType::Elements);
  MOZ_ASSERT(ins->index()->type() == MIRType::IntPtr);

  const LUse elements = useRegister(ins->elements());
  const LAllocation index =
      useRegisterOrIndexConstant(ins->index(), ins->arrayType());

  const LAllocation value = useRegister(ins->value());

  if (Scalar::isBigIntType(ins->arrayType())) {
    LInt64Definition temp1 = tempInt64();
    LDefinition temp2 = temp();

    auto* lir = new (alloc()) LAtomicExchangeTypedArrayElement64(
        elements, index, value, temp1, temp2);
    define(lir, ins);
    assignSafepoint(lir, ins);
    return;
  }

  MOZ_ASSERT(ins->arrayType() <= Scalar::Uint32);

  LDefinition tempDef = LDefinition::BogusTemp();
  if (ins->arrayType() == Scalar::Uint32) {
    tempDef = temp();
  }

  LAtomicExchangeTypedArrayElement* lir = new (alloc())
      LAtomicExchangeTypedArrayElement(elements, index, value, tempDef);

  define(lir, ins);
}

void LIRGeneratorARM64::lowerAtomicLoad64(MLoadUnboxedScalar* ins) {
  const LUse elements = useRegister(ins->elements());
  const LAllocation index =
      useRegisterOrIndexConstant(ins->index(), ins->storageType());

  auto* lir = new (alloc()) LAtomicLoad64(elements, index, temp(), tempInt64());
  define(lir, ins);
  assignSafepoint(lir, ins);
}

void LIRGeneratorARM64::lowerAtomicStore64(MStoreUnboxedScalar* ins) {
  LUse elements = useRegister(ins->elements());
  LAllocation index =
      useRegisterOrIndexConstant(ins->index(), ins->writeType());
  LAllocation value = useRegister(ins->value());

  add(new (alloc()) LAtomicStore64(elements, index, value, tempInt64()), ins);
}

void LIRGenerator::visitSubstr(MSubstr* ins) {
  LSubstr* lir = new (alloc())
      LSubstr(useRegister(ins->string()), useRegister(ins->begin()),
              useRegister(ins->length()), temp(), temp(), temp());
  define(lir, ins);
  assignSafepoint(lir, ins);
}

void LIRGenerator::visitWasmTruncateToInt64(MWasmTruncateToInt64* ins) {
  MDefinition* opd = ins->input();
  MOZ_ASSERT(opd->type() == MIRType::Double || opd->type() == MIRType::Float32);

  defineInt64(new (alloc()) LWasmTruncateToInt64(useRegister(opd)), ins);
}

void LIRGeneratorARM64::lowerWasmBuiltinTruncateToInt64(
    MWasmBuiltinTruncateToInt64* ins) {
  MOZ_CRASH("We don't use WasmBuiltinTruncateToInt64 for arm64");
}

void LIRGeneratorARM64::lowerBuiltinInt64ToFloatingPoint(
    MBuiltinInt64ToFloatingPoint* ins) {
  MOZ_CRASH("We don't use it for this architecture");
}

void LIRGenerator::visitWasmHeapBase(MWasmHeapBase* ins) {
  auto* lir = new (alloc()) LWasmHeapBase(LAllocation());
  define(lir, ins);
}

void LIRGenerator::visitWasmLoad(MWasmLoad* ins) {
  MDefinition* base = ins->base();
  MOZ_ASSERT(base->type() == MIRType::Int32);

  LAllocation ptr = useRegisterAtStart(base);

  if (ins->type() == MIRType::Int64) {
    auto* lir = new (alloc()) LWasmLoadI64(ptr);
    defineInt64(lir, ins);
  } else {
    auto* lir = new (alloc()) LWasmLoad(ptr);
    define(lir, ins);
  }
}

void LIRGenerator::visitWasmStore(MWasmStore* ins) {
  MDefinition* base = ins->base();
  MOZ_ASSERT(base->type() == MIRType::Int32);

  MDefinition* value = ins->value();

  if (ins->access().type() == Scalar::Int64) {
    LAllocation baseAlloc = useRegisterAtStart(base);
    LInt64Allocation valueAlloc = useInt64RegisterAtStart(value);
    auto* lir = new (alloc()) LWasmStoreI64(baseAlloc, valueAlloc);
    add(lir, ins);
    return;
  }

  LAllocation baseAlloc = useRegisterAtStart(base);
  LAllocation valueAlloc = useRegisterAtStart(value);
  auto* lir = new (alloc()) LWasmStore(baseAlloc, valueAlloc);
  add(lir, ins);
}

void LIRGenerator::visitInt64ToFloatingPoint(MInt64ToFloatingPoint* ins) {
  MDefinition* opd = ins->input();
  MOZ_ASSERT(opd->type() == MIRType::Int64);
  MOZ_ASSERT(IsFloatingPointType(ins->type()));

  define(new (alloc()) LInt64ToFloatingPoint(useInt64Register(opd)), ins);
}

void LIRGenerator::visitCopySign(MCopySign* ins) {
  MDefinition* lhs = ins->lhs();
  MDefinition* rhs = ins->rhs();

  MOZ_ASSERT(IsFloatingPointType(lhs->type()));
  MOZ_ASSERT(lhs->type() == rhs->type());
  MOZ_ASSERT(lhs->type() == ins->type());

  LInstructionHelper<1, 2, 2>* lir;
  if (lhs->type() == MIRType::Double) {
    lir = new (alloc()) LCopySignD();
  } else {
    lir = new (alloc()) LCopySignF();
  }

  // TODO: Are these really what we want?  Inherited from x64 / mips.
  lir->setOperand(0, useRegisterAtStart(lhs));
  lir->setOperand(1, useRegister(rhs));
  defineReuseInput(lir, ins, 0);
}

void LIRGenerator::visitExtendInt32ToInt64(MExtendInt32ToInt64* ins) {
  defineInt64(
      new (alloc()) LExtendInt32ToInt64(useRegisterAtStart(ins->input())), ins);
}

void LIRGenerator::visitSignExtendInt64(MSignExtendInt64* ins) {
  defineInt64(new (alloc())
                  LSignExtendInt64(useInt64RegisterAtStart(ins->input())),
              ins);
}

#ifdef ENABLE_WASM_SIMD
void LIRGenerator::visitWasmBitselectSimd128(MWasmBitselectSimd128* ins) {
  MOZ_CRASH("bitselect NYI");
}

void LIRGenerator::visitWasmBinarySimd128(MWasmBinarySimd128* ins) {
  MOZ_CRASH("binary SIMD NYI");
}

bool MWasmBinarySimd128::specializeForConstantRhs() {
  // Probably many we want to do here
  return false;
}

void LIRGenerator::visitWasmBinarySimd128WithConstant(
    MWasmBinarySimd128WithConstant* ins) {
  MOZ_CRASH("binary SIMD with constant NYI");
}

void LIRGenerator::visitWasmShiftSimd128(MWasmShiftSimd128* ins) {
  MOZ_CRASH("shift SIMD NYI");
}

void LIRGenerator::visitWasmShuffleSimd128(MWasmShuffleSimd128* ins) {
  MOZ_CRASH("shuffle SIMD NYI");
}

void LIRGenerator::visitWasmReplaceLaneSimd128(MWasmReplaceLaneSimd128* ins) {
  MOZ_CRASH("replace-lane SIMD NYI");
}

void LIRGenerator::visitWasmScalarToSimd128(MWasmScalarToSimd128* ins) {
  MOZ_CRASH("scalar-to-SIMD NYI");
}

void LIRGenerator::visitWasmUnarySimd128(MWasmUnarySimd128* ins) {
  MOZ_CRASH("unary SIMD NYI");
}

void LIRGenerator::visitWasmReduceSimd128(MWasmReduceSimd128* ins) {
  MOZ_CRASH("reduce-SIMD NYI");
}
#endif

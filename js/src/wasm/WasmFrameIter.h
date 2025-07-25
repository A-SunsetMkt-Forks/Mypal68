/* Copyright 2014 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef wasm_frame_iter_h
#define wasm_frame_iter_h

#include "js/ProfilingFrameIterator.h"
#include "js/TypeDecls.h"
#include "wasm/WasmCode.h"
#include "wasm/WasmCodegenTypes.h"
#include "wasm/WasmFrame.h"
#include "wasm/WasmTlsData.h"

namespace js {

namespace jit {
class MacroAssembler;
struct Register;
class Label;
enum class FrameType;
}  // namespace jit

namespace wasm {

class Code;
class CodeRange;
class DebugFrame;
struct TlsData;
class TypeIdDesc;
class Instance;
class ModuleSegment;

struct CallableOffsets;
struct FuncOffsets;
class Frame;

using RegisterState = JS::ProfilingFrameIterator::RegisterState;

// Iterates over a linear group of wasm frames of a single wasm JitActivation,
// called synchronously from C++ in the wasm thread. It will stop at the first
// frame that is not of the same kind, or at the end of an activation.
//
// If you want to handle every kind of frames (including JS jit frames), use
// JitFrameIter.

class WasmFrameIter {
 public:
  enum class Unwind { True, False };
  static constexpr uint32_t ColumnBit = 1u << 31;

 private:
  jit::JitActivation* activation_;
  const Code* code_;
  const CodeRange* codeRange_;
  unsigned lineOrBytecode_;
  Frame* fp_;
  const TlsData* tls_;
  uint8_t* unwoundIonCallerFP_;
  jit::FrameType unwoundIonFrameType_;
  Unwind unwind_;
  void** unwoundAddressOfReturnAddress_;
  uint8_t* resumePCinCurrentFrame_;

  void popFrame();

 public:
  // See comment above this class definition.
  explicit WasmFrameIter(jit::JitActivation* activation, Frame* fp = nullptr);
  const jit::JitActivation* activation() const { return activation_; }
  void setUnwind(Unwind unwind) { unwind_ = unwind; }
  void operator++();
  bool done() const;
  const char* filename() const;
  const char16_t* displayURL() const;
  bool mutedErrors() const;
  JSAtom* functionDisplayAtom() const;
  unsigned lineOrBytecode() const;
  uint32_t funcIndex() const;
  unsigned computeLine(uint32_t* column) const;
  const CodeRange* codeRange() const { return codeRange_; }
  Instance* instance() const;
  void** unwoundAddressOfReturnAddress() const;
  bool debugEnabled() const;
  DebugFrame* debugFrame() const;
  jit::FrameType unwoundIonFrameType() const;
  uint8_t* unwoundIonCallerFP() const { return unwoundIonCallerFP_; }
  Frame* frame() const { return fp_; }
  const TlsData* tls() const { return tls_; }

  // Returns the address of the next instruction that will execute in this
  // frame, once control returns to this frame.
  uint8_t* resumePCinCurrentFrame() const;
};

enum class SymbolicAddress;

// An ExitReason describes the possible reasons for leaving compiled wasm
// code or the state of not having left compiled wasm code
// (ExitReason::None). It is either a known reason, or a enumeration to a native
// function that is used for better display in the profiler.
class ExitReason {
 public:
  enum class Fixed : uint32_t {
    None,             // default state, the pc is in wasm code
    FakeInterpEntry,  // slow-path entry call from C++ WasmCall()
    ImportJit,        // fast-path call directly into JIT code
    ImportInterp,     // slow-path call into C++ Invoke()
    BuiltinNative,    // fast-path call directly into native C++ code
    Trap,             // call to trap handler
    DebugTrap         // call to debug trap handler
  };

 private:
  uint32_t payload_;

  ExitReason() : ExitReason(Fixed::None) {}

 public:
  MOZ_IMPLICIT ExitReason(Fixed exitReason)
      : payload_(0x0 | (uint32_t(exitReason) << 1)) {
    MOZ_ASSERT(isFixed());
    MOZ_ASSERT_IF(isNone(), payload_ == 0);
  }

  explicit ExitReason(SymbolicAddress sym)
      : payload_(0x1 | (uint32_t(sym) << 1)) {
    MOZ_ASSERT(uint32_t(sym) <= (UINT32_MAX << 1), "packing constraints");
    MOZ_ASSERT(!isFixed());
  }

  static ExitReason Decode(uint32_t payload) {
    ExitReason reason;
    reason.payload_ = payload;
    return reason;
  }

  static ExitReason None() { return ExitReason(ExitReason::Fixed::None); }

  bool isFixed() const { return (payload_ & 0x1) == 0; }
  bool isNone() const { return isFixed() && fixed() == Fixed::None; }
  bool isNative() const {
    return !isFixed() || fixed() == Fixed::BuiltinNative;
  }
  bool isInterpEntry() const {
    return isFixed() && fixed() == Fixed::FakeInterpEntry;
  }

  uint32_t encode() const { return payload_; }
  Fixed fixed() const {
    MOZ_ASSERT(isFixed());
    return Fixed(payload_ >> 1);
  }
  SymbolicAddress symbolic() const {
    MOZ_ASSERT(!isFixed());
    return SymbolicAddress(payload_ >> 1);
  }
};

// Iterates over the frames of a single wasm JitActivation, given an
// asynchronously-profiled thread's state.
class ProfilingFrameIterator {
  const Code* code_;
  const CodeRange* codeRange_;
  uint8_t* callerFP_;
  void* callerPC_;
  void* stackAddress_;
  uint8_t* unwoundIonCallerFP_;
  ExitReason exitReason_;

  void initFromExitFP(const Frame* fp);

 public:
  ProfilingFrameIterator();

  // Start unwinding at a non-innermost activation that has necessarily been
  // exited from wasm code (and thus activation.hasWasmExitFP).
  explicit ProfilingFrameIterator(const jit::JitActivation& activation);

  // Start unwinding at a group of wasm frames after unwinding an inner group
  // of JSJit frames.
  explicit ProfilingFrameIterator(const Frame* fp);

  // Start unwinding at the innermost activation given the register state when
  // the thread was suspended.
  ProfilingFrameIterator(const jit::JitActivation& activation,
                         const RegisterState& state);

  void operator++();
  bool done() const { return !codeRange_ && exitReason_.isNone(); }

  void* stackAddress() const {
    MOZ_ASSERT(!done());
    return stackAddress_;
  }
  uint8_t* unwoundIonCallerFP() const {
    MOZ_ASSERT(done());
    return unwoundIonCallerFP_;
  }
  const char* label() const;
};

// Prologue/epilogue code generation

void SetExitFP(jit::MacroAssembler& masm, ExitReason reason,
               jit::Register scratch);
void ClearExitFP(jit::MacroAssembler& masm, jit::Register scratch);

void GenerateExitPrologue(jit::MacroAssembler& masm, unsigned framePushed,
                          ExitReason reason, CallableOffsets* offsets);
void GenerateExitEpilogue(jit::MacroAssembler& masm, unsigned framePushed,
                          ExitReason reason, CallableOffsets* offsets);

void GenerateJitExitPrologue(jit::MacroAssembler& masm, unsigned framePushed,
                             CallableOffsets* offsets);
void GenerateJitExitEpilogue(jit::MacroAssembler& masm, unsigned framePushed,
                             CallableOffsets* offsets);

void GenerateJitEntryPrologue(jit::MacroAssembler& masm, Offsets* offsets);

void GenerateFunctionPrologue(jit::MacroAssembler& masm,
                              const TypeIdDesc& funcTypeId,
                              const mozilla::Maybe<uint32_t>& tier1FuncIndex,
                              FuncOffsets* offsets);
void GenerateFunctionEpilogue(jit::MacroAssembler& masm, unsigned framePushed,
                              FuncOffsets* offsets);

// Iterates through frames for either possible cross-instance call or an entry
// stub to obtain tls that corresponds to the passed fp.
const TlsData* GetNearestEffectiveTls(const Frame* fp);
TlsData* GetNearestEffectiveTls(Frame* fp);

// Describes register state and associated code at a given call frame.

struct UnwindState {
  uint8_t* fp;
  void* pc;
  const Code* code;
  const CodeRange* codeRange;
  UnwindState() : fp(nullptr), pc(nullptr), code(nullptr), codeRange(nullptr) {}
};

// Ensures the register state at a call site is consistent: pc must be in the
// code range of the code described by fp. This prevents issues when using
// the values of pc/fp, especially at call sites boundaries, where the state
// hasn't fully transitioned from the caller's to the callee's.
//
// unwoundCaller is set to true if we were in a transitional state and had to
// rewind to the caller's frame instead of the current frame.
//
// Returns true if it was possible to get to a clear state, or false if the
// frame should be ignored.

bool StartUnwinding(const RegisterState& registers, UnwindState* unwindState,
                    bool* unwoundCaller);

}  // namespace wasm
}  // namespace js

#endif  // wasm_frame_iter_h

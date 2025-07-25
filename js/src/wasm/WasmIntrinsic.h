/* Copyright 2021 Mozilla Foundation
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

#ifndef wasm_intrinsic_h
#define wasm_intrinsic_h

#include "mozilla/Span.h"

#include "wasm/WasmBuiltins.h"
#include "wasm/WasmConstants.h"
#include "wasm/WasmTypeDecls.h"
#include "wasm/WasmTypeDef.h"

namespace js {
namespace wasm {

// An intrinsic is a natively implemented function that may be compiled into an
// 'intrinsic module', which may be instantiated with a provided memory
// yielding an exported WebAssembly function wrapping the intrinsic.
struct Intrinsic {
  // The name of the intrinsic as it is exported
  const char* exportName;
  // The params taken by the intrinsic. No results are required for intrinsics
  // at this time, so we omit them
  mozilla::Span<const ValType> params;
  // The signature of the builtin that implements the intrinsic
  const SymbolicAddressSignature& signature;

  // Allocate a FuncType for this intrinsic, returning false for OOM
  bool funcType(FuncType* type) const;

  // Get the Intrinsic for an IntrinsicOp. IntrinsicOp must be validated.
  static const Intrinsic& getFromOp(IntrinsicOp op);
};

// Compile and return the intrinsic module for a given intrinsic op. This only
// allows one intrinsic to be compiled at a time, but eventually we will allow
// multiple to share a module.
bool CompileIntrinsicModule(JSContext* cx, IntrinsicOp op,
                            MutableHandleWasmModuleObject result);

}  // namespace wasm
}  // namespace js

#endif  // wasm_intrinsic_h

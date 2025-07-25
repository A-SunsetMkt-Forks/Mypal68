/* Copyright 2018 Mozilla Foundation
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

// This is an ADT-style C API to the WebAssembly per-function compilation state,
// allowing Rust to access constant metadata and produce output.
//
// This file is input to Rust's bindgen, so as to create primitive APIs for the
// Cranelift pipeline to access compilation metadata. The actual Rust API then
// wraps these primitive APIs.  See src/bindings/mod.rs.
//
// This file can be included in SpiderMonkey's C++ code, where all the prefixes
// must be obeyed.  The purpose of the prefixes is to avoid type confusion.  See
// js/src/wasm/WasmCraneliftCompile.cpp.

#ifndef wasm_cranelift_baldrapi_h
#define wasm_cranelift_baldrapi_h

// DO NOT INCLUDE SPIDERMONKEY HEADER FILES INTO THIS FILE.
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

#include "wasm/WasmConstants.h"

// wasm/*.{cpp,h}, class and struct types that are opaque to us here
namespace js {
namespace wasm {
// wasm/WasmGenerator.h
struct FuncCompileInput;
// wasm/WasmTypes.h
class GlobalDesc;
struct FuncTypeWithId;
struct TableDesc;
// wasm/WasmValidate.h
struct ModuleEnvironment;
}  // namespace wasm
}  // namespace js

// This struct contains all the information that can be computed once for the
// entire process and then should never change. It contains a mix of CPU
// feature detection flags, and static information the C++ compile has access
// to, but which can't be automatically provided to Rust.

struct CraneliftStaticEnvironment {
  bool hasSse2;
  bool hasSse3;
  bool hasSse41;
  bool hasSse42;
  bool hasPopcnt;
  bool hasAvx;
  bool hasBmi1;
  bool hasBmi2;
  bool hasLzcnt;
  bool platformIsWindows;
  size_t staticMemoryBound;
  size_t memoryGuardSize;
  size_t memoryBaseTlsOffset;
  size_t instanceTlsOffset;
  size_t interruptTlsOffset;
  size_t cxTlsOffset;
  size_t realmCxOffset;
  size_t realmTlsOffset;
  size_t realmFuncImportTlsOffset;

  // Not bindgen'd because it's inlined.
  inline CraneliftStaticEnvironment();
};

// This structure proxies the C++ ModuleEnvironment and the information it
// contains.

struct CraneliftModuleEnvironment {
  // This is a pointer and not a reference to work-around a bug in bindgen.
  const js::wasm::ModuleEnvironment* env;
  uint32_t min_memory_length;

  // Not bindgen'd because it's inlined.
  explicit inline CraneliftModuleEnvironment(
      const js::wasm::ModuleEnvironment& env);
};

// Data for a single wasm function to be compiled by Cranelift.
// This information is all from the corresponding `js::wasm::FuncCompileInput`
// struct, but formatted in a Rust-friendly way.

struct CraneliftFuncCompileInput {
  const uint8_t* bytecode;
  size_t bytecodeSize;
  uint32_t index;
  uint32_t offset_in_module;

  // Not bindgen'd because it's inlined.
  explicit inline CraneliftFuncCompileInput(const js::wasm::FuncCompileInput&);
};

// A single entry in all the metadata array provided after the compilation of a
// single wasm function. The meaning of the field extra depends on the enum
// value.
//
// XXX should we use a union for this instead? bindgen seems to be able to
// handle them, with a lot of unsafe'ing.

struct CraneliftMetadataEntry {
  enum Which {
    DirectCall,
    IndirectCall,
    Trap,
    MemoryAccess,
    SymbolicAccess
  } which;
  uint32_t codeOffset;
  uint32_t moduleBytecodeOffset;
  size_t extra;
};

// The result of a single function compilation, containing the machine code
// generated by Cranelift, as well as some useful metadata to generate the
// prologue/epilogue etc.

struct CraneliftCompiledFunc {
  size_t numMetadata;
  const CraneliftMetadataEntry* metadatas;

  size_t framePushed;
  bool containsCalls;

  // The compiled code comprises machine code, relocatable jump tables, and
  // copyable read-only data, concatenated without padding.  The "...Size"
  // members give the sizes of the individual sections.  The code starts at
  // offsets 0; the other offsets can be derived from the sizes.
  const uint8_t* code;
  size_t codeSize;
  size_t jumptablesSize;
  size_t rodataSize;
  size_t totalSize;

  // Relocation information for instructions that reference into the jump tables
  // and read-only data segments.  The relocation information is
  // machine-specific.
  size_t numRodataRelocs;
  const uint32_t* rodataRelocs;
};

// Possible constant values for initializing globals.

struct BD_ConstantValue {
  js::wasm::TypeCode t;
  union {
    int32_t i32;
    int64_t i64;
    float f32;
    double f64;
  } u;
};

struct BD_ValType {
  size_t packed;
};

// A subset of the wasm SymbolicAddress enum.
// XXX this is not quite maintenable, because the number of values in this
// enum is hardcoded in wasm2clif.rs.

enum class BD_SymbolicAddress {
  MemoryGrow,
  MemorySize,
  FloorF32,
  FloorF64,
  CeilF32,
  CeilF64,
  NearestF32,
  NearestF64,
  TruncF32,
  TruncF64,
  Limit
};

extern "C" {
js::wasm::TypeCode env_unpack(BD_ValType type);

const js::wasm::FuncTypeWithId* env_function_signature(
    const CraneliftModuleEnvironment* env, size_t funcIndex);
size_t env_func_import_tls_offset(const CraneliftModuleEnvironment* env,
                                  size_t funcIndex);
bool env_func_is_import(const CraneliftModuleEnvironment* env,
                        size_t funcIndex);
const js::wasm::FuncTypeWithId* env_signature(
    const CraneliftModuleEnvironment* env, size_t sigIndex);
const js::wasm::TableDesc* env_table(const CraneliftModuleEnvironment* env,
                                     size_t tableIndex);
const js::wasm::GlobalDesc* env_global(const CraneliftModuleEnvironment* env,
                                       size_t globalIndex);

bool global_isConstant(const js::wasm::GlobalDesc*);
bool global_isIndirect(const js::wasm::GlobalDesc*);
BD_ConstantValue global_constantValue(const js::wasm::GlobalDesc*);
js::wasm::TypeCode global_type(const js::wasm::GlobalDesc*);
size_t global_tlsOffset(const js::wasm::GlobalDesc*);

size_t table_tlsOffset(const js::wasm::TableDesc*);

size_t funcType_numArgs(const js::wasm::FuncTypeWithId*);
const BD_ValType* funcType_args(const js::wasm::FuncTypeWithId*);
size_t funcType_numResults(const js::wasm::FuncTypeWithId*);
const BD_ValType* funcType_results(const js::wasm::FuncTypeWithId*);
js::wasm::FuncTypeIdDescKind funcType_idKind(const js::wasm::FuncTypeWithId*);
size_t funcType_idImmediate(const js::wasm::FuncTypeWithId*);
size_t funcType_idTlsOffset(const js::wasm::FuncTypeWithId*);
}  // extern "C"

#endif  // wasm_cranelift_baldrapi_h

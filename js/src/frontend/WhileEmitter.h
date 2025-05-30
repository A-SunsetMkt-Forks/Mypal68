/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef frontend_WhileEmitter_h
#define frontend_WhileEmitter_h

#include "mozilla/Attributes.h"
#include "mozilla/Maybe.h"

#include <stdint.h>

#include "frontend/BytecodeControlStructures.h"
#include "frontend/TDZCheckCache.h"

namespace js {
namespace frontend {

struct BytecodeEmitter;

// Class for emitting bytecode for while loop.
//
// Usage: (check for the return value is omitted for simplicity)
//
//   `while (cond) body`
//     WhileEmitter wh(this);
//     wh.emitCond(Some(offset_of_while),
//                 Some(offset_of_body),
//                 Some(offset_of_end));
//     emit(cond);
//     wh.emitBody();
//     emit(body);
//     wh.emitEnd();
//
class MOZ_STACK_CLASS WhileEmitter {
  BytecodeEmitter* bce_;

  mozilla::Maybe<LoopControl> loopInfo_;

  // Cache for the loop body, which is enclosed by the cache in `loopInfo_`,
  // which is effectively for the loop condition.
  mozilla::Maybe<TDZCheckCache> tdzCacheForBody_;

#ifdef DEBUG
  // The state of this emitter.
  //
  // +-------+ emitCond +------+ emitBody +------+ emitEnd  +-----+
  // | Start |--------->| Cond |--------->| Body |--------->| End |
  // +-------+          +------+          +------+          +-----+
  enum class State {
    // The initial state.
    Start,

    // After calling emitCond.
    Cond,

    // After calling emitBody.
    Body,

    // After calling emitEnd.
    End
  };
  State state_ = State::Start;
#endif

 public:
  explicit WhileEmitter(BytecodeEmitter* bce);

  // Parameters are the offset in the source code for each character below:
  //
  //   while ( x < 20 ) { ... }
  //   ^       ^              ^
  //   |       |              |
  //   |       |              endPos_
  //   |       |
  //   |       condPos_
  //   |
  //   whilePos_
  //
  // Can be Nothing() if not available.
  [[nodiscard]] bool emitCond(const mozilla::Maybe<uint32_t>& whilePos,
                              const mozilla::Maybe<uint32_t>& condPos,
                              const mozilla::Maybe<uint32_t>& endPos);
  [[nodiscard]] bool emitBody();
  [[nodiscard]] bool emitEnd();
};

} /* namespace frontend */
} /* namespace js */

#endif /* frontend_WhileEmitter_h */

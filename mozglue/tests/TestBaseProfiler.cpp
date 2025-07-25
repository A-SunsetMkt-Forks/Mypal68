/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BaseProfiler.h"

#include "mozilla/Attributes.h"

#ifdef MOZ_GECKO_PROFILER
#  include "BaseProfileJSONWriter.h"
#  include "BaseProfilerMarkerPayload.h"
#  include "mozilla/leb128iterator.h"
#  include "mozilla/ModuloBuffer.h"
#  include "mozilla/PowerOfTwo.h"
#  include "mozilla/ProfileBufferChunk.h"
#  include "mozilla/ProfileBufferChunkManagerSingle.h"
#  include "mozilla/ProfileBufferChunkManagerWithLocalLimit.h"
#  include "mozilla/ProfileBufferControlledChunkManager.h"
#  include "mozilla/ProfileChunkedBuffer.h"
#  include "mozilla/Vector.h"
#endif  // MOZ_GECKO_PROFILER

#if defined(_MSC_VER) || defined(__MINGW32__)
#  include <windows.h>
#  include <mmsystem.h>
#  include <process.h>
#else
#  include <errno.h>
#  include <string.h>
#  include <time.h>
#  include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <thread>
#include <type_traits>
#include <utility>

MOZ_MAYBE_UNUSED static void SleepMilli(unsigned aMilliseconds) {
#if defined(_MSC_VER) || defined(__MINGW32__)
  Sleep(aMilliseconds);
#else
  struct timespec ts = {/* .tv_sec */ static_cast<time_t>(aMilliseconds / 1000),
                        /* ts.tv_nsec */ long(aMilliseconds % 1000) * 1000000};
  struct timespec tr = {0, 0};
  while (nanosleep(&ts, &tr)) {
    if (errno == EINTR) {
      ts = tr;
    } else {
      printf("nanosleep() -> %s\n", strerror(errno));
      exit(1);
    }
  }
#endif
}

#ifdef MOZ_GECKO_PROFILER

using namespace mozilla;

void TestPowerOfTwoMask() {
  printf("TestPowerOfTwoMask...\n");

  static_assert(MakePowerOfTwoMask<uint32_t, 0>().MaskValue() == 0);
  constexpr PowerOfTwoMask<uint32_t> c0 = MakePowerOfTwoMask<uint32_t, 0>();
  MOZ_RELEASE_ASSERT(c0.MaskValue() == 0);

  static_assert(MakePowerOfTwoMask<uint32_t, 0xFFu>().MaskValue() == 0xFFu);
  constexpr PowerOfTwoMask<uint32_t> cFF =
      MakePowerOfTwoMask<uint32_t, 0xFFu>();
  MOZ_RELEASE_ASSERT(cFF.MaskValue() == 0xFFu);

  static_assert(MakePowerOfTwoMask<uint32_t, 0xFFFFFFFFu>().MaskValue() ==
                0xFFFFFFFFu);
  constexpr PowerOfTwoMask<uint32_t> cFFFFFFFF =
      MakePowerOfTwoMask<uint32_t, 0xFFFFFFFFu>();
  MOZ_RELEASE_ASSERT(cFFFFFFFF.MaskValue() == 0xFFFFFFFFu);

  struct TestDataU32 {
    uint32_t mInput;
    uint32_t mMask;
  };
  // clang-format off
  TestDataU32 tests[] = {
    { 0, 0 },
    { 1, 1 },
    { 2, 3 },
    { 3, 3 },
    { 4, 7 },
    { 5, 7 },
    { (1u << 31) - 1, (1u << 31) - 1 },
    { (1u << 31), uint32_t(-1) },
    { (1u << 31) + 1, uint32_t(-1) },
    { uint32_t(-1), uint32_t(-1) }
  };
  // clang-format on
  for (const TestDataU32& test : tests) {
    PowerOfTwoMask<uint32_t> p2m(test.mInput);
    MOZ_RELEASE_ASSERT(p2m.MaskValue() == test.mMask);
    for (const TestDataU32& inner : tests) {
      if (p2m.MaskValue() != uint32_t(-1)) {
        MOZ_RELEASE_ASSERT((inner.mInput % p2m) ==
                           (inner.mInput % (p2m.MaskValue() + 1)));
      }
      MOZ_RELEASE_ASSERT((inner.mInput & p2m) == (inner.mInput % p2m));
      MOZ_RELEASE_ASSERT((p2m & inner.mInput) == (inner.mInput & p2m));
    }
  }

  printf("TestPowerOfTwoMask done\n");
}

void TestPowerOfTwo() {
  printf("TestPowerOfTwo...\n");

  static_assert(MakePowerOfTwo<uint32_t, 1>().Value() == 1);
  constexpr PowerOfTwo<uint32_t> c1 = MakePowerOfTwo<uint32_t, 1>();
  MOZ_RELEASE_ASSERT(c1.Value() == 1);
  static_assert(MakePowerOfTwo<uint32_t, 1>().Mask().MaskValue() == 0);

  static_assert(MakePowerOfTwo<uint32_t, 128>().Value() == 128);
  constexpr PowerOfTwo<uint32_t> c128 = MakePowerOfTwo<uint32_t, 128>();
  MOZ_RELEASE_ASSERT(c128.Value() == 128);
  static_assert(MakePowerOfTwo<uint32_t, 128>().Mask().MaskValue() == 127);

  static_assert(MakePowerOfTwo<uint32_t, 0x80000000u>().Value() == 0x80000000u);
  constexpr PowerOfTwo<uint32_t> cMax = MakePowerOfTwo<uint32_t, 0x80000000u>();
  MOZ_RELEASE_ASSERT(cMax.Value() == 0x80000000u);
  static_assert(MakePowerOfTwo<uint32_t, 0x80000000u>().Mask().MaskValue() ==
                0x7FFFFFFFu);

  struct TestDataU32 {
    uint32_t mInput;
    uint32_t mValue;
    uint32_t mMask;
  };
  // clang-format off
  TestDataU32 tests[] = {
    { 0, 1, 0 },
    { 1, 1, 0 },
    { 2, 2, 1 },
    { 3, 4, 3 },
    { 4, 4, 3 },
    { 5, 8, 7 },
    { (1u << 31) - 1, (1u << 31), (1u << 31) - 1 },
    { (1u << 31), (1u << 31), (1u << 31) - 1 },
    { (1u << 31) + 1, (1u << 31), (1u << 31) - 1 },
    { uint32_t(-1), (1u << 31), (1u << 31) - 1 }
  };
  // clang-format on
  for (const TestDataU32& test : tests) {
    PowerOfTwo<uint32_t> p2(test.mInput);
    MOZ_RELEASE_ASSERT(p2.Value() == test.mValue);
    MOZ_RELEASE_ASSERT(p2.MaskValue() == test.mMask);
    PowerOfTwoMask<uint32_t> p2m = p2.Mask();
    MOZ_RELEASE_ASSERT(p2m.MaskValue() == test.mMask);
    for (const TestDataU32& inner : tests) {
      MOZ_RELEASE_ASSERT((inner.mInput % p2) == (inner.mInput % p2.Value()));
    }
  }

  printf("TestPowerOfTwo done\n");
}

void TestLEB128() {
  printf("TestLEB128...\n");

  MOZ_RELEASE_ASSERT(ULEB128MaxSize<uint8_t>() == 2);
  MOZ_RELEASE_ASSERT(ULEB128MaxSize<uint16_t>() == 3);
  MOZ_RELEASE_ASSERT(ULEB128MaxSize<uint32_t>() == 5);
  MOZ_RELEASE_ASSERT(ULEB128MaxSize<uint64_t>() == 10);

  struct TestDataU64 {
    uint64_t mValue;
    unsigned mSize;
    const char* mBytes;
  };
  // clang-format off
  TestDataU64 tests[] = {
    // Small numbers should keep their normal byte representation.
    {                  0u,  1, "\0" },
    {                  1u,  1, "\x01" },

    // 0111 1111 (127, or 0x7F) is the highest number that fits into a single
    // LEB128 byte. It gets encoded as 0111 1111, note the most significant bit
    // is off.
    {               0x7Fu,  1, "\x7F" },

    // Next number: 128, or 0x80.
    //   Original data representation:  1000 0000
    //     Broken up into groups of 7:         1  0000000
    // Padded with 0 (msB) or 1 (lsB):  00000001 10000000
    //            Byte representation:  0x01     0x80
    //            Little endian order:  -> 0x80 0x01
    {               0x80u,  2, "\x80\x01" },

    // Next: 129, or 0x81 (showing that we don't lose low bits.)
    //   Original data representation:  1000 0001
    //     Broken up into groups of 7:         1  0000001
    // Padded with 0 (msB) or 1 (lsB):  00000001 10000001
    //            Byte representation:  0x01     0x81
    //            Little endian order:  -> 0x81 0x01
    {               0x81u,  2, "\x81\x01" },

    // Highest 8-bit number: 255, or 0xFF.
    //   Original data representation:  1111 1111
    //     Broken up into groups of 7:         1  1111111
    // Padded with 0 (msB) or 1 (lsB):  00000001 11111111
    //            Byte representation:  0x01     0xFF
    //            Little endian order:  -> 0xFF 0x01
    {               0xFFu,  2, "\xFF\x01" },

    // Next: 256, or 0x100.
    //   Original data representation:  1 0000 0000
    //     Broken up into groups of 7:        10  0000000
    // Padded with 0 (msB) or 1 (lsB):  00000010 10000000
    //            Byte representation:  0x10     0x80
    //            Little endian order:  -> 0x80 0x02
    {              0x100u,  2, "\x80\x02" },

    // Highest 32-bit number: 0xFFFFFFFF (8 bytes, all bits set).
    // Original: 1111 1111 1111 1111 1111 1111 1111 1111
    // Groups:     1111  1111111  1111111  1111111  1111111
    // Padded: 00001111 11111111 11111111 11111111 11111111
    // Bytes:  0x0F     0xFF     0xFF     0xFF     0xFF
    // Little Endian: -> 0xFF 0xFF 0xFF 0xFF 0x0F
    {         0xFFFFFFFFu,  5, "\xFF\xFF\xFF\xFF\x0F" },

    // Highest 64-bit number: 0xFFFFFFFFFFFFFFFF (16 bytes, all bits set).
    // 64 bits, that's 9 groups of 7 bits, plus 1 (most significant) bit.
    { 0xFFFFFFFFFFFFFFFFu, 10, "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x01" }
  };
  // clang-format on

  for (const TestDataU64& test : tests) {
    MOZ_RELEASE_ASSERT(ULEB128Size(test.mValue) == test.mSize);
    // Prepare a buffer that can accomodate the largest-possible LEB128.
    uint8_t buffer[ULEB128MaxSize<uint64_t>()];
    // Use a pointer into the buffer as iterator.
    uint8_t* p = buffer;
    // And write the LEB128.
    WriteULEB128(test.mValue, p);
    // Pointer (iterator) should have advanced just past the expected LEB128
    // size.
    MOZ_RELEASE_ASSERT(p == buffer + test.mSize);
    // Check expected bytes.
    for (unsigned i = 0; i < test.mSize; ++i) {
      MOZ_RELEASE_ASSERT(buffer[i] == uint8_t(test.mBytes[i]));
    }

    // Move pointer (iterator) back to start of buffer.
    p = buffer;
    // And read the LEB128 we wrote above.
    uint64_t read = ReadULEB128<uint64_t>(p);
    // Pointer (iterator) should have also advanced just past the expected
    // LEB128 size.
    MOZ_RELEASE_ASSERT(p == buffer + test.mSize);
    // And check the read value.
    MOZ_RELEASE_ASSERT(read == test.mValue);

    // Testing ULEB128 reader.
    ULEB128Reader<uint64_t> reader;
    MOZ_RELEASE_ASSERT(!reader.IsComplete());
    // Move pointer back to start of buffer.
    p = buffer;
    for (;;) {
      // Read a byte and feed it to the reader.
      if (reader.FeedByteIsComplete(*p++)) {
        break;
      }
      // Not complete yet, we shouldn't have reached the end pointer.
      MOZ_RELEASE_ASSERT(!reader.IsComplete());
      MOZ_RELEASE_ASSERT(p < buffer + test.mSize);
    }
    MOZ_RELEASE_ASSERT(reader.IsComplete());
    // Pointer should have advanced just past the expected LEB128 size.
    MOZ_RELEASE_ASSERT(p == buffer + test.mSize);
    // And check the read value.
    MOZ_RELEASE_ASSERT(reader.Value() == test.mValue);

    // And again after a Reset.
    reader.Reset();
    MOZ_RELEASE_ASSERT(!reader.IsComplete());
    p = buffer;
    for (;;) {
      if (reader.FeedByteIsComplete(*p++)) {
        break;
      }
      MOZ_RELEASE_ASSERT(!reader.IsComplete());
      MOZ_RELEASE_ASSERT(p < buffer + test.mSize);
    }
    MOZ_RELEASE_ASSERT(reader.IsComplete());
    MOZ_RELEASE_ASSERT(p == buffer + test.mSize);
    MOZ_RELEASE_ASSERT(reader.Value() == test.mValue);
  }

  printf("TestLEB128 done\n");
}

template <uint8_t byte, uint8_t... tail>
constexpr bool TestConstexprULEB128Reader(ULEB128Reader<uint64_t>& aReader) {
  if (aReader.IsComplete()) {
    return false;
  }
  const bool isComplete = aReader.FeedByteIsComplete(byte);
  if (aReader.IsComplete() != isComplete) {
    return false;
  }
  if constexpr (sizeof...(tail) == 0) {
    return isComplete;
  } else {
    if (isComplete) {
      return false;
    }
    return TestConstexprULEB128Reader<tail...>(aReader);
  }
}

template <uint64_t expected, uint8_t... bytes>
constexpr bool TestConstexprULEB128Reader() {
  ULEB128Reader<uint64_t> reader;
  if (!TestConstexprULEB128Reader<bytes...>(reader)) {
    return false;
  }
  if (!reader.IsComplete()) {
    return false;
  }
  if (reader.Value() != expected) {
    return false;
  }

  reader.Reset();
  if (!TestConstexprULEB128Reader<bytes...>(reader)) {
    return false;
  }
  if (!reader.IsComplete()) {
    return false;
  }
  if (reader.Value() != expected) {
    return false;
  }

  return true;
}

static_assert(TestConstexprULEB128Reader<0x0u, 0x0u>());
static_assert(!TestConstexprULEB128Reader<0x0u, 0x0u, 0x0u>());
static_assert(TestConstexprULEB128Reader<0x1u, 0x1u>());
static_assert(TestConstexprULEB128Reader<0x7Fu, 0x7Fu>());
static_assert(TestConstexprULEB128Reader<0x80u, 0x80u, 0x01u>());
static_assert(!TestConstexprULEB128Reader<0x80u, 0x80u>());
static_assert(!TestConstexprULEB128Reader<0x80u, 0x01u>());
static_assert(TestConstexprULEB128Reader<0x81u, 0x81u, 0x01u>());
static_assert(TestConstexprULEB128Reader<0xFFu, 0xFFu, 0x01u>());
static_assert(TestConstexprULEB128Reader<0x100u, 0x80u, 0x02u>());
static_assert(TestConstexprULEB128Reader<0xFFFFFFFFu, 0xFFu, 0xFFu, 0xFFu,
                                         0xFFu, 0x0Fu>());
static_assert(
    !TestConstexprULEB128Reader<0xFFFFFFFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu>());
static_assert(!TestConstexprULEB128Reader<0xFFFFFFFFu, 0xFFu, 0xFFu, 0xFFu,
                                          0xFFu, 0xFFu, 0x0Fu>());
static_assert(
    TestConstexprULEB128Reader<0xFFFFFFFFFFFFFFFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
                               0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0x01u>());
static_assert(
    !TestConstexprULEB128Reader<0xFFFFFFFFFFFFFFFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
                                0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu>());

static void TestChunk() {
  printf("TestChunk...\n");

  static_assert(!std::is_default_constructible_v<ProfileBufferChunk>,
                "ProfileBufferChunk should not be default-constructible");
  static_assert(
      !std::is_constructible_v<ProfileBufferChunk, ProfileBufferChunk::Length>,
      "ProfileBufferChunk should not be constructible from Length");

  static_assert(
      sizeof(ProfileBufferChunk::Header) ==
          sizeof(ProfileBufferChunk::Header::mOffsetFirstBlock) +
              sizeof(ProfileBufferChunk::Header::mOffsetPastLastBlock) +
              sizeof(ProfileBufferChunk::Header::mDoneTimeStamp) +
              sizeof(ProfileBufferChunk::Header::mBufferBytes) +
              sizeof(ProfileBufferChunk::Header::mBlockCount) +
              sizeof(ProfileBufferChunk::Header::mRangeStart) +
              sizeof(ProfileBufferChunk::Header::mProcessId) +
              sizeof(ProfileBufferChunk::Header::mPADDING),
      "ProfileBufferChunk::Header may have unwanted padding, please review");
  // Note: The above static_assert is an attempt at keeping
  // ProfileBufferChunk::Header tightly packed, but some changes could make this
  // impossible to achieve (most probably due to alignment) -- Just do your
  // best!

  constexpr ProfileBufferChunk::Length TestLen = 1000;

  // Basic allocations of different sizes.
  for (ProfileBufferChunk::Length len = 0; len <= TestLen; ++len) {
    auto chunk = ProfileBufferChunk::Create(len);
    static_assert(
        std::is_same_v<decltype(chunk), UniquePtr<ProfileBufferChunk>>,
        "ProfileBufferChunk::Create() should return a "
        "UniquePtr<ProfileBufferChunk>");
    MOZ_RELEASE_ASSERT(!!chunk, "OOM!?");
    MOZ_RELEASE_ASSERT(chunk->BufferBytes() >= len);
    MOZ_RELEASE_ASSERT(chunk->ChunkBytes() >=
                       len + ProfileBufferChunk::SizeofChunkMetadata());
    MOZ_RELEASE_ASSERT(chunk->RemainingBytes() == chunk->BufferBytes());
    MOZ_RELEASE_ASSERT(chunk->OffsetFirstBlock() == 0);
    MOZ_RELEASE_ASSERT(chunk->OffsetPastLastBlock() == 0);
    MOZ_RELEASE_ASSERT(chunk->BlockCount() == 0);
    MOZ_RELEASE_ASSERT(chunk->ProcessId() == 0);
    MOZ_RELEASE_ASSERT(chunk->RangeStart() == 0);
    MOZ_RELEASE_ASSERT(chunk->BufferSpan().LengthBytes() ==
                       chunk->BufferBytes());
    MOZ_RELEASE_ASSERT(!chunk->GetNext());
    MOZ_RELEASE_ASSERT(!chunk->ReleaseNext());
    MOZ_RELEASE_ASSERT(chunk->Last() == chunk.get());
  }

  // Allocate the main test Chunk.
  auto chunkA = ProfileBufferChunk::Create(TestLen);
  MOZ_RELEASE_ASSERT(!!chunkA, "OOM!?");
  MOZ_RELEASE_ASSERT(chunkA->BufferBytes() >= TestLen);
  MOZ_RELEASE_ASSERT(chunkA->ChunkBytes() >=
                     TestLen + ProfileBufferChunk::SizeofChunkMetadata());
  MOZ_RELEASE_ASSERT(!chunkA->GetNext());
  MOZ_RELEASE_ASSERT(!chunkA->ReleaseNext());

  constexpr ProfileBufferIndex chunkARangeStart = 12345;
  chunkA->SetRangeStart(chunkARangeStart);
  MOZ_RELEASE_ASSERT(chunkA->RangeStart() == chunkARangeStart);

  // Get a read-only span over its buffer.
  auto bufferA = chunkA->BufferSpan();
  static_assert(
      std::is_same_v<decltype(bufferA), Span<const ProfileBufferChunk::Byte>>,
      "BufferSpan() should return a Span<const Byte>");
  MOZ_RELEASE_ASSERT(bufferA.LengthBytes() == chunkA->BufferBytes());

  // Add the initial tail block.
  constexpr ProfileBufferChunk::Length initTailLen = 10;
  auto initTail = chunkA->ReserveInitialBlockAsTail(initTailLen);
  static_assert(
      std::is_same_v<decltype(initTail), Span<ProfileBufferChunk::Byte>>,
      "ReserveInitialBlockAsTail() should return a Span<Byte>");
  MOZ_RELEASE_ASSERT(initTail.LengthBytes() == initTailLen);
  MOZ_RELEASE_ASSERT(initTail.Elements() == bufferA.Elements());
  MOZ_RELEASE_ASSERT(chunkA->OffsetFirstBlock() == initTailLen);
  MOZ_RELEASE_ASSERT(chunkA->OffsetPastLastBlock() == initTailLen);

  // Add the first complete block.
  constexpr ProfileBufferChunk::Length block1Len = 20;
  auto block1 = chunkA->ReserveBlock(block1Len);
  static_assert(
      std::is_same_v<decltype(block1), ProfileBufferChunk::ReserveReturn>,
      "ReserveBlock() should return a ReserveReturn");
  MOZ_RELEASE_ASSERT(block1.mBlockRangeIndex.ConvertToProfileBufferIndex() ==
                     chunkARangeStart + initTailLen);
  MOZ_RELEASE_ASSERT(block1.mSpan.LengthBytes() == block1Len);
  MOZ_RELEASE_ASSERT(block1.mSpan.Elements() ==
                     bufferA.Elements() + initTailLen);
  MOZ_RELEASE_ASSERT(chunkA->OffsetFirstBlock() == initTailLen);
  MOZ_RELEASE_ASSERT(chunkA->OffsetPastLastBlock() == initTailLen + block1Len);
  MOZ_RELEASE_ASSERT(chunkA->RemainingBytes() != 0);

  // Add another block to over-fill the ProfileBufferChunk.
  const ProfileBufferChunk::Length remaining =
      chunkA->BufferBytes() - (initTailLen + block1Len);
  constexpr ProfileBufferChunk::Length overfill = 30;
  const ProfileBufferChunk::Length block2Len = remaining + overfill;
  ProfileBufferChunk::ReserveReturn block2 = chunkA->ReserveBlock(block2Len);
  MOZ_RELEASE_ASSERT(block2.mBlockRangeIndex.ConvertToProfileBufferIndex() ==
                     chunkARangeStart + initTailLen + block1Len);
  MOZ_RELEASE_ASSERT(block2.mSpan.LengthBytes() == remaining);
  MOZ_RELEASE_ASSERT(block2.mSpan.Elements() ==
                     bufferA.Elements() + initTailLen + block1Len);
  MOZ_RELEASE_ASSERT(chunkA->OffsetFirstBlock() == initTailLen);
  MOZ_RELEASE_ASSERT(chunkA->OffsetPastLastBlock() == chunkA->BufferBytes());
  MOZ_RELEASE_ASSERT(chunkA->RemainingBytes() == 0);

  // Block must be marked "done" before it can be recycled.
  chunkA->MarkDone();

  // It must be marked "recycled" before data can be added to it again.
  chunkA->MarkRecycled();

  // Add an empty initial tail block.
  Span<ProfileBufferChunk::Byte> initTail2 =
      chunkA->ReserveInitialBlockAsTail(0);
  MOZ_RELEASE_ASSERT(initTail2.LengthBytes() == 0);
  MOZ_RELEASE_ASSERT(initTail2.Elements() == bufferA.Elements());
  MOZ_RELEASE_ASSERT(chunkA->OffsetFirstBlock() == 0);
  MOZ_RELEASE_ASSERT(chunkA->OffsetPastLastBlock() == 0);

  // Block must be marked "done" before it can be destroyed.
  chunkA->MarkDone();

  chunkA->SetProcessId(123);
  MOZ_RELEASE_ASSERT(chunkA->ProcessId() == 123);

  printf("TestChunk done\n");
}

static void TestChunkManagerSingle() {
  printf("TestChunkManagerSingle...\n");

  // Construct a ProfileBufferChunkManagerSingle for one chunk of size >=1000.
  constexpr ProfileBufferChunk::Length ChunkMinBufferBytes = 1000;
  ProfileBufferChunkManagerSingle cms{ChunkMinBufferBytes};

  // Reference to base class, to exercize virtual methods.
  ProfileBufferChunkManager& cm = cms;

#  ifdef DEBUG
  const char* chunkManagerRegisterer = "TestChunkManagerSingle";
  cm.RegisteredWith(chunkManagerRegisterer);
#  endif  // DEBUG

  const auto maxTotalSize = cm.MaxTotalSize();
  MOZ_RELEASE_ASSERT(maxTotalSize >= ChunkMinBufferBytes);

  cm.SetChunkDestroyedCallback([](const ProfileBufferChunk&) {
    MOZ_RELEASE_ASSERT(
        false,
        "ProfileBufferChunkManagerSingle should never destroy its one chunk");
  });

  UniquePtr<ProfileBufferChunk> extantReleasedChunks =
      cm.GetExtantReleasedChunks();
  MOZ_RELEASE_ASSERT(!extantReleasedChunks, "Unexpected released chunk(s)");

  // First request.
  UniquePtr<ProfileBufferChunk> chunk = cm.GetChunk();
  MOZ_RELEASE_ASSERT(!!chunk, "First chunk request should always work");
  MOZ_RELEASE_ASSERT(chunk->BufferBytes() >= ChunkMinBufferBytes,
                     "Unexpected chunk size");
  MOZ_RELEASE_ASSERT(!chunk->GetNext(), "There should only be one chunk");

  // Keep address, for later checks.
  const uintptr_t chunkAddress = reinterpret_cast<uintptr_t>(chunk.get());

  extantReleasedChunks = cm.GetExtantReleasedChunks();
  MOZ_RELEASE_ASSERT(!extantReleasedChunks, "Unexpected released chunk(s)");

  // Second request.
  MOZ_RELEASE_ASSERT(!cm.GetChunk(), "Second chunk request should always fail");

  extantReleasedChunks = cm.GetExtantReleasedChunks();
  MOZ_RELEASE_ASSERT(!extantReleasedChunks, "Unexpected released chunk(s)");

  // Add some data to the chunk (to verify recycling later on).
  MOZ_RELEASE_ASSERT(chunk->ChunkHeader().mOffsetFirstBlock == 0);
  MOZ_RELEASE_ASSERT(chunk->ChunkHeader().mOffsetPastLastBlock == 0);
  MOZ_RELEASE_ASSERT(chunk->RangeStart() == 0);
  chunk->SetRangeStart(100);
  MOZ_RELEASE_ASSERT(chunk->RangeStart() == 100);
  Unused << chunk->ReserveInitialBlockAsTail(1);
  Unused << chunk->ReserveBlock(2);
  MOZ_RELEASE_ASSERT(chunk->ChunkHeader().mOffsetFirstBlock == 1);
  MOZ_RELEASE_ASSERT(chunk->ChunkHeader().mOffsetPastLastBlock == 1 + 2);

  // Release the first chunk.
  chunk->MarkDone();
  cm.ReleaseChunks(std::move(chunk));
  MOZ_RELEASE_ASSERT(!chunk, "chunk UniquePtr should have been moved-from");

  // Request after release.
  MOZ_RELEASE_ASSERT(!cm.GetChunk(),
                     "Chunk request after release should also fail");

  // Check released chunk.
  extantReleasedChunks = cm.GetExtantReleasedChunks();
  MOZ_RELEASE_ASSERT(!!extantReleasedChunks,
                     "Could not retrieve released chunk");
  MOZ_RELEASE_ASSERT(!extantReleasedChunks->GetNext(),
                     "There should only be one released chunk");
  MOZ_RELEASE_ASSERT(
      reinterpret_cast<uintptr_t>(extantReleasedChunks.get()) == chunkAddress,
      "Released chunk should be first requested one");

  MOZ_RELEASE_ASSERT(!cm.GetExtantReleasedChunks(),
                     "Unexpected extra released chunk(s)");

  // Another request after release.
  MOZ_RELEASE_ASSERT(!cm.GetChunk(),
                     "Chunk request after release should also fail");

  MOZ_RELEASE_ASSERT(
      cm.MaxTotalSize() == maxTotalSize,
      "MaxTotalSize() should not change after requests&releases");

  // Reset the chunk manager. (Single-only non-virtual function.)
  cms.Reset(std::move(extantReleasedChunks));
  MOZ_RELEASE_ASSERT(!extantReleasedChunks,
                     "Released chunk UniquePtr should have been moved-from");

  MOZ_RELEASE_ASSERT(
      cm.MaxTotalSize() == maxTotalSize,
      "MaxTotalSize() should not change when resetting with the same chunk");

  // 2nd round, first request. Theoretically async, but this implementation just
  // immediately runs the callback.
  bool ran = false;
  cm.RequestChunk([&](UniquePtr<ProfileBufferChunk> aChunk) {
    ran = true;
    MOZ_RELEASE_ASSERT(!!aChunk);
    chunk = std::move(aChunk);
  });
  MOZ_RELEASE_ASSERT(ran, "RequestChunk callback not called immediately");
  ran = false;
  cm.FulfillChunkRequests();
  MOZ_RELEASE_ASSERT(!ran, "FulfillChunkRequests should not have any effects");
  MOZ_RELEASE_ASSERT(!!chunk, "First chunk request should always work");
  MOZ_RELEASE_ASSERT(chunk->BufferBytes() >= ChunkMinBufferBytes,
                     "Unexpected chunk size");
  MOZ_RELEASE_ASSERT(!chunk->GetNext(), "There should only be one chunk");
  MOZ_RELEASE_ASSERT(reinterpret_cast<uintptr_t>(chunk.get()) == chunkAddress,
                     "Requested chunk should be first requested one");
  // Verify that chunk is empty and usable.
  MOZ_RELEASE_ASSERT(chunk->ChunkHeader().mOffsetFirstBlock == 0);
  MOZ_RELEASE_ASSERT(chunk->ChunkHeader().mOffsetPastLastBlock == 0);
  MOZ_RELEASE_ASSERT(chunk->RangeStart() == 0);
  chunk->SetRangeStart(200);
  MOZ_RELEASE_ASSERT(chunk->RangeStart() == 200);
  Unused << chunk->ReserveInitialBlockAsTail(3);
  Unused << chunk->ReserveBlock(4);
  MOZ_RELEASE_ASSERT(chunk->ChunkHeader().mOffsetFirstBlock == 3);
  MOZ_RELEASE_ASSERT(chunk->ChunkHeader().mOffsetPastLastBlock == 3 + 4);

  // Second request.
  ran = false;
  cm.RequestChunk([&](UniquePtr<ProfileBufferChunk> aChunk) {
    ran = true;
    MOZ_RELEASE_ASSERT(!aChunk, "Second chunk request should always fail");
  });
  MOZ_RELEASE_ASSERT(ran, "RequestChunk callback not called");

  // This one does nothing.
  cm.ForgetUnreleasedChunks();

  // Don't forget to mark chunk "Done" before letting it die.
  chunk->MarkDone();
  chunk = nullptr;

  // Create a tiny chunk and reset the chunk manager with it.
  chunk = ProfileBufferChunk::Create(1);
  MOZ_RELEASE_ASSERT(!!chunk);
  auto tinyChunkSize = chunk->BufferBytes();
  MOZ_RELEASE_ASSERT(tinyChunkSize >= 1);
  MOZ_RELEASE_ASSERT(tinyChunkSize < ChunkMinBufferBytes);
  MOZ_RELEASE_ASSERT(chunk->RangeStart() == 0);
  chunk->SetRangeStart(300);
  MOZ_RELEASE_ASSERT(chunk->RangeStart() == 300);
  cms.Reset(std::move(chunk));
  MOZ_RELEASE_ASSERT(!chunk, "chunk UniquePtr should have been moved-from");
  MOZ_RELEASE_ASSERT(cm.MaxTotalSize() == tinyChunkSize,
                     "MaxTotalSize() should match the new chunk size");
  chunk = cm.GetChunk();
  MOZ_RELEASE_ASSERT(chunk->RangeStart() == 0, "Got non-recycled chunk");

  // Enough testing! Clean-up.
  Unused << chunk->ReserveInitialBlockAsTail(0);
  chunk->MarkDone();
  cm.ForgetUnreleasedChunks();

#  ifdef DEBUG
  cm.DeregisteredFrom(chunkManagerRegisterer);
#  endif  // DEBUG

  printf("TestChunkManagerSingle done\n");
}

static void TestChunkManagerWithLocalLimit() {
  printf("TestChunkManagerWithLocalLimit...\n");

  // Construct a ProfileBufferChunkManagerWithLocalLimit with chunk of minimum
  // size >=100, up to 1000 bytes.
  constexpr ProfileBufferChunk::Length MaxTotalBytes = 1000;
  constexpr ProfileBufferChunk::Length ChunkMinBufferBytes = 100;
  ProfileBufferChunkManagerWithLocalLimit cmll{MaxTotalBytes,
                                               ChunkMinBufferBytes};

  // Reference to base class, to exercize virtual methods.
  ProfileBufferChunkManager& cm = cmll;

#  ifdef DEBUG
  const char* chunkManagerRegisterer = "TestChunkManagerWithLocalLimit";
  cm.RegisteredWith(chunkManagerRegisterer);
#  endif  // DEBUG

  MOZ_RELEASE_ASSERT(cm.MaxTotalSize() == MaxTotalBytes,
                     "Max total size should be exactly as given");

  unsigned destroyedChunks = 0;
  unsigned destroyedBytes = 0;
  cm.SetChunkDestroyedCallback([&](const ProfileBufferChunk& aChunks) {
    for (const ProfileBufferChunk* chunk = &aChunks; chunk;
         chunk = chunk->GetNext()) {
      destroyedChunks += 1;
      destroyedBytes += chunk->BufferBytes();
    }
  });

  UniquePtr<ProfileBufferChunk> extantReleasedChunks =
      cm.GetExtantReleasedChunks();
  MOZ_RELEASE_ASSERT(!extantReleasedChunks, "Unexpected released chunk(s)");

  // First request.
  UniquePtr<ProfileBufferChunk> chunk = cm.GetChunk();
  MOZ_RELEASE_ASSERT(!!chunk,
                     "First chunk immediate request should always work");
  const auto chunkActualBufferBytes = chunk->BufferBytes();
  MOZ_RELEASE_ASSERT(chunkActualBufferBytes >= ChunkMinBufferBytes,
                     "Unexpected chunk size");
  MOZ_RELEASE_ASSERT(!chunk->GetNext(), "There should only be one chunk");

  // Keep address, for later checks.
  const uintptr_t chunk1Address = reinterpret_cast<uintptr_t>(chunk.get());

  extantReleasedChunks = cm.GetExtantReleasedChunks();
  MOZ_RELEASE_ASSERT(!extantReleasedChunks, "Unexpected released chunk(s)");

  // For this test, we need to be able to get at least 2 chunks without hitting
  // the limit. (If this failed, it wouldn't necessary be a problem with
  // ProfileBufferChunkManagerWithLocalLimit, fiddle with constants at the top
  // of this test.)
  MOZ_RELEASE_ASSERT(chunkActualBufferBytes < 2 * MaxTotalBytes);

  unsigned chunk1ReuseCount = 0;

  // We will do enough loops to go through the maximum size a number of times.
  const unsigned Rollovers = 3;
  const unsigned Loops = Rollovers * MaxTotalBytes / chunkActualBufferBytes;
  for (unsigned i = 0; i < Loops; ++i) {
    // Add some data to the chunk.
    MOZ_RELEASE_ASSERT(chunk->ChunkHeader().mOffsetFirstBlock == 0);
    MOZ_RELEASE_ASSERT(chunk->ChunkHeader().mOffsetPastLastBlock == 0);
    MOZ_RELEASE_ASSERT(chunk->RangeStart() == 0);
    const ProfileBufferIndex index = 1 + i * chunkActualBufferBytes;
    chunk->SetRangeStart(index);
    MOZ_RELEASE_ASSERT(chunk->RangeStart() == index);
    Unused << chunk->ReserveInitialBlockAsTail(1);
    Unused << chunk->ReserveBlock(2);
    MOZ_RELEASE_ASSERT(chunk->ChunkHeader().mOffsetFirstBlock == 1);
    MOZ_RELEASE_ASSERT(chunk->ChunkHeader().mOffsetPastLastBlock == 1 + 2);

    // Request a new chunk.
    bool ran = false;
    UniquePtr<ProfileBufferChunk> newChunk;
    cm.RequestChunk([&](UniquePtr<ProfileBufferChunk> aChunk) {
      ran = true;
      newChunk = std::move(aChunk);
    });
    MOZ_RELEASE_ASSERT(
        !ran, "RequestChunk should not immediately fulfill the request");
    cm.FulfillChunkRequests();
    MOZ_RELEASE_ASSERT(ran, "FulfillChunkRequests should invoke the callback");
    MOZ_RELEASE_ASSERT(!!newChunk, "Chunk request should always work");
    MOZ_RELEASE_ASSERT(newChunk->BufferBytes() == chunkActualBufferBytes,
                       "Unexpected chunk size");
    MOZ_RELEASE_ASSERT(!newChunk->GetNext(), "There should only be one chunk");

    // Mark previous chunk done and release it.
    chunk->MarkDone();
    cm.ReleaseChunks(std::move(chunk));

    // And cycle to the new chunk.
    chunk = std::move(newChunk);

    if (reinterpret_cast<uintptr_t>(chunk.get()) == chunk1Address) {
      ++chunk1ReuseCount;
    }
  }

  // Expect all rollovers except 1 to destroy chunks.
  MOZ_RELEASE_ASSERT(destroyedChunks >= (Rollovers - 1) * MaxTotalBytes /
                                            chunkActualBufferBytes,
                     "Not enough destroyed chunks");
  MOZ_RELEASE_ASSERT(destroyedBytes == destroyedChunks * chunkActualBufferBytes,
                     "Mismatched destroyed chunks and bytes");
  MOZ_RELEASE_ASSERT(chunk1ReuseCount >= (Rollovers - 1),
                     "Not enough reuse of the first chunks");

  // Check that chunk manager is reentrant from request callback.
  bool ran = false;
  bool ranInner = false;
  UniquePtr<ProfileBufferChunk> newChunk;
  cm.RequestChunk([&](UniquePtr<ProfileBufferChunk> aChunk) {
    ran = true;
    MOZ_RELEASE_ASSERT(!!aChunk, "Chunk request should always work");
    Unused << aChunk->ReserveInitialBlockAsTail(0);
    aChunk->MarkDone();
    UniquePtr<ProfileBufferChunk> anotherChunk = cm.GetChunk();
    MOZ_RELEASE_ASSERT(!!anotherChunk);
    Unused << anotherChunk->ReserveInitialBlockAsTail(0);
    anotherChunk->MarkDone();
    cm.RequestChunk([&](UniquePtr<ProfileBufferChunk> aChunk) {
      ranInner = true;
      MOZ_RELEASE_ASSERT(!!aChunk, "Chunk request should always work");
      Unused << aChunk->ReserveInitialBlockAsTail(0);
      aChunk->MarkDone();
    });
    MOZ_RELEASE_ASSERT(
        !ranInner, "RequestChunk should not immediately fulfill the request");
  });
  MOZ_RELEASE_ASSERT(!ran,
                     "RequestChunk should not immediately fulfill the request");
  MOZ_RELEASE_ASSERT(
      !ranInner,
      "RequestChunk should not immediately fulfill the inner request");
  cm.FulfillChunkRequests();
  MOZ_RELEASE_ASSERT(ran, "FulfillChunkRequests should invoke the callback");
  MOZ_RELEASE_ASSERT(!ranInner,
                     "FulfillChunkRequests should not immediately fulfill "
                     "the inner request");
  cm.FulfillChunkRequests();
  MOZ_RELEASE_ASSERT(
      ran, "2nd FulfillChunkRequests should invoke the inner request callback");

  // Enough testing! Clean-up.
  Unused << chunk->ReserveInitialBlockAsTail(0);
  chunk->MarkDone();
  cm.ForgetUnreleasedChunks();

#  ifdef DEBUG
  cm.DeregisteredFrom(chunkManagerRegisterer);
#  endif  // DEBUG

  printf("TestChunkManagerWithLocalLimit done\n");
}

static bool IsSameMetadata(
    const ProfileBufferControlledChunkManager::ChunkMetadata& a1,
    const ProfileBufferControlledChunkManager::ChunkMetadata& a2) {
  return a1.mDoneTimeStamp == a2.mDoneTimeStamp &&
         a1.mBufferBytes == a2.mBufferBytes;
};

static bool IsSameUpdate(
    const ProfileBufferControlledChunkManager::Update& a1,
    const ProfileBufferControlledChunkManager::Update& a2) {
  // Final and not-an-update don't carry other data, so we can test these two
  // states first.
  if (a1.IsFinal() || a2.IsFinal()) {
    return a1.IsFinal() && a2.IsFinal();
  }
  if (a1.IsNotUpdate() || a2.IsNotUpdate()) {
    return a1.IsNotUpdate() && a2.IsNotUpdate();
  }

  // Here, both are "normal" udpates, check member variables:

  if (a1.UnreleasedBytes() != a2.UnreleasedBytes()) {
    return false;
  }
  if (a1.ReleasedBytes() != a2.ReleasedBytes()) {
    return false;
  }
  if (a1.OldestDoneTimeStamp() != a2.OldestDoneTimeStamp()) {
    return false;
  }
  if (a1.NewlyReleasedChunksRef().size() !=
      a2.NewlyReleasedChunksRef().size()) {
    return false;
  }
  for (unsigned i = 0; i < a1.NewlyReleasedChunksRef().size(); ++i) {
    if (!IsSameMetadata(a1.NewlyReleasedChunksRef()[i],
                        a2.NewlyReleasedChunksRef()[i])) {
      return false;
    }
  }
  return true;
}

static void TestControlledChunkManagerUpdate() {
  printf("TestControlledChunkManagerUpdate...\n");

  using Update = ProfileBufferControlledChunkManager::Update;

  // Default construction.
  Update update1;
  MOZ_RELEASE_ASSERT(update1.IsNotUpdate());
  MOZ_RELEASE_ASSERT(!update1.IsFinal());

  // Clear an already-cleared update.
  update1.Clear();
  MOZ_RELEASE_ASSERT(update1.IsNotUpdate());
  MOZ_RELEASE_ASSERT(!update1.IsFinal());

  // Final construction with nullptr.
  const Update final(nullptr);
  MOZ_RELEASE_ASSERT(final.IsFinal());
  MOZ_RELEASE_ASSERT(!final.IsNotUpdate());

  // Copy final to cleared.
  update1 = final;
  MOZ_RELEASE_ASSERT(update1.IsFinal());
  MOZ_RELEASE_ASSERT(!update1.IsNotUpdate());

  // Copy final to final.
  update1 = final;
  MOZ_RELEASE_ASSERT(update1.IsFinal());
  MOZ_RELEASE_ASSERT(!update1.IsNotUpdate());

  // Clear a final update.
  update1.Clear();
  MOZ_RELEASE_ASSERT(update1.IsNotUpdate());
  MOZ_RELEASE_ASSERT(!update1.IsFinal());

  // Move final to cleared.
  update1 = Update(nullptr);
  MOZ_RELEASE_ASSERT(update1.IsFinal());
  MOZ_RELEASE_ASSERT(!update1.IsNotUpdate());

  // Move final to final.
  update1 = Update(nullptr);
  MOZ_RELEASE_ASSERT(update1.IsFinal());
  MOZ_RELEASE_ASSERT(!update1.IsNotUpdate());

  // Move from not-an-update (effectively same as Clear).
  update1 = Update();
  MOZ_RELEASE_ASSERT(update1.IsNotUpdate());
  MOZ_RELEASE_ASSERT(!update1.IsFinal());

  auto CreateBiggerChunkAfter = [](const ProfileBufferChunk& aChunkToBeat) {
    while (TimeStamp::NowUnfuzzed() <=
           aChunkToBeat.ChunkHeader().mDoneTimeStamp) {
      ::SleepMilli(1);
    }
    auto chunk = ProfileBufferChunk::Create(aChunkToBeat.BufferBytes() * 2);
    MOZ_RELEASE_ASSERT(!!chunk);
    MOZ_RELEASE_ASSERT(chunk->BufferBytes() >= aChunkToBeat.BufferBytes() * 2);
    Unused << chunk->ReserveInitialBlockAsTail(0);
    chunk->MarkDone();
    MOZ_RELEASE_ASSERT(chunk->ChunkHeader().mDoneTimeStamp >
                       aChunkToBeat.ChunkHeader().mDoneTimeStamp);
    return chunk;
  };

  update1 = Update(1, 2, nullptr, nullptr);

  // Create initial update with 2 released chunks and 1 unreleased chunk.
  auto released = ProfileBufferChunk::Create(10);
  ProfileBufferChunk* c1 = released.get();
  Unused << c1->ReserveInitialBlockAsTail(0);
  c1->MarkDone();

  released->SetLast(CreateBiggerChunkAfter(*c1));
  ProfileBufferChunk* c2 = c1->GetNext();

  auto unreleased = CreateBiggerChunkAfter(*c2);
  ProfileBufferChunk* c3 = unreleased.get();

  Update update2(c3->BufferBytes(), c1->BufferBytes() + c2->BufferBytes(), c1,
                 c1);
  MOZ_RELEASE_ASSERT(IsSameUpdate(
      update2,
      Update(c3->BufferBytes(), c1->BufferBytes() + c2->BufferBytes(),
             c1->ChunkHeader().mDoneTimeStamp,
             {{c1->ChunkHeader().mDoneTimeStamp, c1->BufferBytes()},
              {c2->ChunkHeader().mDoneTimeStamp, c2->BufferBytes()}})));
  // Check every field, this time only, after that we'll trust that the
  // `SameUpdate` test will be enough.
  MOZ_RELEASE_ASSERT(!update2.IsNotUpdate());
  MOZ_RELEASE_ASSERT(!update2.IsFinal());
  MOZ_RELEASE_ASSERT(update2.UnreleasedBytes() == c3->BufferBytes());
  MOZ_RELEASE_ASSERT(update2.ReleasedBytes() ==
                     c1->BufferBytes() + c2->BufferBytes());
  MOZ_RELEASE_ASSERT(update2.OldestDoneTimeStamp() ==
                     c1->ChunkHeader().mDoneTimeStamp);
  MOZ_RELEASE_ASSERT(update2.NewlyReleasedChunksRef().size() == 2);
  MOZ_RELEASE_ASSERT(
      IsSameMetadata(update2.NewlyReleasedChunksRef()[0],
                     {c1->ChunkHeader().mDoneTimeStamp, c1->BufferBytes()}));
  MOZ_RELEASE_ASSERT(
      IsSameMetadata(update2.NewlyReleasedChunksRef()[1],
                     {c2->ChunkHeader().mDoneTimeStamp, c2->BufferBytes()}));

  // Fold into not-an-update.
  update1.Fold(std::move(update2));
  MOZ_RELEASE_ASSERT(IsSameUpdate(
      update1,
      Update(c3->BufferBytes(), c1->BufferBytes() + c2->BufferBytes(),
             c1->ChunkHeader().mDoneTimeStamp,
             {{c1->ChunkHeader().mDoneTimeStamp, c1->BufferBytes()},
              {c2->ChunkHeader().mDoneTimeStamp, c2->BufferBytes()}})));

  // Pretend nothing happened.
  update2 = Update(c3->BufferBytes(), c1->BufferBytes() + c2->BufferBytes(), c1,
                   nullptr);
  MOZ_RELEASE_ASSERT(IsSameUpdate(
      update2, Update(c3->BufferBytes(), c1->BufferBytes() + c2->BufferBytes(),
                      c1->ChunkHeader().mDoneTimeStamp, {})));
  update1.Fold(std::move(update2));
  MOZ_RELEASE_ASSERT(IsSameUpdate(
      update1,
      Update(c3->BufferBytes(), c1->BufferBytes() + c2->BufferBytes(),
             c1->ChunkHeader().mDoneTimeStamp,
             {{c1->ChunkHeader().mDoneTimeStamp, c1->BufferBytes()},
              {c2->ChunkHeader().mDoneTimeStamp, c2->BufferBytes()}})));

  // Pretend there's a new unreleased chunk.
  c3->SetLast(CreateBiggerChunkAfter(*c3));
  ProfileBufferChunk* c4 = c3->GetNext();
  update2 = Update(c3->BufferBytes() + c4->BufferBytes(),
                   c1->BufferBytes() + c2->BufferBytes(), c1, nullptr);
  MOZ_RELEASE_ASSERT(
      IsSameUpdate(update2, Update(c3->BufferBytes() + c4->BufferBytes(),
                                   c1->BufferBytes() + c2->BufferBytes(),
                                   c1->ChunkHeader().mDoneTimeStamp, {})));
  update1.Fold(std::move(update2));
  MOZ_RELEASE_ASSERT(IsSameUpdate(
      update1,
      Update(c3->BufferBytes() + c4->BufferBytes(),
             c1->BufferBytes() + c2->BufferBytes(),
             c1->ChunkHeader().mDoneTimeStamp,
             {{c1->ChunkHeader().mDoneTimeStamp, c1->BufferBytes()},
              {c2->ChunkHeader().mDoneTimeStamp, c2->BufferBytes()}})));

  // Pretend the first unreleased chunk c3 has been released.
  released->SetLast(std::exchange(unreleased, unreleased->ReleaseNext()));
  update2 =
      Update(c4->BufferBytes(),
             c1->BufferBytes() + c2->BufferBytes() + c3->BufferBytes(), c1, c3);
  MOZ_RELEASE_ASSERT(IsSameUpdate(
      update2,
      Update(c4->BufferBytes(),
             c1->BufferBytes() + c2->BufferBytes() + c3->BufferBytes(),
             c1->ChunkHeader().mDoneTimeStamp,
             {{c3->ChunkHeader().mDoneTimeStamp, c3->BufferBytes()}})));
  update1.Fold(std::move(update2));
  MOZ_RELEASE_ASSERT(IsSameUpdate(
      update1,
      Update(c4->BufferBytes(),
             c1->BufferBytes() + c2->BufferBytes() + c3->BufferBytes(),
             c1->ChunkHeader().mDoneTimeStamp,
             {{c1->ChunkHeader().mDoneTimeStamp, c1->BufferBytes()},
              {c2->ChunkHeader().mDoneTimeStamp, c2->BufferBytes()},
              {c3->ChunkHeader().mDoneTimeStamp, c3->BufferBytes()}})));

  // Pretend c1 has been destroyed, so the oldest timestamp is now at c2.
  released = released->ReleaseNext();
  c1 = nullptr;
  update2 = Update(c4->BufferBytes(), c2->BufferBytes() + c3->BufferBytes(), c2,
                   nullptr);
  MOZ_RELEASE_ASSERT(IsSameUpdate(
      update2, Update(c4->BufferBytes(), c2->BufferBytes() + c3->BufferBytes(),
                      c2->ChunkHeader().mDoneTimeStamp, {})));
  update1.Fold(std::move(update2));
  MOZ_RELEASE_ASSERT(IsSameUpdate(
      update1,
      Update(c4->BufferBytes(), c2->BufferBytes() + c3->BufferBytes(),
             c2->ChunkHeader().mDoneTimeStamp,
             {{c2->ChunkHeader().mDoneTimeStamp, c2->BufferBytes()},
              {c3->ChunkHeader().mDoneTimeStamp, c3->BufferBytes()}})));

  // Pretend c2 has been recycled to make unreleased c5, and c4 has been
  // released.
  auto recycled = std::exchange(released, released->ReleaseNext());
  recycled->MarkRecycled();
  Unused << recycled->ReserveInitialBlockAsTail(0);
  recycled->MarkDone();
  released->SetLast(std::move(unreleased));
  unreleased = std::move(recycled);
  ProfileBufferChunk* c5 = c2;
  c2 = nullptr;
  update2 =
      Update(c5->BufferBytes(), c3->BufferBytes() + c4->BufferBytes(), c3, c4);
  MOZ_RELEASE_ASSERT(IsSameUpdate(
      update2,
      Update(c5->BufferBytes(), c3->BufferBytes() + c4->BufferBytes(),
             c3->ChunkHeader().mDoneTimeStamp,
             {{c4->ChunkHeader().mDoneTimeStamp, c4->BufferBytes()}})));
  update1.Fold(std::move(update2));
  MOZ_RELEASE_ASSERT(IsSameUpdate(
      update1,
      Update(c5->BufferBytes(), c3->BufferBytes() + c4->BufferBytes(),
             c3->ChunkHeader().mDoneTimeStamp,
             {{c3->ChunkHeader().mDoneTimeStamp, c3->BufferBytes()},
              {c4->ChunkHeader().mDoneTimeStamp, c4->BufferBytes()}})));

  // And send a final update.
  update1.Fold(Update(nullptr));
  MOZ_RELEASE_ASSERT(update1.IsFinal());
  MOZ_RELEASE_ASSERT(!update1.IsNotUpdate());

  printf("TestControlledChunkManagerUpdate done\n");
}

static void TestControlledChunkManagerWithLocalLimit() {
  printf("TestControlledChunkManagerWithLocalLimit...\n");

  // Construct a ProfileBufferChunkManagerWithLocalLimit with chunk of minimum
  // size >=100, up to 1000 bytes.
  constexpr ProfileBufferChunk::Length MaxTotalBytes = 1000;
  constexpr ProfileBufferChunk::Length ChunkMinBufferBytes = 100;
  ProfileBufferChunkManagerWithLocalLimit cmll{MaxTotalBytes,
                                               ChunkMinBufferBytes};

  // Reference to chunk manager base class.
  ProfileBufferChunkManager& cm = cmll;

  // Reference to controlled chunk manager base class.
  ProfileBufferControlledChunkManager& ccm = cmll;

#  ifdef DEBUG
  const char* chunkManagerRegisterer =
      "TestControlledChunkManagerWithLocalLimit";
  cm.RegisteredWith(chunkManagerRegisterer);
#  endif  // DEBUG

  MOZ_RELEASE_ASSERT(cm.MaxTotalSize() == MaxTotalBytes,
                     "Max total size should be exactly as given");

  unsigned destroyedChunks = 0;
  unsigned destroyedBytes = 0;
  cm.SetChunkDestroyedCallback([&](const ProfileBufferChunk& aChunks) {
    for (const ProfileBufferChunk* chunk = &aChunks; chunk;
         chunk = chunk->GetNext()) {
      destroyedChunks += 1;
      destroyedBytes += chunk->BufferBytes();
    }
  });

  using Update = ProfileBufferControlledChunkManager::Update;
  unsigned updateCount = 0;
  ProfileBufferControlledChunkManager::Update update;
  MOZ_RELEASE_ASSERT(update.IsNotUpdate());
  auto updateCallback = [&](Update&& aUpdate) {
    ++updateCount;
    update.Fold(std::move(aUpdate));
  };
  ccm.SetUpdateCallback(updateCallback);
  MOZ_RELEASE_ASSERT(updateCount == 1,
                     "SetUpdateCallback should have triggered an update");
  MOZ_RELEASE_ASSERT(IsSameUpdate(update, Update(0, 0, TimeStamp{}, {})));
  updateCount = 0;
  update.Clear();

  UniquePtr<ProfileBufferChunk> extantReleasedChunks =
      cm.GetExtantReleasedChunks();
  MOZ_RELEASE_ASSERT(!extantReleasedChunks, "Unexpected released chunk(s)");
  MOZ_RELEASE_ASSERT(updateCount == 1,
                     "GetExtantReleasedChunks should have triggered an update");
  MOZ_RELEASE_ASSERT(IsSameUpdate(update, Update(0, 0, TimeStamp{}, {})));
  updateCount = 0;
  update.Clear();

  // First request.
  UniquePtr<ProfileBufferChunk> chunk = cm.GetChunk();
  MOZ_RELEASE_ASSERT(!!chunk,
                     "First chunk immediate request should always work");
  const auto chunkActualBufferBytes = chunk->BufferBytes();
  // Keep address, for later checks.
  const uintptr_t chunk1Address = reinterpret_cast<uintptr_t>(chunk.get());
  MOZ_RELEASE_ASSERT(updateCount == 1,
                     "GetChunk should have triggered an update");
  MOZ_RELEASE_ASSERT(
      IsSameUpdate(update, Update(chunk->BufferBytes(), 0, TimeStamp{}, {})));
  updateCount = 0;
  update.Clear();

  extantReleasedChunks = cm.GetExtantReleasedChunks();
  MOZ_RELEASE_ASSERT(!extantReleasedChunks, "Unexpected released chunk(s)");
  MOZ_RELEASE_ASSERT(updateCount == 1,
                     "GetExtantReleasedChunks should have triggered an update");
  MOZ_RELEASE_ASSERT(
      IsSameUpdate(update, Update(chunk->BufferBytes(), 0, TimeStamp{}, {})));
  updateCount = 0;
  update.Clear();

  // For this test, we need to be able to get at least 2 chunks without hitting
  // the limit. (If this failed, it wouldn't necessary be a problem with
  // ProfileBufferChunkManagerWithLocalLimit, fiddle with constants at the top
  // of this test.)
  MOZ_RELEASE_ASSERT(chunkActualBufferBytes < 2 * MaxTotalBytes);

  ProfileBufferChunk::Length previousUnreleasedBytes = chunk->BufferBytes();
  ProfileBufferChunk::Length previousReleasedBytes = 0;
  TimeStamp previousOldestDoneTimeStamp;

  unsigned chunk1ReuseCount = 0;

  // We will do enough loops to go through the maximum size a number of times.
  const unsigned Rollovers = 3;
  const unsigned Loops = Rollovers * MaxTotalBytes / chunkActualBufferBytes;
  for (unsigned i = 0; i < Loops; ++i) {
    // Add some data to the chunk.
    const ProfileBufferIndex index =
        ProfileBufferIndex(chunkActualBufferBytes) * i + 1;
    chunk->SetRangeStart(index);
    Unused << chunk->ReserveInitialBlockAsTail(1);
    Unused << chunk->ReserveBlock(2);

    // Request a new chunk.
    UniquePtr<ProfileBufferChunk> newChunk;
    cm.RequestChunk([&](UniquePtr<ProfileBufferChunk> aChunk) {
      newChunk = std::move(aChunk);
    });
    MOZ_RELEASE_ASSERT(updateCount == 0,
                       "RequestChunk() shouldn't have triggered an update");
    cm.FulfillChunkRequests();
    MOZ_RELEASE_ASSERT(!!newChunk, "Chunk request should always work");
    MOZ_RELEASE_ASSERT(newChunk->BufferBytes() == chunkActualBufferBytes,
                       "Unexpected chunk size");
    MOZ_RELEASE_ASSERT(!newChunk->GetNext(), "There should only be one chunk");

    MOZ_RELEASE_ASSERT(updateCount == 1,
                       "FulfillChunkRequests() after a request should have "
                       "triggered an update");
    MOZ_RELEASE_ASSERT(!update.IsFinal());
    MOZ_RELEASE_ASSERT(!update.IsNotUpdate());
    MOZ_RELEASE_ASSERT(update.UnreleasedBytes() ==
                       previousUnreleasedBytes + newChunk->BufferBytes());
    previousUnreleasedBytes = update.UnreleasedBytes();
    MOZ_RELEASE_ASSERT(update.ReleasedBytes() <= previousReleasedBytes);
    previousReleasedBytes = update.ReleasedBytes();
    MOZ_RELEASE_ASSERT(previousOldestDoneTimeStamp.IsNull() ||
                       update.OldestDoneTimeStamp() >=
                           previousOldestDoneTimeStamp);
    previousOldestDoneTimeStamp = update.OldestDoneTimeStamp();
    MOZ_RELEASE_ASSERT(update.NewlyReleasedChunksRef().empty());
    updateCount = 0;
    update.Clear();

    // Make sure the "Done" timestamp below cannot be the same as from the
    // previous loop.
    const TimeStamp now = TimeStamp::NowUnfuzzed();
    while (TimeStamp::NowUnfuzzed() == now) {
      ::SleepMilli(1);
    }

    // Mark previous chunk done and release it.
    chunk->MarkDone();
    const auto doneTimeStamp = chunk->ChunkHeader().mDoneTimeStamp;
    const auto bufferBytes = chunk->BufferBytes();
    cm.ReleaseChunks(std::move(chunk));

    MOZ_RELEASE_ASSERT(updateCount == 1,
                       "ReleaseChunks() should have triggered an update");
    MOZ_RELEASE_ASSERT(!update.IsFinal());
    MOZ_RELEASE_ASSERT(!update.IsNotUpdate());
    MOZ_RELEASE_ASSERT(update.UnreleasedBytes() ==
                       previousUnreleasedBytes - bufferBytes);
    previousUnreleasedBytes = update.UnreleasedBytes();
    MOZ_RELEASE_ASSERT(update.ReleasedBytes() ==
                       previousReleasedBytes + bufferBytes);
    previousReleasedBytes = update.ReleasedBytes();
    MOZ_RELEASE_ASSERT(previousOldestDoneTimeStamp.IsNull() ||
                       update.OldestDoneTimeStamp() >=
                           previousOldestDoneTimeStamp);
    previousOldestDoneTimeStamp = update.OldestDoneTimeStamp();
    MOZ_RELEASE_ASSERT(update.OldestDoneTimeStamp() <= doneTimeStamp);
    MOZ_RELEASE_ASSERT(update.NewlyReleasedChunksRef().size() == 1);
    MOZ_RELEASE_ASSERT(update.NewlyReleasedChunksRef()[0].mDoneTimeStamp ==
                       doneTimeStamp);
    MOZ_RELEASE_ASSERT(update.NewlyReleasedChunksRef()[0].mBufferBytes ==
                       bufferBytes);
    updateCount = 0;
    update.Clear();

    // And cycle to the new chunk.
    chunk = std::move(newChunk);

    if (reinterpret_cast<uintptr_t>(chunk.get()) == chunk1Address) {
      ++chunk1ReuseCount;
    }
  }

  // Enough testing! Clean-up.
  Unused << chunk->ReserveInitialBlockAsTail(0);
  chunk->MarkDone();
  cm.ForgetUnreleasedChunks();
  MOZ_RELEASE_ASSERT(
      updateCount == 1,
      "ForgetUnreleasedChunks() should have triggered an update");
  MOZ_RELEASE_ASSERT(!update.IsFinal());
  MOZ_RELEASE_ASSERT(!update.IsNotUpdate());
  MOZ_RELEASE_ASSERT(update.UnreleasedBytes() == 0);
  MOZ_RELEASE_ASSERT(update.ReleasedBytes() == previousReleasedBytes);
  MOZ_RELEASE_ASSERT(update.NewlyReleasedChunksRef().empty() == 1);
  updateCount = 0;
  update.Clear();

  ccm.SetUpdateCallback({});
  MOZ_RELEASE_ASSERT(updateCount == 1,
                     "SetUpdateCallback({}) should have triggered an update");
  MOZ_RELEASE_ASSERT(update.IsFinal());

#  ifdef DEBUG
  cm.DeregisteredFrom(chunkManagerRegisterer);
#  endif  // DEBUG

  printf("TestControlledChunkManagerWithLocalLimit done\n");
}

static void TestChunkedBuffer() {
  printf("TestChunkedBuffer...\n");

  ProfileBufferBlockIndex blockIndex;
  MOZ_RELEASE_ASSERT(!blockIndex);
  MOZ_RELEASE_ASSERT(blockIndex == nullptr);

  // Create an out-of-session ProfileChunkedBuffer.
  ProfileChunkedBuffer cb(ProfileChunkedBuffer::ThreadSafety::WithMutex);

  MOZ_RELEASE_ASSERT(cb.BufferLength().isNothing());

  int result = 0;
  result = cb.ReserveAndPut(
      []() {
        MOZ_RELEASE_ASSERT(false);
        return 1;
      },
      [](Maybe<ProfileBufferEntryWriter>& aEW) { return aEW ? 2 : 3; });
  MOZ_RELEASE_ASSERT(result == 3);

  result = 0;
  result = cb.Put(
      1, [](Maybe<ProfileBufferEntryWriter>& aEW) { return aEW ? 1 : 2; });
  MOZ_RELEASE_ASSERT(result == 2);

  blockIndex = cb.PutFrom(&result, 1);
  MOZ_RELEASE_ASSERT(!blockIndex);

  blockIndex = cb.PutObjects(123, result, "hello");
  MOZ_RELEASE_ASSERT(!blockIndex);

  blockIndex = cb.PutObject(123);
  MOZ_RELEASE_ASSERT(!blockIndex);

  auto chunks = cb.GetAllChunks();
  static_assert(std::is_same_v<decltype(chunks), UniquePtr<ProfileBufferChunk>>,
                "ProfileChunkedBuffer::GetAllChunks() should return a "
                "UniquePtr<ProfileBufferChunk>");
  MOZ_RELEASE_ASSERT(!chunks, "Expected no chunks when out-of-session");

  bool ran = false;
  result = 0;
  result = cb.Read([&](ProfileChunkedBuffer::Reader* aReader) {
    ran = true;
    MOZ_RELEASE_ASSERT(!aReader);
    return 3;
  });
  MOZ_RELEASE_ASSERT(ran);
  MOZ_RELEASE_ASSERT(result == 3);

  cb.ReadEach([](ProfileBufferEntryReader&) { MOZ_RELEASE_ASSERT(false); });

  result = 0;
  result = cb.ReadAt(nullptr, [](Maybe<ProfileBufferEntryReader>&& er) {
    MOZ_RELEASE_ASSERT(er.isNothing());
    return 4;
  });
  MOZ_RELEASE_ASSERT(result == 4);

  // Use ProfileBufferChunkManagerWithLocalLimit, which will give away
  // ProfileBufferChunks that can contain 128 bytes, using up to 1KB of memory
  // (including usable 128 bytes and headers).
  constexpr size_t bufferMaxSize = 1024;
  constexpr ProfileChunkedBuffer::Length chunkMinSize = 128;
  ProfileBufferChunkManagerWithLocalLimit cm(bufferMaxSize, chunkMinSize);
  cb.SetChunkManager(cm);

  // Let the chunk manager fulfill the initial request for an extra chunk.
  cm.FulfillChunkRequests();

  MOZ_RELEASE_ASSERT(cm.MaxTotalSize() == bufferMaxSize);
  MOZ_RELEASE_ASSERT(cb.BufferLength().isSome());
  MOZ_RELEASE_ASSERT(*cb.BufferLength() == bufferMaxSize);

  // Write an int with the main `ReserveAndPut` function.
  const int test = 123;
  ran = false;
  blockIndex = nullptr;
  bool success = cb.ReserveAndPut(
      []() { return sizeof(test); },
      [&](Maybe<ProfileBufferEntryWriter>& aEW) {
        ran = true;
        if (!aEW) {
          return false;
        }
        blockIndex = aEW->CurrentBlockIndex();
        MOZ_RELEASE_ASSERT(aEW->RemainingBytes() == sizeof(test));
        aEW->WriteObject(test);
        MOZ_RELEASE_ASSERT(aEW->RemainingBytes() == 0);
        return true;
      });
  MOZ_RELEASE_ASSERT(ran);
  MOZ_RELEASE_ASSERT(success);
  MOZ_RELEASE_ASSERT(blockIndex.ConvertToProfileBufferIndex() == 1);

  ran = false;
  result = 0;
  result = cb.Read([&](ProfileChunkedBuffer::Reader* aReader) {
    ran = true;
    MOZ_RELEASE_ASSERT(!!aReader);
    // begin() and end() should be at the range edges (verified above).
    MOZ_RELEASE_ASSERT(
        aReader->begin().CurrentBlockIndex().ConvertToProfileBufferIndex() ==
        1);
    MOZ_RELEASE_ASSERT(
        aReader->end().CurrentBlockIndex().ConvertToProfileBufferIndex() == 0);
    // Null ProfileBufferBlockIndex clamped to the beginning.
    MOZ_RELEASE_ASSERT(aReader->At(nullptr) == aReader->begin());
    MOZ_RELEASE_ASSERT(aReader->At(blockIndex) == aReader->begin());
    // At(begin) same as begin().
    MOZ_RELEASE_ASSERT(aReader->At(aReader->begin().CurrentBlockIndex()) ==
                       aReader->begin());
    // At(past block) same as end().
    MOZ_RELEASE_ASSERT(
        aReader->At(ProfileBufferBlockIndex::CreateFromProfileBufferIndex(
            1 + 1 + sizeof(test))) == aReader->end());

    size_t read = 0;
    aReader->ForEach([&](ProfileBufferEntryReader& er) {
      ++read;
      MOZ_RELEASE_ASSERT(er.RemainingBytes() == sizeof(test));
      const auto value = er.ReadObject<decltype(test)>();
      MOZ_RELEASE_ASSERT(value == test);
      MOZ_RELEASE_ASSERT(er.RemainingBytes() == 0);
    });
    MOZ_RELEASE_ASSERT(read == 1);

    read = 0;
    for (auto er : *aReader) {
      static_assert(std::is_same_v<decltype(er), ProfileBufferEntryReader>,
                    "ProfileChunkedBuffer::Reader range-for should produce "
                    "ProfileBufferEntryReader objects");
      ++read;
      MOZ_RELEASE_ASSERT(er.RemainingBytes() == sizeof(test));
      const auto value = er.ReadObject<decltype(test)>();
      MOZ_RELEASE_ASSERT(value == test);
      MOZ_RELEASE_ASSERT(er.RemainingBytes() == 0);
    };
    MOZ_RELEASE_ASSERT(read == 1);
    return 5;
  });
  MOZ_RELEASE_ASSERT(ran);
  MOZ_RELEASE_ASSERT(result == 5);

  // Read the int directly from the ProfileChunkedBuffer, without block index.
  size_t read = 0;
  cb.ReadEach([&](ProfileBufferEntryReader& er) {
    ++read;
    MOZ_RELEASE_ASSERT(er.RemainingBytes() == sizeof(test));
    const auto value = er.ReadObject<decltype(test)>();
    MOZ_RELEASE_ASSERT(value == test);
    MOZ_RELEASE_ASSERT(er.RemainingBytes() == 0);
  });
  MOZ_RELEASE_ASSERT(read == 1);

  // Read the int directly from the ProfileChunkedBuffer, with block index.
  read = 0;
  blockIndex = nullptr;
  cb.ReadEach(
      [&](ProfileBufferEntryReader& er, ProfileBufferBlockIndex aBlockIndex) {
        ++read;
        MOZ_RELEASE_ASSERT(!!aBlockIndex);
        MOZ_RELEASE_ASSERT(!blockIndex);
        blockIndex = aBlockIndex;
        MOZ_RELEASE_ASSERT(er.RemainingBytes() == sizeof(test));
        const auto value = er.ReadObject<decltype(test)>();
        MOZ_RELEASE_ASSERT(value == test);
        MOZ_RELEASE_ASSERT(er.RemainingBytes() == 0);
      });
  MOZ_RELEASE_ASSERT(read == 1);
  MOZ_RELEASE_ASSERT(!!blockIndex);
  MOZ_RELEASE_ASSERT(blockIndex != nullptr);

  // Read the int from its block index.
  read = 0;
  result = 0;
  result = cb.ReadAt(blockIndex, [&](Maybe<ProfileBufferEntryReader>&& er) {
    ++read;
    MOZ_RELEASE_ASSERT(er.isSome());
    MOZ_RELEASE_ASSERT(er->CurrentBlockIndex() == blockIndex);
    MOZ_RELEASE_ASSERT(!er->NextBlockIndex());
    MOZ_RELEASE_ASSERT(er->RemainingBytes() == sizeof(test));
    const auto value = er->ReadObject<decltype(test)>();
    MOZ_RELEASE_ASSERT(value == test);
    MOZ_RELEASE_ASSERT(er->RemainingBytes() == 0);
    return 6;
  });
  MOZ_RELEASE_ASSERT(result == 6);
  MOZ_RELEASE_ASSERT(read == 1);

  // Steal the underlying ProfileBufferChunks from the ProfileChunkedBuffer.
  chunks = cb.GetAllChunks();
  MOZ_RELEASE_ASSERT(!!chunks, "Expected at least one chunk");
  MOZ_RELEASE_ASSERT(!!chunks->GetNext(), "Expected two chunks");
  MOZ_RELEASE_ASSERT(!chunks->GetNext()->GetNext(), "Expected only two chunks");
  const ProfileChunkedBuffer::Length chunkActualSize = chunks->BufferBytes();
  MOZ_RELEASE_ASSERT(chunkActualSize >= chunkMinSize);
  MOZ_RELEASE_ASSERT(chunks->RangeStart() == 1);
  MOZ_RELEASE_ASSERT(chunks->OffsetFirstBlock() == 0);
  MOZ_RELEASE_ASSERT(chunks->OffsetPastLastBlock() == 1 + sizeof(test));

  // Nothing more to read from the now-empty ProfileChunkedBuffer.
  cb.ReadEach([](ProfileBufferEntryReader&) { MOZ_RELEASE_ASSERT(false); });
  cb.ReadEach([](ProfileBufferEntryReader&, ProfileBufferBlockIndex) {
    MOZ_RELEASE_ASSERT(false);
  });
  result = 0;
  result = cb.ReadAt(nullptr, [](Maybe<ProfileBufferEntryReader>&& er) {
    MOZ_RELEASE_ASSERT(er.isNothing());
    return 7;
  });
  MOZ_RELEASE_ASSERT(result == 7);

  // Read the int from the stolen chunks.
  read = 0;
  ProfileChunkedBuffer::ReadEach(
      chunks.get(), nullptr,
      [&](ProfileBufferEntryReader& er, ProfileBufferBlockIndex aBlockIndex) {
        ++read;
        MOZ_RELEASE_ASSERT(aBlockIndex == blockIndex);
        MOZ_RELEASE_ASSERT(er.RemainingBytes() == sizeof(test));
        const auto value = er.ReadObject<decltype(test)>();
        MOZ_RELEASE_ASSERT(value == test);
        MOZ_RELEASE_ASSERT(er.RemainingBytes() == 0);
      });
  MOZ_RELEASE_ASSERT(read == 1);

  // Write lots of numbers (by memcpy), which should trigger Chunk destructions.
  ProfileBufferBlockIndex firstBlockIndex;
  MOZ_RELEASE_ASSERT(!firstBlockIndex);
  ProfileBufferBlockIndex lastBlockIndex;
  MOZ_RELEASE_ASSERT(!lastBlockIndex);
  const size_t lots = 2 * bufferMaxSize / (1 + sizeof(int));
  for (size_t i = 1; i < lots; ++i) {
    ProfileBufferBlockIndex blockIndex = cb.PutFrom(&i, sizeof(i));
    MOZ_RELEASE_ASSERT(!!blockIndex);
    MOZ_RELEASE_ASSERT(blockIndex > firstBlockIndex);
    if (!firstBlockIndex) {
      firstBlockIndex = blockIndex;
    }
    MOZ_RELEASE_ASSERT(blockIndex > lastBlockIndex);
    lastBlockIndex = blockIndex;
  }

  // Read extant numbers, which should at least follow each other.
  read = 0;
  size_t i = 0;
  cb.ReadEach(
      [&](ProfileBufferEntryReader& er, ProfileBufferBlockIndex aBlockIndex) {
        ++read;
        MOZ_RELEASE_ASSERT(!!aBlockIndex);
        MOZ_RELEASE_ASSERT(aBlockIndex > firstBlockIndex);
        MOZ_RELEASE_ASSERT(aBlockIndex <= lastBlockIndex);
        MOZ_RELEASE_ASSERT(er.RemainingBytes() == sizeof(size_t));
        const auto value = er.ReadObject<size_t>();
        if (i == 0) {
          i = value;
        } else {
          MOZ_RELEASE_ASSERT(value == ++i);
        }
        MOZ_RELEASE_ASSERT(er.RemainingBytes() == 0);
      });
  MOZ_RELEASE_ASSERT(read != 0);
  MOZ_RELEASE_ASSERT(read < lots);

  // Read first extant number.
  read = 0;
  i = 0;
  blockIndex = nullptr;
  success =
      cb.ReadAt(firstBlockIndex, [&](Maybe<ProfileBufferEntryReader>&& er) {
        MOZ_ASSERT(er.isSome());
        ++read;
        MOZ_RELEASE_ASSERT(er->CurrentBlockIndex() > firstBlockIndex);
        MOZ_RELEASE_ASSERT(!!er->NextBlockIndex());
        MOZ_RELEASE_ASSERT(er->NextBlockIndex() > firstBlockIndex);
        MOZ_RELEASE_ASSERT(er->NextBlockIndex() < lastBlockIndex);
        blockIndex = er->NextBlockIndex();
        MOZ_RELEASE_ASSERT(er->RemainingBytes() == sizeof(size_t));
        const auto value = er->ReadObject<size_t>();
        MOZ_RELEASE_ASSERT(i == 0);
        i = value;
        MOZ_RELEASE_ASSERT(er->RemainingBytes() == 0);
        return 7;
      });
  MOZ_RELEASE_ASSERT(success);
  MOZ_RELEASE_ASSERT(read == 1);
  // Read other extant numbers one by one.
  do {
    bool success =
        cb.ReadAt(blockIndex, [&](Maybe<ProfileBufferEntryReader>&& er) {
          MOZ_ASSERT(er.isSome());
          ++read;
          MOZ_RELEASE_ASSERT(er->CurrentBlockIndex() == blockIndex);
          MOZ_RELEASE_ASSERT(!er->NextBlockIndex() ||
                             er->NextBlockIndex() > blockIndex);
          MOZ_RELEASE_ASSERT(!er->NextBlockIndex() ||
                             er->NextBlockIndex() > firstBlockIndex);
          MOZ_RELEASE_ASSERT(!er->NextBlockIndex() ||
                             er->NextBlockIndex() <= lastBlockIndex);
          MOZ_RELEASE_ASSERT(er->NextBlockIndex()
                                 ? blockIndex < lastBlockIndex
                                 : blockIndex == lastBlockIndex,
                             "er->NextBlockIndex() should only be null when "
                             "blockIndex is at the last block");
          blockIndex = er->NextBlockIndex();
          MOZ_RELEASE_ASSERT(er->RemainingBytes() == sizeof(size_t));
          const auto value = er->ReadObject<size_t>();
          MOZ_RELEASE_ASSERT(value == ++i);
          MOZ_RELEASE_ASSERT(er->RemainingBytes() == 0);
          return true;
        });
    MOZ_RELEASE_ASSERT(success);
  } while (blockIndex);
  MOZ_RELEASE_ASSERT(read > 1);

#  ifdef DEBUG
  // cb.Dump();
#  endif

  cb.Clear();

#  ifdef DEBUG
  // cb.Dump();
#  endif

  // Start writer threads.
  constexpr int ThreadCount = 32;
  std::thread threads[ThreadCount];
  for (int threadNo = 0; threadNo < ThreadCount; ++threadNo) {
    threads[threadNo] = std::thread(
        [&](int aThreadNo) {
          ::SleepMilli(1);
          constexpr int pushCount = 1024;
          for (int push = 0; push < pushCount; ++push) {
            // Reserve as many bytes as the thread number (but at least enough
            // to store an int), and write an increasing int.
            const bool success =
                cb.Put(std::max(aThreadNo, int(sizeof(push))),
                       [&](Maybe<ProfileBufferEntryWriter>& aEW) {
                         if (!aEW) {
                           return false;
                         }
                         aEW->WriteObject(aThreadNo * 1000000 + push);
                         // Advance writer to the end.
                         for (size_t r = aEW->RemainingBytes(); r != 0; --r) {
                           aEW->WriteObject<char>('_');
                         }
                         return true;
                       });
            MOZ_RELEASE_ASSERT(success);
          }
        },
        threadNo);
  }

  // Wait for all writer threads to die.
  for (auto&& thread : threads) {
    thread.join();
  }

#  ifdef DEBUG
  // cb.Dump();
#  endif

  // Reset to out-of-session.
  cb.ResetChunkManager();

  success = cb.ReserveAndPut(
      []() {
        MOZ_RELEASE_ASSERT(false);
        return 1;
      },
      [](Maybe<ProfileBufferEntryWriter>& aEW) { return !!aEW; });
  MOZ_RELEASE_ASSERT(!success);

  success =
      cb.Put(1, [](Maybe<ProfileBufferEntryWriter>& aEW) { return !!aEW; });
  MOZ_RELEASE_ASSERT(!success);

  blockIndex = cb.PutFrom(&success, 1);
  MOZ_RELEASE_ASSERT(!blockIndex);

  blockIndex = cb.PutObjects(123, success, "hello");
  MOZ_RELEASE_ASSERT(!blockIndex);

  blockIndex = cb.PutObject(123);
  MOZ_RELEASE_ASSERT(!blockIndex);

  chunks = cb.GetAllChunks();
  MOZ_RELEASE_ASSERT(!chunks, "Expected no chunks when out-of-session");

  cb.ReadEach([](ProfileBufferEntryReader&) { MOZ_RELEASE_ASSERT(false); });

  success = cb.ReadAt(nullptr, [](Maybe<ProfileBufferEntryReader>&& er) {
    MOZ_RELEASE_ASSERT(er.isNothing());
    return true;
  });
  MOZ_RELEASE_ASSERT(success);

  printf("TestChunkedBuffer done\n");
}

static void TestChunkedBufferSingle() {
  printf("TestChunkedBufferSingle...\n");

  constexpr ProfileChunkedBuffer::Length chunkMinSize = 128;

  // Create a ProfileChunkedBuffer that will own&use a
  // ProfileBufferChunkManagerSingle, which will give away one
  // ProfileBufferChunk that can contain 128 bytes.
  ProfileChunkedBuffer cbSingle(
      ProfileChunkedBuffer::ThreadSafety::WithoutMutex,
      MakeUnique<ProfileBufferChunkManagerSingle>(chunkMinSize));

  MOZ_RELEASE_ASSERT(cbSingle.BufferLength().isSome());
  MOZ_RELEASE_ASSERT(*cbSingle.BufferLength() >= chunkMinSize);

  // Write lots of numbers (as objects), which should trigger the release of our
  // single Chunk.
  size_t firstIndexToFail = 0;
  ProfileBufferBlockIndex lastBlockIndex;
  for (size_t i = 1; i < 3 * chunkMinSize / (1 + sizeof(int)); ++i) {
    ProfileBufferBlockIndex blockIndex = cbSingle.PutObject(i);
    if (blockIndex) {
      MOZ_RELEASE_ASSERT(
          firstIndexToFail == 0,
          "We should successfully write after we have failed once");
      lastBlockIndex = blockIndex;
    } else if (firstIndexToFail == 0) {
      firstIndexToFail = i;
    }
  }
  MOZ_RELEASE_ASSERT(firstIndexToFail != 0,
                     "There should be at least one failure");
  MOZ_RELEASE_ASSERT(firstIndexToFail != 1, "We shouldn't fail from the start");
  MOZ_RELEASE_ASSERT(!!lastBlockIndex, "We shouldn't fail from the start");

  // Read extant numbers, which should go from 1 to firstIndexToFail-1.
  size_t read = 0;
  cbSingle.ReadEach(
      [&](ProfileBufferEntryReader& er, ProfileBufferBlockIndex blockIndex) {
        ++read;
        MOZ_RELEASE_ASSERT(er.RemainingBytes() == sizeof(size_t));
        const auto value = er.ReadObject<size_t>();
        MOZ_RELEASE_ASSERT(value == read);
        MOZ_RELEASE_ASSERT(er.RemainingBytes() == 0);
        MOZ_RELEASE_ASSERT(blockIndex <= lastBlockIndex,
                           "Unexpected block index past the last written one");
      });
  MOZ_RELEASE_ASSERT(read == firstIndexToFail - 1,
                     "We should have read up to before the first failure");

  // Test AppendContent:
  // Create another ProfileChunkedBuffer that will use a
  // ProfileBufferChunkManagerWithLocalLimit, which will give away
  // ProfileBufferChunks that can contain 128 bytes, using up to 1KB of memory
  // (including usable 128 bytes and headers).
  constexpr size_t bufferMaxSize = 1024;
  ProfileBufferChunkManagerWithLocalLimit cmTarget(bufferMaxSize, chunkMinSize);
  ProfileChunkedBuffer cbTarget(ProfileChunkedBuffer::ThreadSafety::WithMutex,
                                cmTarget);

  // It should start empty.
  cbTarget.ReadEach(
      [](ProfileBufferEntryReader&) { MOZ_RELEASE_ASSERT(false); });

  // Copy the contents from cbSingle to cbTarget.
  cbTarget.AppendContents(cbSingle);

  // And verify that we now have the same contents in cbTarget.
  read = 0;
  cbTarget.ReadEach(
      [&](ProfileBufferEntryReader& er, ProfileBufferBlockIndex blockIndex) {
        ++read;
        MOZ_RELEASE_ASSERT(er.RemainingBytes() == sizeof(size_t));
        const auto value = er.ReadObject<size_t>();
        MOZ_RELEASE_ASSERT(value == read);
        MOZ_RELEASE_ASSERT(er.RemainingBytes() == 0);
        MOZ_RELEASE_ASSERT(blockIndex <= lastBlockIndex,
                           "Unexpected block index past the last written one");
      });
  MOZ_RELEASE_ASSERT(read == firstIndexToFail - 1,
                     "We should have read up to before the first failure");

#  ifdef DEBUG
  // cbSingle.Dump();
  // cbTarget.Dump();
#  endif

  printf("TestChunkedBufferSingle done\n");
}

static void TestModuloBuffer(ModuloBuffer<>& mb, uint32_t MBSize) {
  using MB = ModuloBuffer<>;

  MOZ_RELEASE_ASSERT(mb.BufferLength().Value() == MBSize);

  // Iterator comparisons.
  MOZ_RELEASE_ASSERT(mb.ReaderAt(2) == mb.ReaderAt(2));
  MOZ_RELEASE_ASSERT(mb.ReaderAt(2) != mb.ReaderAt(3));
  MOZ_RELEASE_ASSERT(mb.ReaderAt(2) < mb.ReaderAt(3));
  MOZ_RELEASE_ASSERT(mb.ReaderAt(2) <= mb.ReaderAt(2));
  MOZ_RELEASE_ASSERT(mb.ReaderAt(2) <= mb.ReaderAt(3));
  MOZ_RELEASE_ASSERT(mb.ReaderAt(3) > mb.ReaderAt(2));
  MOZ_RELEASE_ASSERT(mb.ReaderAt(2) >= mb.ReaderAt(2));
  MOZ_RELEASE_ASSERT(mb.ReaderAt(3) >= mb.ReaderAt(2));

  // Iterators indices don't wrap around (even though they may be pointing at
  // the same location).
  MOZ_RELEASE_ASSERT(mb.ReaderAt(2) != mb.ReaderAt(MBSize + 2));
  MOZ_RELEASE_ASSERT(mb.ReaderAt(MBSize + 2) != mb.ReaderAt(2));

  // Dereference.
  static_assert(std::is_same<decltype(*mb.ReaderAt(0)), const MB::Byte&>::value,
                "Dereferencing from a reader should return const Byte*");
  static_assert(std::is_same<decltype(*mb.WriterAt(0)), MB::Byte&>::value,
                "Dereferencing from a writer should return Byte*");
  // Contiguous between 0 and MBSize-1.
  MOZ_RELEASE_ASSERT(&*mb.ReaderAt(MBSize - 1) ==
                     &*mb.ReaderAt(0) + (MBSize - 1));
  // Wraps around.
  MOZ_RELEASE_ASSERT(&*mb.ReaderAt(MBSize) == &*mb.ReaderAt(0));
  MOZ_RELEASE_ASSERT(&*mb.ReaderAt(MBSize + MBSize - 1) ==
                     &*mb.ReaderAt(MBSize - 1));
  MOZ_RELEASE_ASSERT(&*mb.ReaderAt(MBSize + MBSize) == &*mb.ReaderAt(0));
  // Power of 2 modulo wrapping.
  MOZ_RELEASE_ASSERT(&*mb.ReaderAt(uint32_t(-1)) == &*mb.ReaderAt(MBSize - 1));
  MOZ_RELEASE_ASSERT(&*mb.ReaderAt(static_cast<MB::Index>(-1)) ==
                     &*mb.ReaderAt(MBSize - 1));

  // Arithmetic.
  MB::Reader arit = mb.ReaderAt(0);
  MOZ_RELEASE_ASSERT(++arit == mb.ReaderAt(1));
  MOZ_RELEASE_ASSERT(arit == mb.ReaderAt(1));

  MOZ_RELEASE_ASSERT(--arit == mb.ReaderAt(0));
  MOZ_RELEASE_ASSERT(arit == mb.ReaderAt(0));

  MOZ_RELEASE_ASSERT(arit++ == mb.ReaderAt(0));
  MOZ_RELEASE_ASSERT(arit == mb.ReaderAt(1));

  MOZ_RELEASE_ASSERT(arit-- == mb.ReaderAt(1));
  MOZ_RELEASE_ASSERT(arit == mb.ReaderAt(0));

  MOZ_RELEASE_ASSERT(arit + 3 == mb.ReaderAt(3));
  MOZ_RELEASE_ASSERT(arit == mb.ReaderAt(0));

  MOZ_RELEASE_ASSERT(4 + arit == mb.ReaderAt(4));
  MOZ_RELEASE_ASSERT(arit == mb.ReaderAt(0));

  // (Can't have assignments inside asserts, hence the split.)
  const bool checkPlusEq = ((arit += 3) == mb.ReaderAt(3));
  MOZ_RELEASE_ASSERT(checkPlusEq);
  MOZ_RELEASE_ASSERT(arit == mb.ReaderAt(3));

  MOZ_RELEASE_ASSERT((arit - 2) == mb.ReaderAt(1));
  MOZ_RELEASE_ASSERT(arit == mb.ReaderAt(3));

  const bool checkMinusEq = ((arit -= 2) == mb.ReaderAt(1));
  MOZ_RELEASE_ASSERT(checkMinusEq);
  MOZ_RELEASE_ASSERT(arit == mb.ReaderAt(1));

  // Random access.
  MOZ_RELEASE_ASSERT(&arit[3] == &*(arit + 3));
  MOZ_RELEASE_ASSERT(arit == mb.ReaderAt(1));

  // Iterator difference.
  MOZ_RELEASE_ASSERT(mb.ReaderAt(3) - mb.ReaderAt(1) == 2);
  MOZ_RELEASE_ASSERT(mb.ReaderAt(1) - mb.ReaderAt(3) == MB::Index(-2));

  // Only testing Writer, as Reader is just a subset with no code differences.
  MB::Writer it = mb.WriterAt(0);
  MOZ_RELEASE_ASSERT(it.CurrentIndex() == 0);

  // Write two characters at the start.
  it.WriteObject('x');
  it.WriteObject('y');

  // Backtrack to read them.
  it -= 2;
  // PeekObject should read without moving.
  MOZ_RELEASE_ASSERT(it.PeekObject<char>() == 'x');
  MOZ_RELEASE_ASSERT(it.CurrentIndex() == 0);
  // ReadObject should read and move past the character.
  MOZ_RELEASE_ASSERT(it.ReadObject<char>() == 'x');
  MOZ_RELEASE_ASSERT(it.CurrentIndex() == 1);
  MOZ_RELEASE_ASSERT(it.PeekObject<char>() == 'y');
  MOZ_RELEASE_ASSERT(it.CurrentIndex() == 1);
  MOZ_RELEASE_ASSERT(it.ReadObject<char>() == 'y');
  MOZ_RELEASE_ASSERT(it.CurrentIndex() == 2);

  // Checking that a reader can be created from a writer.
  MB::Reader it2(it);
  MOZ_RELEASE_ASSERT(it2.CurrentIndex() == 2);
  // Or assigned.
  it2 = it;
  MOZ_RELEASE_ASSERT(it2.CurrentIndex() == 2);

  // Iterator traits.
  static_assert(std::is_same<std::iterator_traits<MB::Reader>::difference_type,
                             MB::Index>::value,
                "ModuloBuffer::Reader::difference_type should be Index");
  static_assert(std::is_same<std::iterator_traits<MB::Reader>::value_type,
                             MB::Byte>::value,
                "ModuloBuffer::Reader::value_type should be Byte");
  static_assert(std::is_same<std::iterator_traits<MB::Reader>::pointer,
                             const MB::Byte*>::value,
                "ModuloBuffer::Reader::pointer should be const Byte*");
  static_assert(std::is_same<std::iterator_traits<MB::Reader>::reference,
                             const MB::Byte&>::value,
                "ModuloBuffer::Reader::reference should be const Byte&");
  static_assert(std::is_base_of<
                    std::input_iterator_tag,
                    std::iterator_traits<MB::Reader>::iterator_category>::value,
                "ModuloBuffer::Reader::iterator_category should be derived "
                "from input_iterator_tag");
  static_assert(std::is_base_of<
                    std::forward_iterator_tag,
                    std::iterator_traits<MB::Reader>::iterator_category>::value,
                "ModuloBuffer::Reader::iterator_category should be derived "
                "from forward_iterator_tag");
  static_assert(std::is_base_of<
                    std::bidirectional_iterator_tag,
                    std::iterator_traits<MB::Reader>::iterator_category>::value,
                "ModuloBuffer::Reader::iterator_category should be derived "
                "from bidirectional_iterator_tag");
  static_assert(
      std::is_same<std::iterator_traits<MB::Reader>::iterator_category,
                   std::random_access_iterator_tag>::value,
      "ModuloBuffer::Reader::iterator_category should be "
      "random_access_iterator_tag");

  // Use as input iterator by std::string constructor (which is only considered
  // with proper input iterators.)
  std::string s(mb.ReaderAt(0), mb.ReaderAt(2));
  MOZ_RELEASE_ASSERT(s == "xy");

  // Write 4-byte number at index 2.
  it.WriteObject(int32_t(123));
  MOZ_RELEASE_ASSERT(it.CurrentIndex() == 6);
  // And another, which should now wrap around (but index continues on.)
  it.WriteObject(int32_t(456));
  MOZ_RELEASE_ASSERT(it.CurrentIndex() == MBSize + 2);
  // Even though index==MBSize+2, we can read the object we wrote at 2.
  MOZ_RELEASE_ASSERT(it.ReadObject<int32_t>() == 123);
  MOZ_RELEASE_ASSERT(it.CurrentIndex() == MBSize + 6);
  // And similarly, index MBSize+6 points at the same location as index 6.
  MOZ_RELEASE_ASSERT(it.ReadObject<int32_t>() == 456);
  MOZ_RELEASE_ASSERT(it.CurrentIndex() == MBSize + MBSize + 2);
}

void TestModuloBuffer() {
  printf("TestModuloBuffer...\n");

  // Testing ModuloBuffer with default template arguments.
  using MB = ModuloBuffer<>;

  // Only 8-byte buffers, to easily test wrap-around.
  constexpr uint32_t MBSize = 8;

  // MB with self-allocated heap buffer.
  MB mbByLength(MakePowerOfTwo32<MBSize>());
  TestModuloBuffer(mbByLength, MBSize);

  // MB taking ownership of a provided UniquePtr to a buffer.
  auto uniqueBuffer = MakeUnique<uint8_t[]>(MBSize);
  MB mbByUniquePtr(MakeUnique<uint8_t[]>(MBSize), MakePowerOfTwo32<MBSize>());
  TestModuloBuffer(mbByUniquePtr, MBSize);

  // MB using part of a buffer on the stack. The buffer is three times the
  // required size: The middle third is where ModuloBuffer will work, the first
  // and last thirds are only used to later verify that ModuloBuffer didn't go
  // out of its bounds.
  uint8_t buffer[MBSize * 3];
  // Pre-fill the buffer with a known pattern, so we can later see what changed.
  for (size_t i = 0; i < MBSize * 3; ++i) {
    buffer[i] = uint8_t('A' + i);
  }
  MB mbByBuffer(&buffer[MBSize], MakePowerOfTwo32<MBSize>());
  TestModuloBuffer(mbByBuffer, MBSize);

  // Check that only the provided stack-based sub-buffer was modified.
  uint32_t changed = 0;
  for (size_t i = MBSize; i < MBSize * 2; ++i) {
    changed += (buffer[i] == uint8_t('A' + i)) ? 0 : 1;
  }
  // Expect at least 75% changes.
  MOZ_RELEASE_ASSERT(changed >= MBSize * 6 / 8);

  // Everything around the sub-buffer should be unchanged.
  for (size_t i = 0; i < MBSize; ++i) {
    MOZ_RELEASE_ASSERT(buffer[i] == uint8_t('A' + i));
  }
  for (size_t i = MBSize * 2; i < MBSize * 3; ++i) {
    MOZ_RELEASE_ASSERT(buffer[i] == uint8_t('A' + i));
  }

  // Check that move-construction is allowed. This verifies that we do not
  // crash from a double free, when `mbByBuffer` and `mbByStolenBuffer` are both
  // destroyed at the end of this function.
  MB mbByStolenBuffer = std::move(mbByBuffer);
  TestModuloBuffer(mbByStolenBuffer, MBSize);

  // Check that only the provided stack-based sub-buffer was modified.
  changed = 0;
  for (size_t i = MBSize; i < MBSize * 2; ++i) {
    changed += (buffer[i] == uint8_t('A' + i)) ? 0 : 1;
  }
  // Expect at least 75% changes.
  MOZ_RELEASE_ASSERT(changed >= MBSize * 6 / 8);

  // Everything around the sub-buffer should be unchanged.
  for (size_t i = 0; i < MBSize; ++i) {
    MOZ_RELEASE_ASSERT(buffer[i] == uint8_t('A' + i));
  }
  for (size_t i = MBSize * 2; i < MBSize * 3; ++i) {
    MOZ_RELEASE_ASSERT(buffer[i] == uint8_t('A' + i));
  }

  // This test function does a `ReadInto` as directed, and checks that the
  // result is the same as if the copy had been done manually byte-by-byte.
  // `TestReadInto(3, 7, 2)` copies from index 3 to index 7, 2 bytes long.
  // Return the output string (from `ReadInto`) for external checks.
  auto TestReadInto = [](MB::Index aReadFrom, MB::Index aWriteTo,
                         MB::Length aBytes) {
    constexpr uint32_t TRISize = 16;

    // Prepare an input buffer, all different elements.
    uint8_t input[TRISize + 1] = "ABCDEFGHIJKLMNOP";
    const MB mbInput(input, MakePowerOfTwo32<TRISize>());

    // Prepare an output buffer, different from input.
    uint8_t output[TRISize + 1] = "abcdefghijklmnop";
    MB mbOutput(output, MakePowerOfTwo32<TRISize>());

    // Run ReadInto.
    auto writer = mbOutput.WriterAt(aWriteTo);
    mbInput.ReaderAt(aReadFrom).ReadInto(writer, aBytes);

    // Do the same operation manually.
    uint8_t outputCheck[TRISize + 1] = "abcdefghijklmnop";
    MB mbOutputCheck(outputCheck, MakePowerOfTwo32<TRISize>());
    auto readerCheck = mbInput.ReaderAt(aReadFrom);
    auto writerCheck = mbOutputCheck.WriterAt(aWriteTo);
    for (MB::Length i = 0; i < aBytes; ++i) {
      *writerCheck++ = *readerCheck++;
    }

    // Compare the two outputs.
    for (uint32_t i = 0; i < TRISize; ++i) {
#  ifdef TEST_MODULOBUFFER_FAILURE_DEBUG
      // Only used when debugging failures.
      if (output[i] != outputCheck[i]) {
        printf(
            "*** from=%u to=%u bytes=%u i=%u\ninput:  '%s'\noutput: "
            "'%s'\ncheck:  '%s'\n",
            unsigned(aReadFrom), unsigned(aWriteTo), unsigned(aBytes),
            unsigned(i), input, output, outputCheck);
      }
#  endif
      MOZ_RELEASE_ASSERT(output[i] == outputCheck[i]);
    }

#  ifdef TEST_MODULOBUFFER_HELPER
    // Only used when adding more tests.
    printf("*** from=%u to=%u bytes=%u output: %s\n", unsigned(aReadFrom),
           unsigned(aWriteTo), unsigned(aBytes), output);
#  endif

    return std::string(reinterpret_cast<const char*>(output));
  };

  // A few manual checks:
  constexpr uint32_t TRISize = 16;
  MOZ_RELEASE_ASSERT(TestReadInto(0, 0, 0) == "abcdefghijklmnop");
  MOZ_RELEASE_ASSERT(TestReadInto(0, 0, TRISize) == "ABCDEFGHIJKLMNOP");
  MOZ_RELEASE_ASSERT(TestReadInto(0, 5, TRISize) == "LMNOPABCDEFGHIJK");
  MOZ_RELEASE_ASSERT(TestReadInto(5, 0, TRISize) == "FGHIJKLMNOPABCDE");

  // Test everything! (16^3 = 4096, not too much.)
  for (MB::Index r = 0; r < TRISize; ++r) {
    for (MB::Index w = 0; w < TRISize; ++w) {
      for (MB::Length len = 0; len < TRISize; ++len) {
        TestReadInto(r, w, len);
      }
    }
  }

  printf("TestModuloBuffer done\n");
}

void TestProfilerDependencies() {
  TestPowerOfTwoMask();
  TestPowerOfTwo();
  TestLEB128();
  TestChunk();
  TestChunkManagerSingle();
  TestChunkManagerWithLocalLimit();
  TestControlledChunkManagerUpdate();
  TestControlledChunkManagerWithLocalLimit();
  TestChunkedBuffer();
  TestChunkedBufferSingle();
  TestModuloBuffer();
}

class BaseTestMarkerPayload : public baseprofiler::ProfilerMarkerPayload {
 public:
  explicit BaseTestMarkerPayload(int aData) : mData(aData) {}

  int GetData() const { return mData; }

  // Exploded DECL_BASE_STREAM_PAYLOAD, but without `MFBT_API`s.
  static UniquePtr<ProfilerMarkerPayload> Deserialize(
      ProfileBufferEntryReader& aEntryReader);
  ProfileBufferEntryWriter::Length TagAndSerializationBytes() const override;
  void SerializeTagAndPayload(
      ProfileBufferEntryWriter& aEntryWriter) const override;
  void StreamPayload(
      ::mozilla::baseprofiler::SpliceableJSONWriter& aWriter,
      const ::mozilla::TimeStamp& aProcessStartTime,
      ::mozilla::baseprofiler::UniqueStacks& aUniqueStacks) const override;

 private:
  BaseTestMarkerPayload(CommonProps&& aProps, int aData)
      : baseprofiler::ProfilerMarkerPayload(std::move(aProps)), mData(aData) {}

  int mData;
};

// static
UniquePtr<baseprofiler::ProfilerMarkerPayload>
BaseTestMarkerPayload::Deserialize(ProfileBufferEntryReader& aEntryReader) {
  CommonProps props = DeserializeCommonProps(aEntryReader);
  int data = aEntryReader.ReadObject<int>();
  return UniquePtr<baseprofiler::ProfilerMarkerPayload>(
      new BaseTestMarkerPayload(std::move(props), data));
}

ProfileBufferEntryWriter::Length
BaseTestMarkerPayload::TagAndSerializationBytes() const {
  return CommonPropsTagAndSerializationBytes() + sizeof(int);
}

void BaseTestMarkerPayload::SerializeTagAndPayload(
    ProfileBufferEntryWriter& aEntryWriter) const {
  static const DeserializerTag tag = TagForDeserializer(Deserialize);
  SerializeTagAndCommonProps(tag, aEntryWriter);
  aEntryWriter.WriteObject(mData);
}

void BaseTestMarkerPayload::StreamPayload(
    baseprofiler::SpliceableJSONWriter& aWriter,
    const TimeStamp& aProcessStartTime,
    baseprofiler::UniqueStacks& aUniqueStacks) const {
  aWriter.IntProperty("data", mData);
}

void TestProfilerMarkerSerialization() {
  printf("TestProfilerMarkerSerialization...\n");

  constexpr uint32_t MBSize = 256;
  uint8_t buffer[MBSize * 3];
  for (size_t i = 0; i < MBSize * 3; ++i) {
    buffer[i] = uint8_t('A' + i);
  }
  BlocksRingBuffer rb(BlocksRingBuffer::ThreadSafety::WithMutex,
                      &buffer[MBSize], MakePowerOfTwo32<MBSize>());

  constexpr int data = 42;
  {
    BaseTestMarkerPayload payload(data);
    rb.PutObject(
        static_cast<const baseprofiler::ProfilerMarkerPayload*>(&payload));
  }

  int read = 0;
  rb.ReadEach([&](ProfileBufferEntryReader& aER) {
    UniquePtr<baseprofiler::ProfilerMarkerPayload> payload =
        aER.ReadObject<UniquePtr<baseprofiler::ProfilerMarkerPayload>>();
    MOZ_RELEASE_ASSERT(!!payload);
    ++read;
    BaseTestMarkerPayload* testPayload =
        static_cast<BaseTestMarkerPayload*>(payload.get());
    MOZ_RELEASE_ASSERT(testPayload);
    MOZ_RELEASE_ASSERT(testPayload->GetData() == data);
  });
  MOZ_RELEASE_ASSERT(read == 1);

  // Everything around the sub-buffer should be unchanged.
  for (size_t i = 0; i < MBSize; ++i) {
    MOZ_RELEASE_ASSERT(buffer[i] == uint8_t('A' + i));
  }
  for (size_t i = MBSize * 2; i < MBSize * 3; ++i) {
    MOZ_RELEASE_ASSERT(buffer[i] == uint8_t('A' + i));
  }

  printf("TestProfilerMarkerSerialization done\n");
}

// Increase the depth, to a maximum (to avoid too-deep recursion).
static constexpr size_t NextDepth(size_t aDepth) {
  constexpr size_t MAX_DEPTH = 128;
  return (aDepth < MAX_DEPTH) ? (aDepth + 1) : aDepth;
}

Atomic<bool, Relaxed, recordreplay::Behavior::DontPreserve> sStopFibonacci;

// Compute fibonacci the hard way (recursively: `f(n)=f(n-1)+f(n-2)`), and
// prevent inlining.
// The template parameter makes each depth be a separate function, to better
// distinguish them in the profiler output.
template <size_t DEPTH = 0>
MOZ_NEVER_INLINE unsigned long long Fibonacci(unsigned long long n) {
  AUTO_BASE_PROFILER_LABEL_DYNAMIC_STRING("fib", OTHER, std::to_string(DEPTH));
  if (n == 0) {
    return 0;
  }
  if (n == 1) {
    return 1;
  }
  if (DEPTH < 5 && sStopFibonacci) {
    return 1'000'000'000;
  }
  TimeStamp start = TimeStamp::NowUnfuzzed();
  static constexpr size_t MAX_MARKER_DEPTH = 10;
  unsigned long long f2 = Fibonacci<NextDepth(DEPTH)>(n - 2);
  if (DEPTH == 0) {
    BASE_PROFILER_ADD_MARKER("Half-way through Fibonacci", OTHER);
  }
  unsigned long long f1 = Fibonacci<NextDepth(DEPTH)>(n - 1);
  if (DEPTH < MAX_MARKER_DEPTH) {
    baseprofiler::profiler_add_text_marker(
        "fib", std::to_string(DEPTH),
        baseprofiler::ProfilingCategoryPair::OTHER, start,
        TimeStamp::NowUnfuzzed());
  }
  return f2 + f1;
}

void TestProfiler() {
  printf("TestProfiler starting -- pid: %d, tid: %d\n",
         baseprofiler::profiler_current_process_id(),
         baseprofiler::profiler_current_thread_id());
  // ::SleepMilli(10000);

  TestProfilerDependencies();

  TestProfilerMarkerSerialization();

  {
    printf("profiler_init()...\n");
    AUTO_BASE_PROFILER_INIT;

    MOZ_RELEASE_ASSERT(!baseprofiler::profiler_is_active());
    MOZ_RELEASE_ASSERT(!baseprofiler::profiler_thread_is_being_profiled());
    MOZ_RELEASE_ASSERT(!baseprofiler::profiler_thread_is_sleeping());

    printf("profiler_start()...\n");
    Vector<const char*> filters;
    // Profile all registered threads.
    MOZ_RELEASE_ASSERT(filters.append(""));
    const uint32_t features = baseprofiler::ProfilerFeature::Leaf |
                              baseprofiler::ProfilerFeature::StackWalk |
                              baseprofiler::ProfilerFeature::Threads;
    baseprofiler::profiler_start(baseprofiler::BASE_PROFILER_DEFAULT_ENTRIES,
                                 BASE_PROFILER_DEFAULT_INTERVAL, features,
                                 filters.begin(), filters.length());

    MOZ_RELEASE_ASSERT(baseprofiler::profiler_is_active());
    MOZ_RELEASE_ASSERT(baseprofiler::profiler_thread_is_being_profiled());
    MOZ_RELEASE_ASSERT(!baseprofiler::profiler_thread_is_sleeping());

    sStopFibonacci = false;

    std::thread threadFib([]() {
      AUTO_BASE_PROFILER_REGISTER_THREAD("fibonacci");
      SleepMilli(5);
      auto cause =
#  if defined(__linux__) || defined(__ANDROID__)
          // Currently disabled on these platforms, so just return a null.
          decltype(baseprofiler::profiler_get_backtrace()){};
#  else
          baseprofiler::profiler_get_backtrace();
#  endif
      AUTO_BASE_PROFILER_TEXT_MARKER_CAUSE("fibonacci", "First leaf call",
                                           OTHER, std::move(cause));
      static const unsigned long long fibStart = 37;
      printf("Fibonacci(%llu)...\n", fibStart);
      AUTO_BASE_PROFILER_LABEL("Label around Fibonacci", OTHER);
      unsigned long long f = Fibonacci(fibStart);
      printf("Fibonacci(%llu) = %llu\n", fibStart, f);
    });

    std::thread threadCancelFib([]() {
      AUTO_BASE_PROFILER_REGISTER_THREAD("fibonacci canceller");
      SleepMilli(5);
      AUTO_BASE_PROFILER_TEXT_MARKER_CAUSE("fibonacci", "Canceller", OTHER,
                                           nullptr);
      static const int waitMaxSeconds = 10;
      for (int i = 0; i < waitMaxSeconds; ++i) {
        if (sStopFibonacci) {
          AUTO_BASE_PROFILER_LABEL_DYNAMIC_STRING("fibCancel", OTHER,
                                                  std::to_string(i));
          return;
        }
        AUTO_BASE_PROFILER_THREAD_SLEEP;
        SleepMilli(1000);
      }
      AUTO_BASE_PROFILER_LABEL_DYNAMIC_STRING("fibCancel", OTHER,
                                              "Cancelling!");
      sStopFibonacci = true;
    });

    {
      AUTO_BASE_PROFILER_TEXT_MARKER_CAUSE(
          "main thread", "joining fibonacci thread", OTHER, nullptr);
      AUTO_BASE_PROFILER_THREAD_SLEEP;
      threadFib.join();
    }

    {
      AUTO_BASE_PROFILER_TEXT_MARKER_CAUSE(
          "main thread", "joining fibonacci-canceller thread", OTHER, nullptr);
      sStopFibonacci = true;
      AUTO_BASE_PROFILER_THREAD_SLEEP;
      threadCancelFib.join();
    }

    // Just making sure all payloads know how to (de)serialize and stream.
    baseprofiler::profiler_add_marker(
        "TracingMarkerPayload", baseprofiler::ProfilingCategoryPair::OTHER,
        baseprofiler::TracingMarkerPayload("category",
                                           baseprofiler::TRACING_EVENT));

    auto cause =
#  if defined(__linux__) || defined(__ANDROID__)
        // Currently disabled on these platforms, so just return a null.
        decltype(baseprofiler::profiler_get_backtrace()){};
#  else
        baseprofiler::profiler_get_backtrace();
#  endif
    baseprofiler::profiler_add_marker(
        "FileIOMarkerPayload", baseprofiler::ProfilingCategoryPair::OTHER,
        baseprofiler::FileIOMarkerPayload(
            "operation", "source", "filename", TimeStamp::NowUnfuzzed(),
            TimeStamp::NowUnfuzzed(), std::move(cause)));

    baseprofiler::profiler_add_marker(
        "UserTimingMarkerPayload", baseprofiler::ProfilingCategoryPair::OTHER,
        baseprofiler::UserTimingMarkerPayload("name", TimeStamp::NowUnfuzzed(),
                                              Nothing{}, Nothing{}));

    baseprofiler::profiler_add_marker(
        "HangMarkerPayload", baseprofiler::ProfilingCategoryPair::OTHER,
        baseprofiler::HangMarkerPayload(TimeStamp::NowUnfuzzed(),
                                        TimeStamp::NowUnfuzzed()));

    baseprofiler::profiler_add_marker(
        "LongTaskMarkerPayload", baseprofiler::ProfilingCategoryPair::OTHER,
        baseprofiler::LongTaskMarkerPayload(TimeStamp::NowUnfuzzed(),
                                            TimeStamp::NowUnfuzzed()));

    {
      std::string s = "text payload";
      baseprofiler::profiler_add_marker(
          "TextMarkerPayload", baseprofiler::ProfilingCategoryPair::OTHER,
          baseprofiler::TextMarkerPayload(s, TimeStamp::NowUnfuzzed(),
                                          TimeStamp::NowUnfuzzed()));
    }

    baseprofiler::profiler_add_marker(
        "LogMarkerPayload", baseprofiler::ProfilingCategoryPair::OTHER,
        baseprofiler::LogMarkerPayload("module", "text",
                                       TimeStamp::NowUnfuzzed()));

    printf("Sleep 1s...\n");
    {
      AUTO_BASE_PROFILER_THREAD_SLEEP;
      SleepMilli(1000);
    }

    Maybe<baseprofiler::ProfilerBufferInfo> info =
        baseprofiler::profiler_get_buffer_info();
    MOZ_RELEASE_ASSERT(info.isSome());
    printf("Profiler buffer range: %llu .. %llu (%llu bytes)\n",
           static_cast<unsigned long long>(info->mRangeStart),
           static_cast<unsigned long long>(info->mRangeEnd),
           // sizeof(ProfileBufferEntry) == 9
           (static_cast<unsigned long long>(info->mRangeEnd) -
            static_cast<unsigned long long>(info->mRangeStart)) *
               9);
    printf("Stats:         min(ns) .. mean(ns) .. max(ns)  [count]\n");
    printf("- Intervals:   %7.1f .. %7.1f  .. %7.1f  [%u]\n",
           info->mIntervalsNs.min,
           info->mIntervalsNs.sum / info->mIntervalsNs.n,
           info->mIntervalsNs.max, info->mIntervalsNs.n);
    printf("- Overheads:   %7.1f .. %7.1f  .. %7.1f  [%u]\n",
           info->mOverheadsNs.min,
           info->mOverheadsNs.sum / info->mOverheadsNs.n,
           info->mOverheadsNs.max, info->mOverheadsNs.n);
    printf("  - Locking:   %7.1f .. %7.1f  .. %7.1f  [%u]\n",
           info->mLockingsNs.min, info->mLockingsNs.sum / info->mLockingsNs.n,
           info->mLockingsNs.max, info->mLockingsNs.n);
    printf("  - Clearning: %7.1f .. %7.1f  .. %7.1f  [%u]\n",
           info->mCleaningsNs.min,
           info->mCleaningsNs.sum / info->mCleaningsNs.n,
           info->mCleaningsNs.max, info->mCleaningsNs.n);
    printf("  - Counters:  %7.1f .. %7.1f  .. %7.1f  [%u]\n",
           info->mCountersNs.min, info->mCountersNs.sum / info->mCountersNs.n,
           info->mCountersNs.max, info->mCountersNs.n);
    printf("  - Threads:   %7.1f .. %7.1f  .. %7.1f  [%u]\n",
           info->mThreadsNs.min, info->mThreadsNs.sum / info->mThreadsNs.n,
           info->mThreadsNs.max, info->mThreadsNs.n);

    printf("baseprofiler_save_profile_to_file()...\n");
    baseprofiler::profiler_save_profile_to_file("TestProfiler_profile.json");

    printf("profiler_stop()...\n");
    baseprofiler::profiler_stop();

    MOZ_RELEASE_ASSERT(!baseprofiler::profiler_is_active());
    MOZ_RELEASE_ASSERT(!baseprofiler::profiler_thread_is_being_profiled());
    MOZ_RELEASE_ASSERT(!baseprofiler::profiler_thread_is_sleeping());

    printf("profiler_shutdown()...\n");
  }

  printf("TestProfiler done\n");
}

#else  // MOZ_GECKO_PROFILER

// Testing that macros are still #defined (but do nothing) when
// MOZ_GECKO_PROFILER is disabled.
void TestProfiler() {
  // These don't need to make sense, we just want to know that they're defined
  // and don't do anything.
  AUTO_BASE_PROFILER_INIT;

  // This wouldn't build if the macro did output its arguments.
  AUTO_BASE_PROFILER_TEXT_MARKER_CAUSE(catch, catch, catch, catch);

  AUTO_BASE_PROFILER_LABEL(catch, catch);

  AUTO_BASE_PROFILER_THREAD_SLEEP;
}

#endif  // MOZ_GECKO_PROFILER else

int main() {
#ifdef MOZ_GECKO_PROFILER
  printf("BaseTestProfiler -- pid: %d, tid: %d\n",
         baseprofiler::profiler_current_process_id(),
         baseprofiler::profiler_current_thread_id());
  // ::SleepMilli(10000);
#endif  // MOZ_GECKO_PROFILER

  // Note that there are two `TestProfiler` functions above, depending on
  // whether MOZ_GECKO_PROFILER is #defined.
  TestProfiler();

  return 0;
}

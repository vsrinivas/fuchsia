// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_MESSAGE_STORAGE_H_
#define LIB_FIDL_LLCPP_MESSAGE_STORAGE_H_

#include <lib/fidl/cpp/message_part.h>
#include <lib/fidl/llcpp/traits.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>

namespace fidl {
namespace internal {

// An uninitialized array of |kSize| bytes, guaranteed to follow FIDL alignment.
template <uint32_t kSize>
struct AlignedBuffer {
  AlignedBuffer() {}

  fidl::BytePart view() { return fidl::BytePart(data_, kSize); }
  uint8_t* data() { return data_; }

 private:
  FIDL_ALIGNDECL uint8_t data_[kSize];
};

static_assert(alignof(std::max_align_t) % FIDL_ALIGNMENT == 0,
              "AlignedBuffer should follow FIDL alignment when allocated on the heap.");

// The largest acceptable size for a stack-allocated buffer.
// Messages which are smaller than/equal to this threshold are stack-allocated,
// whereas messages greater than this threshold are heap allocated.
// This constant has therefore a potentially large impact on the behavior of programs built
// on top of the LLCPP bindings, and modification should be done with great care.
//
// July 2019: initial value set at 512 due to Chrome's restriction that the largest stack object
// tolerated is 512 bytes. For reference, the default stack size on Fuchsia is 256kb.
constexpr uint32_t kMaxStackAllocSize = 512;

// |ByteStorage| allocates a buffer either inline or on the heap, depending on the magnitude
// of |kSize| relative to |kMaxStackAllocSize|.
template <uint32_t kSize, typename Enabled = void>
struct ByteStorage;

// A tag to delay allocation when passed to the constructor of |ByteStorage|.
// The caller should then invoke |Allocate| explicitly at a later point.
struct DelayAllocationTag {};
constexpr DelayAllocationTag DelayAllocation = DelayAllocationTag{};

// This definition is selected when the size is larger than |kMaxStackAllocSize|.
template <uint32_t kSize>
struct ByteStorage<kSize, std::enable_if_t<(kSize > kMaxStackAllocSize)>> {
  constexpr static bool kWillCopyBufferDuringMove = false;
  constexpr static uint32_t kBufferSize = kSize;

  fidl::BytePart buffer() { return storage->view(); }
  uint8_t* data() { return storage->data(); }

  ByteStorage() : storage(std::make_unique<AlignedBuffer<kBufferSize>>()) {}
  explicit ByteStorage(DelayAllocationTag) {}
  ~ByteStorage() = default;

  void Allocate() { storage = std::make_unique<AlignedBuffer<kBufferSize>>(); }

  ByteStorage(const ByteStorage&) = delete;
  ByteStorage& operator=(const ByteStorage&) = delete;

  ByteStorage(ByteStorage&&) = default;
  ByteStorage& operator=(ByteStorage&&) = default;

 private:
  std::unique_ptr<AlignedBuffer<kBufferSize>> storage = {};
};

// This definition is selected when the size is less than or equal to |kMaxStackAllocSize|.
template <uint32_t kSize>
struct ByteStorage<kSize, std::enable_if_t<(kSize <= kMaxStackAllocSize)>> {
  constexpr static bool kWillCopyBufferDuringMove = true;
  constexpr static uint32_t kBufferSize = kSize;

  fidl::BytePart buffer() { return storage.view(); }
  uint8_t* data() { return storage.data(); }

  ByteStorage() {}
  explicit ByteStorage(DelayAllocationTag) {}
  ~ByteStorage() = default;

  void Allocate() { /* No-op when |storage| is in stack, since everything is already allocated */
  }

  ByteStorage(const ByteStorage&) = delete;
  ByteStorage& operator=(const ByteStorage&) = delete;

  ByteStorage(ByteStorage&&) = default;
  ByteStorage& operator=(ByteStorage&&) = default;

 private:
  AlignedBuffer<kBufferSize> storage;
};

// |ResponseStorage| allocates a buffer either inline or on the heap, depending on the
// maximum wire-format size of that particular FidlType. FidlType should be a response message.
template <typename FidlType>
struct ResponseStorage
    : private ByteStorage<ClampedMessageSize<FidlType, MessageDirection::kReceiving>()> {
 private:
  using Super = ByteStorage<ClampedMessageSize<FidlType, MessageDirection::kReceiving>()>;

 public:
  using Super::buffer;
  using Super::kBufferSize;
  using Super::kWillCopyBufferDuringMove;

  ResponseStorage() = default;
  ~ResponseStorage() = default;

  ResponseStorage(const ResponseStorage&) = delete;
  ResponseStorage& operator=(const ResponseStorage&) = delete;

  ResponseStorage(ResponseStorage&&) = default;
  ResponseStorage& operator=(ResponseStorage&&) = default;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_MESSAGE_STORAGE_H_

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_MESSAGE_STORAGE_H_
#define LIB_FIDL_LLCPP_MESSAGE_STORAGE_H_

#include <lib/fidl/llcpp/traits.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <type_traits>

namespace fidl {

// Holds a reference to any storage buffer. This is independent of the allocation.
struct BufferSpan {
  BufferSpan() = default;
  BufferSpan(uint8_t* data, uint32_t capacity) : data(data), capacity(capacity) {}

  uint8_t* data = nullptr;
  uint32_t capacity = 0;
};

namespace internal {

// An stack allocated uninitialized array of |kSize| bytes, guaranteed to follow FIDL alignment.
template <size_t kSize>
struct InlineMessageBuffer {
  static_assert(kSize % FIDL_ALIGNMENT == 0, "kSize must be FIDL-aligned");

  InlineMessageBuffer() {}
  InlineMessageBuffer(InlineMessageBuffer&&) = delete;
  InlineMessageBuffer(const InlineMessageBuffer&) = delete;
  InlineMessageBuffer& operator=(InlineMessageBuffer&&) = delete;
  InlineMessageBuffer& operator=(const InlineMessageBuffer&) = delete;

  BufferSpan view() { return BufferSpan(data(), kSize); }
  uint8_t* data() { return data_; }
  const uint8_t* data() const { return data_; }
  constexpr size_t size() const { return kSize; }

 private:
  FIDL_ALIGNDECL uint8_t data_[kSize];
};

static_assert(sizeof(InlineMessageBuffer<40>) == 40);

static_assert(alignof(std::max_align_t) % FIDL_ALIGNMENT == 0,
              "AlignedBuffer should follow FIDL alignment when allocated on the heap.");

// An heap allocated uninitialized array of |kSize| bytes, guaranteed to follow FIDL alignment.
template <size_t kSize>
struct BoxedMessageBuffer {
  static_assert(kSize % FIDL_ALIGNMENT == 0, "kSize must be FIDL-aligned");

  BoxedMessageBuffer() { ZX_DEBUG_ASSERT(FidlIsAligned(bytes_.get())); }
  BoxedMessageBuffer(BoxedMessageBuffer&&) = delete;
  BoxedMessageBuffer(const BoxedMessageBuffer&) = delete;
  BoxedMessageBuffer& operator=(BoxedMessageBuffer&&) = delete;
  BoxedMessageBuffer& operator=(const BoxedMessageBuffer&) = delete;

  BufferSpan view() { return BufferSpan(data(), kSize); }
  uint8_t* data() { return bytes_.get(); }
  const uint8_t* data() const { return bytes_.get(); }
  constexpr size_t size() const { return kSize; }

 private:
  std::unique_ptr<uint8_t[]> bytes_ = std::make_unique<uint8_t[]>(kSize);
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_MESSAGE_STORAGE_H_

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_MESSAGE_STORAGE_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_MESSAGE_STORAGE_H_

#include <lib/fidl/cpp/wire/traits.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <type_traits>

#if defined(__clang__) && __has_attribute(uninitialized)
// Attribute "uninitialized" disables -ftrivial-auto-var-init=pattern
// (automatic variable initialization) for the specified variable.
// This is a security measure to better reveal memory corruptions and
// reduce leaking sensitive bits, but FIDL generated code/runtime can
// sometimes prove that a buffer is always overwritten. In those cases
// we can use this attribute to disable the compiler-inserted initialization
// and avoid the performance hit of writing to a large buffer.
#define FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT __attribute__((uninitialized))
#else
#define FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT
#endif

namespace fidl {

class MemoryResource {
 public:
  MemoryResource() = default;
  virtual ~MemoryResource() = default;

  // Allocates a |num_bytes| sized buffer, aligned to |FIDL_ALIGNMENT|.
  //
  // If the buffer resource cannot satisfy the allocation, it should return
  // nullptr, and preserve its original state before the allocation.
  //
  // |num_bytes| represents the size of the allocation request.
  virtual uint8_t* Allocate(uint32_t num_bytes) = 0;
};

// An |AnyMemoryResource| is a type-erased object that responds to allocation
// commands and updates the state of the underlying memory resource referenced
// by it.
//
// Using |inline_any| ensures that there is no heap allocation, which would
// otherwise defeat the purpose of caller-allocating flavors.
//
// See |AnyBufferAllocator|.
using AnyMemoryResource = fit::inline_any<MemoryResource, /* Reserve */ 24, /* Align */ 8>;

// Holds a reference to any storage buffer. This is independent of the allocation.
struct BufferSpan {
  BufferSpan() = default;
  BufferSpan(uint8_t* data, uint32_t capacity) : data(data), capacity(capacity) {}

  uint8_t* data = nullptr;
  uint32_t capacity = 0;

 private:
  // Type erasing adaptor from |BufferSpan| to |AnyBufferAllocator|.
  // See |AnyBufferAllocator|.
  friend AnyMemoryResource MakeFidlAnyMemoryResource(fidl::BufferSpan buffer_span);
};

namespace internal {
// The largest message to store on the stack.
static constexpr size_t kMaxMessageSizeOnStack = 512;

// A stack allocated uninitialized array of |kSize| bytes, guaranteed to follow
// FIDL alignment.
//
// To properly ensure uninitialization, always declare objects of this type with
// FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT.
template <size_t kSize>
struct InlineMessageBuffer {
  static_assert(kSize % FIDL_ALIGNMENT == 0, "kSize must be FIDL-aligned");

  // NOLINTNEXTLINE
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
              "BoxedMessageBuffer should follow FIDL alignment when allocated on the heap.");

// A heap allocated uninitialized array of |kSize| bytes, guaranteed to follow
// FIDL alignment.
template <size_t kSize>
struct BoxedMessageBuffer {
  static_assert(kSize % FIDL_ALIGNMENT == 0, "kSize must be FIDL-aligned");

  BoxedMessageBuffer() { ZX_DEBUG_ASSERT(FidlIsAligned(bytes_)); }
  ~BoxedMessageBuffer() { delete[] bytes_; }
  BoxedMessageBuffer(BoxedMessageBuffer&&) = delete;
  BoxedMessageBuffer(const BoxedMessageBuffer&) = delete;
  BoxedMessageBuffer& operator=(BoxedMessageBuffer&&) = delete;
  BoxedMessageBuffer& operator=(const BoxedMessageBuffer&) = delete;

  BufferSpan view() { return BufferSpan(data(), kSize); }
  uint8_t* data() { return bytes_; }
  const uint8_t* data() const { return bytes_; }
  constexpr size_t size() const { return kSize; }

 private:
  uint8_t* bytes_ = new uint8_t[kSize];
};

// Pick the appropriate message buffer implementation based on size requirements.
template <size_t kSize>
using MessageBuffer = std::conditional_t<kSize <= kMaxMessageSizeOnStack,
                                         InlineMessageBuffer<kSize>, BoxedMessageBuffer<kSize>>;

// Outgoing messages only have to be as big enough to hold known fields.
template <typename FidlType>
using OutgoingMessageBuffer =
    MessageBuffer<internal::ClampedMessageSize<FidlType, MessageDirection::kSending>()>;

// |AnyBufferAllocator| is a type-erasing buffer allocator. Its main purpose is
// to extend the caller-allocating call/reply flavors to work with a flexible
// range of buffer-like types ("memory resources").
//
// This class is similar in spirit to a |std::pmr::polymorphic_allocator|,
// except that it is specialized to allocating buffers (ranges of bytes).
//
// This class is compact (4 machine words), such that it may be efficiently
// moved around as a temporary value.
//
// If initialized with a |BufferSpan|, allocates in that buffer span. If
// initialized with a reference to some arena, allocates in that arena.
//
// To extend |AnyBufferAllocator| to work with future buffer-like types,
// declare this function for a user type |R| in the same namespace as the
// user type:
//
//     fidl::AnyMemoryResource MakeFidlAnyMemoryResource(R memory_resource);
//
// If possible, it is recommended to only declare this function as a friend of
// the user type (i.e. declare it within the user type definition, the "hidden
// member friend pattern"), such that it is hidden from qualified calls and only
// findable by ADL.
class AnyBufferAllocator {
 public:
  // Allocates a buffer of size |num_bytes|.
  //
  // If the underlying memory resource cannot satisfy the allocation, it should
  // return nullptr, and preserve its original state before the allocation.
  uint8_t* Allocate(uint32_t num_bytes) { return memory_resource_->Allocate(num_bytes); }

  // Attempt to allocate |size| bytes from the allocator, returning a view when
  // successful and an error otherwise.
  fit::result<fidl::Error, fidl::BufferSpan> TryAllocate(uint32_t num_bytes);

 private:
  template <typename MemoryResource>
  friend AnyBufferAllocator MakeAnyBufferAllocator(MemoryResource&& resource);

  // This constructor should only be used by |MakeAnyBufferAllocator|.
  explicit AnyBufferAllocator(AnyMemoryResource&& memory_resource)
      : memory_resource_(std::move(memory_resource)) {}

  AnyMemoryResource memory_resource_;
};

static_assert(sizeof(AnyBufferAllocator) <= 4 * sizeof(void*),
              "AnyBufferAllocator should be reasonably small");

template <typename MemoryResource>
AnyBufferAllocator MakeAnyBufferAllocator(MemoryResource&& resource) {
  // The |MakeFidlAnyMemoryResource| function will be found via
  // argument-dependent-lookup.
  return AnyBufferAllocator(MakeFidlAnyMemoryResource(std::forward<MemoryResource>(resource)));
}

}  // namespace internal

}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_MESSAGE_STORAGE_H_

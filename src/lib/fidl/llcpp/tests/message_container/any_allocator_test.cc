// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/arena.h>
#include <lib/fidl/llcpp/message_storage.h>

#include <unordered_set>

#include <zxtest/zxtest.h>

using ::fidl::internal::AnyBufferAllocator;

TEST(AnyBufferAllocator, WrapBufferSpan) {
  constexpr uint32_t kFullAllocSize = 128;
  constexpr uint32_t kHalfAllocSize = 64;
  uint8_t bytes[kFullAllocSize];
  fidl::BufferSpan buffer_span(bytes, sizeof(bytes));
  AnyBufferAllocator allocator = ::fidl::internal::MakeAnyBufferAllocator(buffer_span);
  uint8_t* addr;

  addr = allocator.Allocate(kHalfAllocSize);
  EXPECT_EQ(bytes, addr);

  addr = allocator.Allocate(kHalfAllocSize);
  EXPECT_EQ(&bytes[kHalfAllocSize], addr);

  // After allocating the half size twice, the buffer should be exhausted now.

  addr = allocator.Allocate(kHalfAllocSize);
  EXPECT_NULL(addr);

  addr = allocator.Allocate(1);
  EXPECT_NULL(addr);
}

TEST(AnyBufferAllocator, WrapBufferSpanOverflow) {
  constexpr uint32_t kBufferSize = 128;
  uint8_t bytes[kBufferSize];
  fidl::BufferSpan buffer_span(bytes, sizeof(bytes));
  AnyBufferAllocator allocator = ::fidl::internal::MakeAnyBufferAllocator(buffer_span);
  uint8_t* addr;

  addr = allocator.Allocate(1);
  EXPECT_NOT_NULL(addr);

  addr = allocator.Allocate(std::numeric_limits<uint32_t>::max());
  EXPECT_NULL(addr);

  addr = allocator.Allocate(1);
  EXPECT_NOT_NULL(addr);
}

TEST(AnyBufferAllocator, WrapArena) {
  constexpr uint32_t kFullAllocSize = 128;
  constexpr size_t kNumAllocations = 100;
  fidl::Arena arena;
  AnyBufferAllocator allocator = ::fidl::internal::MakeAnyBufferAllocator(arena);

  // Invariants:
  // - None of the returned addresses should reappear (no reusing of previously
  //   allocated parts).
  // - Writing to the allocated buffer should not fail.
  std::unordered_set<uint8_t*> addresses;
  for (size_t i = 0; i < kNumAllocations; i++) {
    uint8_t* addr = allocator.Allocate(kFullAllocSize);
    EXPECT_NOT_NULL(addr);
    memset(addr, 0, kFullAllocSize);
    EXPECT_TRUE(addresses.find(addr) == addresses.end());
    addresses.insert(addr);
  }
}

// Test that the user could extend `.buffer(...)` calls with their custom memory
// resource by defining a |MakeFidlAnyMemoryResource| function.
namespace my_fancy_memory_resource {

// A simple allocator that delegates to `new` and `delete`.
struct HeapAllocator {
  ~HeapAllocator() {
    for (const auto& allocation : allocations_) {
      delete[] allocation;
    }
  }

 private:
  friend fidl::AnyMemoryResource MakeFidlAnyMemoryResource(HeapAllocator& a) {
    return [&a](uint32_t num_bytes) {
      uint8_t* allocation = new uint8_t[num_bytes];
      a.allocations_.push_back(allocation);
      return allocation;
    };
  }

  std::vector<uint8_t*> allocations_;
};

}  // namespace my_fancy_memory_resource

TEST(AnyBufferAllocator, WrapCustomMemoryResource) {
  my_fancy_memory_resource::HeapAllocator custom_allocator{};
  AnyBufferAllocator allocator = ::fidl::internal::MakeAnyBufferAllocator(custom_allocator);
  constexpr uint32_t kAllocSize = 10;
  uint8_t* bytes = allocator.Allocate(kAllocSize);
  ASSERT_NOT_NULL(bytes);
  memset(bytes, 0xFF, kAllocSize);
  for (size_t i = 0; i < kAllocSize; i++) {
    EXPECT_EQ(0xFF, bytes[i]);
  }
}

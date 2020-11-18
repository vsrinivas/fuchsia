// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/memalloc.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <cstdlib>
#include <memory>
#include <new>

#include <gtest/gtest.h>

namespace memalloc {
namespace {

constexpr size_t kMiB = 1048576;

// A region of memory correctly aligned for tests.
class Region {
 public:
  // Allocate a correct aligned region of memory.
  static Region Create(size_t size_bytes) {
    ZX_ASSERT(size_bytes % Allocator::kBlockSize == 0);
    return Region(std::make_unique<Block[]>(size_bytes / Allocator::kBlockSize),
                  size_bytes);
  }

  std::byte* data() const { return reinterpret_cast<std::byte*>(data_[0].data); }

  size_t size() const { return size_; }

 private:
  // A block of data, aligned and sized to the allocator's required format.
  struct Block {
    std::byte data[Allocator::kBlockSize];
  } __ALIGNED(Allocator::kBlockSize);

  Region(std::unique_ptr<Block[]> data, size_t size) : data_(std::move(data)), size_(size) {}

  std::unique_ptr<Block[]> data_;
  size_t size_;
};


TEST(Allocator, EmptyAllocator) {
  Allocator allocator{};
  EXPECT_EQ(nullptr, allocator.Allocate(Allocator::kBlockSize));
}

TEST(Allocator, ZeroSizeRanges) {
  auto range = Region::Create(Allocator::kBlockSize);

  // Add an empty range to the allocator.
  Allocator allocator{};
  allocator.AddRange(range.data(), 0);

  // Add a real range to the allocator.
  allocator.AddRange(range.data(), Allocator::kBlockSize);

  // Allocate some empty ranges.
  EXPECT_EQ(nullptr, allocator.Allocate(0));
  EXPECT_EQ(nullptr, allocator.Allocate(0));

  // Allocate some real ranges again.
  EXPECT_EQ(range.data(), allocator.Allocate(Allocator::kBlockSize));
}

TEST(Allocator, SingleRange) {
  auto range = Region::Create(Allocator::kBlockSize);

  // Create an allocator with 1 page of memory in it.
  Allocator allocator{};
  allocator.AddRange(range.data(), range.size());

  // Expect to be able to allocate it again.
  EXPECT_EQ(range.data(), allocator.Allocate(range.size()));

  // Ensure we are empty.
  EXPECT_EQ(nullptr, allocator.Allocate(range.size()));
}

TEST(Allocator, MultipleAllocations) {
  auto range = Region::Create(Allocator::kBlockSize * 100);

  // Create an allocator with 1 page of memory in it.
  Allocator allocator{};
  allocator.AddRange(range.data(), range.size());

  // Allocate pages.
  std::byte* a = allocator.Allocate(Allocator::kBlockSize * 10);
  ASSERT_NE(a, nullptr);
  std::byte* b = allocator.Allocate(Allocator::kBlockSize * 20);
  ASSERT_NE(b, nullptr);
  std::byte* c = allocator.Allocate(Allocator::kBlockSize * 70);
  ASSERT_NE(c, nullptr);

  // Ensure we are empty.
  EXPECT_EQ(nullptr, allocator.Allocate(Allocator::kBlockSize));

  // Try adding pages back again in a different order.
  allocator.AddRange(a, Allocator::kBlockSize * 10);
  allocator.AddRange(c, Allocator::kBlockSize * 70);
  allocator.AddRange(b, Allocator::kBlockSize * 20);

  // We should be able to allocate the entire original range again.
  EXPECT_EQ(range.data(), allocator.Allocate(range.size()));
}

TEST(Allocator, AlignedAllocations) {
  // Create an allocator with 16MiB of memory in it.
  auto range = Region::Create(16 * kMiB);
  Allocator allocator{};
  allocator.AddRange(range.data(), 16 * kMiB);

  // Get allocations of memory, at increasing alignment.
  for (size_t alignment = Allocator::kBlockSize; alignment <= kMiB; alignment *= 2) {
    std::byte* result = allocator.Allocate(Allocator::kBlockSize, alignment);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(result) % alignment, 0u)
        << "Allocation was not aligned as requested.";
  }
}

TEST(Allocator, DeallocationMerging) {
  auto range = Region::Create(4 * Allocator::kBlockSize);
  Allocator allocator{};
  allocator.AddRange(range.data(), 4 * Allocator::kBlockSize);

  // Allocate 4 pages of memory and deallocate them again in every possible
  // order.
  //
  // We attempt all 4 factorial methods of deallocating them to exercise the
  // merging logic.
  std::vector<size_t> permutation = {0, 1, 2, 3};
  do {
    std::vector<std::byte*> addrs;

    // Allocate 4 pages.
    for (int i = 0; i < 4; i++) {
      std::byte* result = allocator.Allocate(Allocator::kBlockSize);
      ASSERT_NE(result, nullptr);
      addrs.push_back(result);
    }

    // Deallocate in a given order.
    for (int i = 0; i < 4; i++) {
      allocator.AddRange(addrs[permutation[i]], Allocator::kBlockSize);
    }

    // Ensure we can allocate the full range again.
    std::byte* result = allocator.Allocate(Allocator::kBlockSize * 4);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result, range.data());
    allocator.AddRange(result, Allocator::kBlockSize * 4);
  } while (std::next_permutation(permutation.begin(), permutation.end()));
}

TEST(Allocator, Overflow) {
  constexpr uint64_t kMaxAlign = uint64_t{1} << 63;

  // Create an allocator with 1 MiB of memory in it.
  auto range = Region::Create(kMiB);
  Allocator allocator{};
  allocator.AddRange(range.data(), kMiB);

  // If the OS-selected range happens to span the only address at
  // kMaxAlign, select a new address.
  auto range_start = reinterpret_cast<uintptr_t>(range.data());
  if (range_start <= kMaxAlign && range_start + kMiB > kMaxAlign) {
    auto new_range = Region::Create(kMiB);
    range = std::move(new_range);
  }

  // Attempt to allocate various amounts of RAM likely to cause overflow in internal calculations.
  EXPECT_EQ(nullptr, allocator.Allocate(-Allocator::kBlockSize, Allocator::kBlockSize));
  EXPECT_EQ(nullptr, allocator.Allocate(Allocator::kBlockSize, kMaxAlign));
  EXPECT_EQ(nullptr, allocator.Allocate(-Allocator::kBlockSize, kMaxAlign));
  EXPECT_EQ(nullptr, allocator.Allocate(kMaxAlign, kMaxAlign));
}

}  // namespace
}  // namespace memalloc

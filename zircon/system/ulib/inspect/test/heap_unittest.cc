// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/cpp/vmo/heap.h>
#include <lib/inspect/cpp/vmo/scanner.h>

#include <iostream>
#include <vector>

#include <fbl/string_printf.h>
#include <zxtest/zxtest.h>

namespace {

using inspect::internal::Block;
using inspect::internal::BlockIndex;
using inspect::internal::BlockType;
using inspect::internal::Heap;
using inspect::internal::ScanBlocks;

zx::vmo MakeVmo(size_t size) {
  zx::vmo ret;
  EXPECT_OK(zx::vmo::create(size, 0, &ret));
  return ret;
}

constexpr size_t kMinAllocationSize = sizeof(Block);

struct DebugBlock {
  size_t index;
  BlockType type;
  size_t order;

  fbl::String dump() const {
    return fbl::StringPrintf("index=%lu type=%d order=%lu", index, static_cast<int>(type), order);
  }

  bool operator==(const DebugBlock& other) const {
    return index == other.index && type == other.type && order == other.order;
  }

  bool operator!=(const DebugBlock& other) const { return !(*this == other); }
};

std::vector<DebugBlock> dump(const Heap& heap) {
  std::vector<DebugBlock> ret;
  ZX_ASSERT(ZX_OK ==
            ScanBlocks(heap.data(), heap.size(), [&ret](BlockIndex index, const Block* block) {
              ret.push_back({index, GetType(block), GetOrder(block)});
              return true;
            }));

  return ret;
}

void ExpectDebugBlock(size_t index, const DebugBlock& expected, const DebugBlock& actual) {
  if (expected != actual) {
    std::cout << fbl::StringPrintf("Actual: %s, Expected: %s\n", actual.dump().c_str(),
                                   expected.dump().c_str())
                     .c_str()
              << fbl::StringPrintf("Mismatch at index %lu", index).c_str();

    EXPECT_TRUE(false);
  }
}

void MatchDebugBlockVectors(const std::vector<DebugBlock>& expected,
                            const std::vector<DebugBlock>& actual) {
  if (expected.size() != actual.size()) {
    fprintf(stderr, "Expected:\n");
    for (const auto& h : expected) {
      fprintf(stderr, " %s\n", h.dump().c_str());
    }
    fprintf(stderr, "Actual:\n");
    for (const auto& h : actual) {
      fprintf(stderr, " %s\n", h.dump().c_str());
    }
    ASSERT_TRUE(false);
  }

  for (size_t i = 0; i < expected.size(); i++) {
    ExpectDebugBlock(i, expected[i], actual[i]);
  }
}

TEST(Heap, Create) {
  auto vmo = MakeVmo(4096);
  ASSERT_TRUE(!!vmo);

  Heap heap(std::move(vmo));

  MatchDebugBlockVectors({{0, BlockType::kFree, 7}, {128, BlockType::kFree, 7}}, dump(heap));
}

TEST(Heap, Allocate) {
  auto vmo = MakeVmo(4096);
  ASSERT_TRUE(!!vmo);
  Heap heap(std::move(vmo));

  // Allocate a series of small blocks, they should all be in order.
  BlockIndex b;
  EXPECT_OK(heap.Allocate(kMinAllocationSize, &b));
  EXPECT_EQ(0u, b);
  EXPECT_OK(heap.Allocate(kMinAllocationSize, &b));
  EXPECT_EQ(1u, b);
  EXPECT_OK(heap.Allocate(kMinAllocationSize, &b));
  EXPECT_EQ(2u, b);
  EXPECT_OK(heap.Allocate(kMinAllocationSize, &b));
  EXPECT_EQ(3u, b);
  EXPECT_OK(heap.Allocate(kMinAllocationSize, &b));
  EXPECT_EQ(4u, b);
  EXPECT_OK(heap.Allocate(kMinAllocationSize, &b));
  EXPECT_EQ(5u, b);

  // Free blocks, leaving some in the middle to ensure they chain.
  heap.Free(2);
  heap.Free(4);
  heap.Free(0);

  // Allocate small blocks again to see that we get the same ones in reverse order.
  EXPECT_OK(heap.Allocate(kMinAllocationSize, &b));
  EXPECT_EQ(0u, b);
  EXPECT_OK(heap.Allocate(kMinAllocationSize, &b));
  EXPECT_EQ(4u, b);
  EXPECT_OK(heap.Allocate(kMinAllocationSize, &b));
  EXPECT_EQ(2u, b);

  // Free everything except for the first two.
  heap.Free(4);
  heap.Free(2);
  heap.Free(3);
  heap.Free(5);

  MatchDebugBlockVectors({{0, BlockType::kReserved, 0},
                          {1, BlockType::kReserved, 0},
                          {2, BlockType::kFree, 1},
                          {4, BlockType::kFree, 2},
                          {8, BlockType::kFree, 3},
                          {16, BlockType::kFree, 4},
                          {32, BlockType::kFree, 5},
                          {64, BlockType::kFree, 6},
                          {128, BlockType::kFree, 7}},
                         dump(heap));

  // Leave a small free hole at 0, allocate something large
  // and observe it takes the free largest block.
  heap.Free(0);
  EXPECT_OK(heap.Allocate(2048, &b));
  EXPECT_EQ(128u, b);

  // Free the last small allocation, the next large allocation
  // takes the first half of the buffer.
  heap.Free(1);
  EXPECT_OK(heap.Allocate(2048, &b));
  EXPECT_EQ(0u, b);

  MatchDebugBlockVectors({{0, BlockType::kReserved, 7}, {128, BlockType::kReserved, 7}},
                         dump(heap));

  // Allocate twice in the first half, free in reverse order
  // to ensure buddy freeing works left to right and right to left.
  heap.Free(0);
  EXPECT_OK(heap.Allocate(1024, &b));
  EXPECT_EQ(0u, b);
  EXPECT_OK(heap.Allocate(1024, &b));
  EXPECT_EQ(64u, b);
  heap.Free(0);
  heap.Free(64);

  // Ensure the freed blocks all merged into one big block and that we
  // can use the whole space at position 0.
  EXPECT_OK(heap.Allocate(2048, &b));
  EXPECT_EQ(0u, b);
  heap.Free(0);

  MatchDebugBlockVectors({{0, BlockType::kFree, 7}, {128, BlockType::kReserved, 7}}, dump(heap));
  heap.Free(128);
}

TEST(Heap, MergeBlockedByAllocation) {
  auto vmo = MakeVmo(4096);
  ASSERT_TRUE(!!vmo);
  Heap heap(std::move(vmo));

  // Allocate 4 small blocks at the beginning of the buffer.
  BlockIndex b;
  EXPECT_OK(heap.Allocate(kMinAllocationSize, &b));
  EXPECT_EQ(0u, b);
  EXPECT_OK(heap.Allocate(kMinAllocationSize, &b));
  EXPECT_EQ(1u, b);
  EXPECT_OK(heap.Allocate(kMinAllocationSize, &b));
  EXPECT_EQ(2u, b);
  EXPECT_OK(heap.Allocate(kMinAllocationSize, &b));
  EXPECT_EQ(3u, b);

  // Free position 2 first, then 0 and 1.
  // The final free sees a situation like:
  // FREE | FREE | FREE | RESERVED
  // The first two spaces will get merged into an order 1 blocku, but the
  // reserved space will prevent merging into an order 2 block.
  heap.Free(2);
  heap.Free(0);
  heap.Free(1);

  MatchDebugBlockVectors({{0, BlockType::kFree, 1},
                          {2, BlockType::kFree, 0},
                          {3, BlockType::kReserved, 0},
                          {4, BlockType::kFree, 2},
                          {8, BlockType::kFree, 3},
                          {16, BlockType::kFree, 4},
                          {32, BlockType::kFree, 5},
                          {64, BlockType::kFree, 6},
                          {128, BlockType::kFree, 7}},
                         dump(heap));

  heap.Free(3);

  MatchDebugBlockVectors({{0, BlockType::kFree, 7}, {128, BlockType::kFree, 7}}, dump(heap));
}

TEST(Heap, Extend) {
  auto vmo = MakeVmo(128 * 1024);
  ASSERT_TRUE(!!vmo);
  Heap heap(std::move(vmo));

  // Allocate many large blocks, so the VMO needs to be extended.
  BlockIndex b;
  EXPECT_OK(heap.Allocate(2048, &b));
  EXPECT_EQ(0u, b);
  EXPECT_OK(heap.Allocate(2048, &b));
  EXPECT_EQ(128u, b);
  EXPECT_OK(heap.Allocate(2048, &b));
  EXPECT_EQ(256u, b);

  MatchDebugBlockVectors({{0, BlockType::kReserved, 7},
                          {128, BlockType::kReserved, 7},
                          {256, BlockType::kReserved, 7},
                          {384, BlockType::kFree, 7}},
                         dump(heap));

  EXPECT_OK(heap.Allocate(2048, &b));
  EXPECT_EQ(384u, b);
  EXPECT_OK(heap.Allocate(2048, &b));
  EXPECT_EQ(512u, b);

  heap.Free(0);
  heap.Free(128);
  heap.Free(256);
  heap.Free(384);
  heap.Free(512);

  MatchDebugBlockVectors({{0, BlockType::kFree, 7},
                          {128, BlockType::kFree, 7},
                          {256, BlockType::kFree, 7},
                          {384, BlockType::kFree, 7},
                          {512, BlockType::kFree, 7},
                          {640, BlockType::kFree, 7},
                          {768, BlockType::kFree, 7},
                          {896, BlockType::kFree, 7}},
                         dump(heap));
}

TEST(Heap, ExtendFailure) {
  auto vmo = MakeVmo(3 * 4096);
  ASSERT_TRUE(!!vmo);
  Heap heap(std::move(vmo));

  // Allocate many large blocks, so the VMO needs to be extended.
  BlockIndex b;
  EXPECT_OK(heap.Allocate(2048, &b));
  EXPECT_EQ(0u, b);
  EXPECT_OK(heap.Allocate(2048, &b));
  EXPECT_EQ(128u, b);
  EXPECT_OK(heap.Allocate(2048, &b));
  EXPECT_EQ(256u, b);
  EXPECT_OK(heap.Allocate(2048, &b));
  EXPECT_EQ(384u, b);
  EXPECT_OK(heap.Allocate(2048, &b));
  EXPECT_EQ(512u, b);
  EXPECT_OK(heap.Allocate(2048, &b));
  EXPECT_EQ(640u, b);
  EXPECT_EQ(ZX_ERR_NO_MEMORY, heap.Allocate(2048, &b));

  MatchDebugBlockVectors({{0, BlockType::kReserved, 7},
                          {128, BlockType::kReserved, 7},
                          {256, BlockType::kReserved, 7},
                          {384, BlockType::kReserved, 7},
                          {512, BlockType::kReserved, 7},
                          {640, BlockType::kReserved, 7}},
                         dump(heap));

  heap.Free(0);
  heap.Free(128);
  heap.Free(256);
  heap.Free(384);
  heap.Free(512);
  heap.Free(640);
}

}  // namespace

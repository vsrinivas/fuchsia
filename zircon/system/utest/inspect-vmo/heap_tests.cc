// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/string_printf.h>
#include <fbl/vector.h>
#include <lib/inspect-vmo/heap.h>
#include <lib/inspect-vmo/scanner.h>
#include <unittest/unittest.h>

namespace {

using inspect::vmo::BlockType;
using inspect::vmo::internal::Block;
using inspect::vmo::internal::BlockIndex;
using inspect::vmo::internal::Heap;
using inspect::vmo::internal::ScanBlocks;

constexpr size_t kMinAllocationSize = sizeof(Block);

struct DebugBlock {
    size_t index;
    BlockType type;
    size_t order;

    fbl::String dump() const {
        return fbl::StringPrintf("index=%lu type=%d order=%lu", index, static_cast<int>(type),
                                 order);
    }

    bool operator==(const DebugBlock& other) const {
        return index == other.index && type == other.type && order == other.order;
    }
};

fbl::Vector<DebugBlock> dump(const Heap& heap) {
    fbl::Vector<DebugBlock> ret;
    ZX_ASSERT(ZX_OK ==
              ScanBlocks(heap.data(), heap.size(), [&ret](BlockIndex index, const Block* block) {
                  ret.push_back({index, GetType(block), GetOrder(block)});
              }));

    return ret;
}

bool ExpectDebugBlock(const DebugBlock& expected, const DebugBlock& actual) {
    BEGIN_HELPER;

    EXPECT_TRUE(expected == actual,
                fbl::StringPrintf("Actual: %s, Expected: %s", actual.dump().c_str(),
                                  expected.dump().c_str())
                    .c_str());

    END_HELPER;
}

bool MatchDebugBlockVectors(const fbl::Vector<DebugBlock>& expected,
                            const fbl::Vector<DebugBlock>& actual) {
    BEGIN_HELPER;

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
        EXPECT_TRUE(ExpectDebugBlock(expected[i], actual[i]),
                    fbl::StringPrintf("Mismatch at index %lu", i).c_str());
    }

    END_HELPER;
}

bool Create() {
    BEGIN_TEST;

    auto vmo = fzl::ResizeableVmoMapper::Create(4096, "test");
    ASSERT_NE(nullptr, vmo.get());

    Heap heap(std::move(vmo));

    EXPECT_TRUE(
        MatchDebugBlockVectors({{0, BlockType::kFree, 7}, {128, BlockType::kFree, 7}}, dump(heap)));

    END_TEST;
}

bool Allocate() {
    BEGIN_TEST;

    auto vmo = fzl::ResizeableVmoMapper::Create(4096, "test");
    ASSERT_NE(nullptr, vmo.get());
    Heap heap(std::move(vmo));

    // Allocate a series of small blocks, they should all be in order.
    BlockIndex b;
    EXPECT_EQ(ZX_OK, heap.Allocate(kMinAllocationSize, &b));
    EXPECT_EQ(0, b);
    EXPECT_EQ(ZX_OK, heap.Allocate(kMinAllocationSize, &b));
    EXPECT_EQ(1, b);
    EXPECT_EQ(ZX_OK, heap.Allocate(kMinAllocationSize, &b));
    EXPECT_EQ(2, b);
    EXPECT_EQ(ZX_OK, heap.Allocate(kMinAllocationSize, &b));
    EXPECT_EQ(3, b);
    EXPECT_EQ(ZX_OK, heap.Allocate(kMinAllocationSize, &b));
    EXPECT_EQ(4, b);
    EXPECT_EQ(ZX_OK, heap.Allocate(kMinAllocationSize, &b));
    EXPECT_EQ(5, b);

    // Free blocks, leaving some in the middle to ensure they chain.
    heap.Free(2);
    heap.Free(4);
    heap.Free(0);

    // Allocate small blocks again to see that we get the same ones in reverse order.
    EXPECT_EQ(ZX_OK, heap.Allocate(kMinAllocationSize, &b));
    EXPECT_EQ(0, b);
    EXPECT_EQ(ZX_OK, heap.Allocate(kMinAllocationSize, &b));
    EXPECT_EQ(4, b);
    EXPECT_EQ(ZX_OK, heap.Allocate(kMinAllocationSize, &b));
    EXPECT_EQ(2, b);

    // Free everything except for the first two.
    heap.Free(4);
    heap.Free(2);
    heap.Free(3);
    heap.Free(5);

    EXPECT_TRUE(MatchDebugBlockVectors({{0, BlockType::kReserved, 0},
                                        {1, BlockType::kReserved, 0},
                                        {2, BlockType::kFree, 1},
                                        {4, BlockType::kFree, 2},
                                        {8, BlockType::kFree, 3},
                                        {16, BlockType::kFree, 4},
                                        {32, BlockType::kFree, 5},
                                        {64, BlockType::kFree, 6},
                                        {128, BlockType::kFree, 7}},
                                       dump(heap)));

    // Leave a small free hole at 0, allocate something large
    // and observe it takes the free largest block.
    heap.Free(0);
    EXPECT_EQ(ZX_OK, heap.Allocate(2048, &b));
    EXPECT_EQ(128, b);

    // Free the last small allocation, the next large allocation
    // takes the first half of the buffer.
    heap.Free(1);
    EXPECT_EQ(ZX_OK, heap.Allocate(2048, &b));
    EXPECT_EQ(0, b);

    EXPECT_TRUE(MatchDebugBlockVectors(
        {{0, BlockType::kReserved, 7}, {128, BlockType::kReserved, 7}}, dump(heap)));

    // Allocate twice in the first half, free in reverse order
    // to ensure buddy freeing works left to right and right to left.
    heap.Free(0);
    EXPECT_EQ(ZX_OK, heap.Allocate(1024, &b));
    EXPECT_EQ(0, b);
    EXPECT_EQ(ZX_OK, heap.Allocate(1024, &b));
    EXPECT_EQ(64, b);
    heap.Free(0);
    heap.Free(64);

    // Ensure the freed blocks all merged into one big block and that we
    // can use the whole space at position 0.
    EXPECT_EQ(ZX_OK, heap.Allocate(2048, &b));
    EXPECT_EQ(0, b);
    heap.Free(0);

    EXPECT_TRUE(MatchDebugBlockVectors({{0, BlockType::kFree, 7}, {128, BlockType::kReserved, 7}},
                                       dump(heap)));
    heap.Free(128);

    END_TEST;
}

bool MergeBlockedByAllocation() {
    BEGIN_TEST;

    auto vmo = fzl::ResizeableVmoMapper::Create(4096, "test");
    ASSERT_NE(nullptr, vmo.get());
    Heap heap(std::move(vmo));

    // Allocate 4 small blocks at the beginning of the buffer.
    BlockIndex b;
    EXPECT_EQ(ZX_OK, heap.Allocate(kMinAllocationSize, &b));
    EXPECT_EQ(0, b);
    EXPECT_EQ(ZX_OK, heap.Allocate(kMinAllocationSize, &b));
    EXPECT_EQ(1, b);
    EXPECT_EQ(ZX_OK, heap.Allocate(kMinAllocationSize, &b));
    EXPECT_EQ(2, b);
    EXPECT_EQ(ZX_OK, heap.Allocate(kMinAllocationSize, &b));
    EXPECT_EQ(3, b);

    // Free position 2 first, then 0 and 1.
    // The final free sees a situation like:
    // FREE | FREE | FREE | RESERVED
    // The first two spaces will get merged into an order 1 block, but the
    // reserved space will prevent merging into an order 2 block.
    heap.Free(2);
    heap.Free(0);
    heap.Free(1);

    EXPECT_TRUE(MatchDebugBlockVectors({{0, BlockType::kFree, 1},
                                        {2, BlockType::kFree, 0},
                                        {3, BlockType::kReserved, 0},
                                        {4, BlockType::kFree, 2},
                                        {8, BlockType::kFree, 3},
                                        {16, BlockType::kFree, 4},
                                        {32, BlockType::kFree, 5},
                                        {64, BlockType::kFree, 6},
                                        {128, BlockType::kFree, 7}},
                                       dump(heap)));

    heap.Free(3);

    EXPECT_TRUE(
        MatchDebugBlockVectors({{0, BlockType::kFree, 7}, {128, BlockType::kFree, 7}}, dump(heap)));

    END_TEST;
}

bool Extend() {
    BEGIN_TEST;

    auto vmo = fzl::ResizeableVmoMapper::Create(4096, "test");
    ASSERT_NE(nullptr, vmo.get());
    Heap heap(std::move(vmo));

    // Allocate many large blocks, so the VMO needs to be extended.
    BlockIndex b;
    EXPECT_EQ(ZX_OK, heap.Allocate(2048, &b));
    EXPECT_EQ(0, b);
    EXPECT_EQ(ZX_OK, heap.Allocate(2048, &b));
    EXPECT_EQ(128, b);
    EXPECT_EQ(ZX_OK, heap.Allocate(2048, &b));
    EXPECT_EQ(256, b);

    EXPECT_TRUE(MatchDebugBlockVectors({{0, BlockType::kReserved, 7},
                                        {128, BlockType::kReserved, 7},
                                        {256, BlockType::kReserved, 7},
                                        {384, BlockType::kFree, 7}},
                                       dump(heap)));

    EXPECT_EQ(ZX_OK, heap.Allocate(2048, &b));
    EXPECT_EQ(384, b);
    EXPECT_EQ(ZX_OK, heap.Allocate(2048, &b));
    EXPECT_EQ(512, b);

    heap.Free(0);
    heap.Free(128);
    heap.Free(256);
    heap.Free(384);
    heap.Free(512);

    EXPECT_TRUE(MatchDebugBlockVectors({{0, BlockType::kFree, 7},
                                        {128, BlockType::kFree, 7},
                                        {256, BlockType::kFree, 7},
                                        {384, BlockType::kFree, 7},
                                        {512, BlockType::kFree, 7},
                                        {640, BlockType::kFree, 7},
                                        {768, BlockType::kFree, 7},
                                        {896, BlockType::kFree, 7}},
                                       dump(heap)));

    END_TEST;
}

bool ExtendFailure() {
    BEGIN_TEST;

    auto vmo = fzl::ResizeableVmoMapper::Create(4096, "test");
    ASSERT_NE(nullptr, vmo.get());
    Heap heap(std::move(vmo), /*max_size=*/3 * 4096);

    // Allocate many large blocks, so the VMO needs to be extended.
    BlockIndex b;
    EXPECT_EQ(ZX_OK, heap.Allocate(2048, &b));
    EXPECT_EQ(0, b);
    EXPECT_EQ(ZX_OK, heap.Allocate(2048, &b));
    EXPECT_EQ(128, b);
    EXPECT_EQ(ZX_OK, heap.Allocate(2048, &b));
    EXPECT_EQ(256, b);
    EXPECT_EQ(ZX_OK, heap.Allocate(2048, &b));
    EXPECT_EQ(384, b);
    EXPECT_EQ(ZX_OK, heap.Allocate(2048, &b));
    EXPECT_EQ(512, b);
    EXPECT_EQ(ZX_OK, heap.Allocate(2048, &b));
    EXPECT_EQ(640, b);
    EXPECT_EQ(ZX_ERR_NO_MEMORY, heap.Allocate(2048, &b));

    EXPECT_TRUE(MatchDebugBlockVectors({{0, BlockType::kReserved, 7},
                                        {128, BlockType::kReserved, 7},
                                        {256, BlockType::kReserved, 7},
                                        {384, BlockType::kReserved, 7},
                                        {512, BlockType::kReserved, 7},
                                        {640, BlockType::kReserved, 7}},
                                       dump(heap)));

    heap.Free(0);
    heap.Free(128);
    heap.Free(256);
    heap.Free(384);
    heap.Free(512);
    heap.Free(640);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(HeapTests)
RUN_TEST(Create)
RUN_TEST(Allocate)
RUN_TEST(MergeBlockedByAllocation)
RUN_TEST(Extend)
RUN_TEST(ExtendFailure)
END_TEST_CASE(HeapTests)

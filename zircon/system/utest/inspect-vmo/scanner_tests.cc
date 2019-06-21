// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect-vmo/scanner.h>
#include <unittest/unittest.h>
#include <zircon/types.h>

namespace {

using inspect::vmo::BlockType;
using inspect::vmo::internal::Block;
using inspect::vmo::internal::BlockIndex;
using inspect::vmo::internal::ScanBlocks;

bool ReadEmpty() {
    BEGIN_TEST;

    uint8_t buf[1024];
    memset(buf, 0, 1024);

    int count = 0;
    EXPECT_TRUE(ZX_OK ==
                ScanBlocks(buf, 1024, [&count](BlockIndex index, const Block* block) { count++; }));
    EXPECT_EQ(1024 / inspect::vmo::kMinOrderSize, count);

    END_TEST;
}

bool ReadMisaligned() {
    BEGIN_TEST;

    uint8_t buf[1020];
    memset(buf, 0, 1020);

    int count = 0;
    EXPECT_TRUE(ZX_ERR_OUT_OF_RANGE ==
                ScanBlocks(buf, 1020, [&count](BlockIndex index, const Block* block) { count++; }));
    EXPECT_EQ(1024 / inspect::vmo::kMinOrderSize - 1, count);

    END_TEST;
}

bool ReadSingle() {
    BEGIN_TEST;

    uint8_t buf[inspect::vmo::kMinOrderSize];
    memset(buf, 0, inspect::vmo::kMinOrderSize);

    int count = 0;
    BlockIndex last_index = 0xFFFFFF;
    EXPECT_TRUE(ZX_OK ==
                ScanBlocks(buf, inspect::vmo::kMinOrderSize, [&](BlockIndex index, const Block* block) {
                    count++;
                    last_index = index;
                }));
    EXPECT_EQ(1, count);
    EXPECT_EQ(0, last_index);

    END_TEST;
}

bool ReadOutOfBounds() {
    BEGIN_TEST;

    uint8_t buf[inspect::vmo::kMinOrderSize];
    memset(buf, 0, inspect::vmo::kMinOrderSize);
    Block* block = reinterpret_cast<Block*>(buf);
    block->header = inspect::vmo::internal::BlockFields::Order::Make(1);

    int count = 0;
    EXPECT_TRUE(ZX_ERR_OUT_OF_RANGE ==
                ScanBlocks(buf, inspect::vmo::kMinOrderSize,
                           [&count](BlockIndex index, const Block* block) { count++; }));
    EXPECT_EQ(0, count);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(ScannerTests)
RUN_TEST(ReadEmpty)
RUN_TEST(ReadMisaligned)
RUN_TEST(ReadSingle)
RUN_TEST(ReadOutOfBounds)
END_TEST_CASE(ScannerTests)

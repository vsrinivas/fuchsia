// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logical-to-physical-map.h"

#include <fbl/array.h>
#include <fbl/vector.h>
#include <unittest/unittest.h>
#include <zircon/types.h>

namespace nand {

bool Check(LogicalToPhysicalMap ltop_map, fbl::Vector<uint32_t> expected) {
    BEGIN_HELPER;
    EXPECT_EQ(ltop_map.LogicalBlockCount(), expected.size());
    uint32_t i;
    for (i = 0; i < expected.size(); i++) {
        uint32_t physical;
        zx_status_t status = ltop_map.GetPhysical(i, &physical);
        EXPECT_EQ(status, ZX_OK);
        EXPECT_EQ(physical, expected[i]);
    }
    uint32_t physical;
    zx_status_t status = ltop_map.GetPhysical(i, &physical);
    EXPECT_EQ(status, ZX_ERR_OUT_OF_RANGE);
    END_HELPER;
}

bool SimpleTest() {
    BEGIN_TEST;
    LogicalToPhysicalMap ltop_map(5, fbl::Array<uint32_t>());
    EXPECT_TRUE(Check(fbl::move(ltop_map), {0, 1, 2, 3, 4}));
    END_TEST;
}

bool SingleBadTest() {
    BEGIN_TEST;
    fbl::Array<uint32_t> bad_blocks(new uint32_t[1], 1);
    bad_blocks[0] = 2;
    LogicalToPhysicalMap ltop_map(5, fbl::move(bad_blocks));
    EXPECT_TRUE(Check(fbl::move(ltop_map), {0, 1, 3, 4}));
    END_TEST;
}

bool FirstBadTest() {
    BEGIN_TEST;
    fbl::Array<uint32_t> bad_blocks(new uint32_t[1], 1);
    bad_blocks[0] = 0;
    LogicalToPhysicalMap ltop_map(5, fbl::move(bad_blocks));
    EXPECT_TRUE(Check(fbl::move(ltop_map), {1, 2, 3, 4}));
    END_TEST;
}

bool LastBadTest() {
    BEGIN_TEST;
    fbl::Array<uint32_t> bad_blocks(new uint32_t[1], 1);
    bad_blocks[0] = 4;
    LogicalToPhysicalMap ltop_map(5, fbl::move(bad_blocks));
    EXPECT_TRUE(Check(fbl::move(ltop_map), {0, 1, 2, 3}));
    END_TEST;
}

bool MultipleBadTest() {
    BEGIN_TEST;
    fbl::Array<uint32_t> bad_blocks(new uint32_t[3], 3);
    bad_blocks[0] = 0;
    bad_blocks[1] = 2;
    bad_blocks[2] = 4;
    LogicalToPhysicalMap ltop_map(5, fbl::move(bad_blocks));
    EXPECT_TRUE(Check(fbl::move(ltop_map), {1, 3}));
    END_TEST;
}

bool AllBadTest() {
    BEGIN_TEST;
    fbl::Array<uint32_t> bad_blocks(new uint32_t[3], 3);
    bad_blocks[0] = 0;
    bad_blocks[1] = 1;
    bad_blocks[2] = 2;
    LogicalToPhysicalMap ltop_map(3, fbl::move(bad_blocks));

    EXPECT_EQ(ltop_map.LogicalBlockCount(), 0);
    uint32_t physical;
    zx_status_t status = ltop_map.GetPhysical(0, &physical);
    EXPECT_EQ(status, ZX_ERR_OUT_OF_RANGE);
    END_TEST;
}

} // namespace nand

BEGIN_TEST_CASE(LtopTests)
RUN_TEST(nand::SimpleTest)
RUN_TEST(nand::SingleBadTest)
RUN_TEST(nand::FirstBadTest)
RUN_TEST(nand::LastBadTest)
RUN_TEST(nand::AllBadTest)
END_TEST_CASE(LtopTests);

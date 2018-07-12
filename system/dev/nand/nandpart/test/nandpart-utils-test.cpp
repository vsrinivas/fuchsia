// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nandpart-utils.h"

#include <fbl/unique_ptr.h>
#include <unittest/unittest.h>
#include <zircon/types.h>

namespace nand {
namespace {

constexpr uint32_t kPageSize = ZX_PAGE_SIZE;
constexpr uint32_t kPagesPerBlock = 2;
constexpr uint32_t kNumBlocks = 5;
constexpr uint32_t kOobSize = 8;
constexpr nand_info_t kNandInfo = {
    .page_size = kPageSize,
    .pages_per_block = kPagesPerBlock,
    .num_blocks = kNumBlocks,
    .ecc_bits = 2,
    .oob_size = kOobSize,
    .nand_class = NAND_CLASS_BBS,
    .partition_guid = {},
};

zbi_partition_map_t MakePartitionMap(uint32_t partition_count) {
    return zbi_partition_map_t{
        .block_count = kNumBlocks * kPagesPerBlock,
        .block_size = kPageSize,
        .partition_count = partition_count,
        .reserved = 0,
        .guid = {},
        .partitions = {},
    };
}

zbi_partition_t MakePartition(uint32_t first_block, uint32_t last_block) {
    return zbi_partition_t{
        .type_guid = {},
        .uniq_guid = {},
        .first_block = first_block,
        .last_block = last_block,
        .flags = 0,
        .name = {},
    };
}

bool ValidatePartition(zbi_partition_map_t* pmap, size_t partition_number, uint32_t first_block,
                       uint32_t last_block) {
    BEGIN_HELPER;
    EXPECT_EQ(pmap->partitions[partition_number].first_block, first_block);
    EXPECT_EQ(pmap->partitions[partition_number].last_block, last_block);
    END_HELPER;
}

bool SanitizeEmptyPartitionMapTest() {
    BEGIN_TEST;
    auto pmap = MakePartitionMap(0);
    ASSERT_NE(SanitizePartitionMap(&pmap, kNandInfo), ZX_OK);
    END_TEST;
}

bool SanitizeSinglePartitionMapTest() {
    BEGIN_TEST;
    fbl::unique_ptr<uint8_t[]> pmap_buffer(new uint8_t[sizeof(zbi_partition_map_t) +
                                                       sizeof(zbi_partition_t)]);
    auto* pmap = reinterpret_cast<zbi_partition_map_t*>(pmap_buffer.get());
    *pmap = MakePartitionMap(1);
    pmap->partitions[0] = MakePartition(0, 9);
    ASSERT_EQ(SanitizePartitionMap(pmap, kNandInfo), ZX_OK);
    ASSERT_TRUE(ValidatePartition(pmap, 0, 0, 4));
    END_TEST;
}

bool SanitizeMultiplePartitionMapTest() {
    BEGIN_TEST;
    fbl::unique_ptr<uint8_t[]> pmap_buffer(new uint8_t[sizeof(zbi_partition_map_t) +
                                                       3 * sizeof(zbi_partition_t)]);
    auto* pmap = reinterpret_cast<zbi_partition_map_t*>(pmap_buffer.get());
    *pmap = MakePartitionMap(3);
    pmap->partitions[0] = MakePartition(0, 3);
    pmap->partitions[1] = MakePartition(4, 7);
    pmap->partitions[2] = MakePartition(8, 9);

    ASSERT_EQ(SanitizePartitionMap(pmap, kNandInfo), ZX_OK);
    ASSERT_TRUE(ValidatePartition(pmap, 0, 0, 1));
    ASSERT_TRUE(ValidatePartition(pmap, 1, 2, 3));
    ASSERT_TRUE(ValidatePartition(pmap, 2, 4, 4));
    END_TEST;
}

bool SanitizeMultiplePartitionMapOutOfOrderTest() {
    BEGIN_TEST;
    fbl::unique_ptr<uint8_t[]> pmap_buffer(new uint8_t[sizeof(zbi_partition_map_t) +
                                                       2 * sizeof(zbi_partition_t)]);
    auto* pmap = reinterpret_cast<zbi_partition_map_t*>(pmap_buffer.get());
    *pmap = MakePartitionMap(2);
    pmap->partitions[0] = MakePartition(4, 9);
    pmap->partitions[1] = MakePartition(0, 3);

    ASSERT_EQ(SanitizePartitionMap(pmap, kNandInfo), ZX_OK);
    ASSERT_TRUE(ValidatePartition(pmap, 0, 0, 1));
    ASSERT_TRUE(ValidatePartition(pmap, 1, 2, 4));
    END_TEST;
}

bool SanitizeMultiplePartitionMapOverlappingTest() {
    BEGIN_TEST;
    fbl::unique_ptr<uint8_t[]> pmap_buffer(new uint8_t[sizeof(zbi_partition_map_t) +
                                                       3 * sizeof(zbi_partition_t)]);
    auto* pmap = reinterpret_cast<zbi_partition_map_t*>(pmap_buffer.get());
    *pmap = MakePartitionMap(3);
    pmap->partitions[0] = MakePartition(0, 3);
    pmap->partitions[1] = MakePartition(8, 9);
    pmap->partitions[2] = MakePartition(4, 8);

    ASSERT_NE(SanitizePartitionMap(pmap, kNandInfo), ZX_OK);
    END_TEST;
}

bool SanitizePartitionMapBadRangeTest() {
    BEGIN_TEST;
    fbl::unique_ptr<uint8_t[]> pmap_buffer(new uint8_t[sizeof(zbi_partition_map_t) +
                                                       2 * sizeof(zbi_partition_t)]);
    auto* pmap = reinterpret_cast<zbi_partition_map_t*>(pmap_buffer.get());
    *pmap = MakePartitionMap(2);
    pmap->partitions[0] = MakePartition(1, 0);
    pmap->partitions[1] = MakePartition(1, 9);

    ASSERT_NE(SanitizePartitionMap(pmap, kNandInfo), ZX_OK);
    END_TEST;
}

bool SanitizePartitionMapUnalignedTest() {
    BEGIN_TEST;
    fbl::unique_ptr<uint8_t[]> pmap_buffer(new uint8_t[sizeof(zbi_partition_map_t) +
                                                       2 * sizeof(zbi_partition_t)]);
    auto* pmap = reinterpret_cast<zbi_partition_map_t*>(pmap_buffer.get());
    *pmap = MakePartitionMap(2);
    pmap->partitions[0] = MakePartition(0, 3);
    pmap->partitions[1] = MakePartition(5, 8);

    ASSERT_NE(SanitizePartitionMap(pmap, kNandInfo), ZX_OK);
    END_TEST;
}

bool SanitizePartitionMapOutofBoundsTest() {
    BEGIN_TEST;
    fbl::unique_ptr<uint8_t[]> pmap_buffer(new uint8_t[sizeof(zbi_partition_map_t) +
                                                       2 * sizeof(zbi_partition_t)]);
    auto* pmap = reinterpret_cast<zbi_partition_map_t*>(pmap_buffer.get());
    *pmap = MakePartitionMap(2);
    pmap->partitions[0] = MakePartition(0, 3);
    pmap->partitions[1] = MakePartition(4, 11);

    ASSERT_NE(SanitizePartitionMap(pmap, kNandInfo), ZX_OK);
    END_TEST;
}

} // namespace
} // namespace nand

BEGIN_TEST_CASE(NandpartUtilsTests)
RUN_TEST(nand::SanitizeEmptyPartitionMapTest)
RUN_TEST(nand::SanitizeSinglePartitionMapTest)
RUN_TEST(nand::SanitizeMultiplePartitionMapTest)
RUN_TEST(nand::SanitizeMultiplePartitionMapOutOfOrderTest)
RUN_TEST(nand::SanitizeMultiplePartitionMapOverlappingTest)
RUN_TEST(nand::SanitizePartitionMapBadRangeTest)
RUN_TEST(nand::SanitizePartitionMapUnalignedTest)
RUN_TEST(nand::SanitizePartitionMapOutofBoundsTest)
END_TEST_CASE(NandpartUtilsTests);

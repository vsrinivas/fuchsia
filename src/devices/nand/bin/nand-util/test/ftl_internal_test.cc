// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ftl_internal.h"

#include <fbl/vector.h>
#include <zxtest/zxtest.h>

namespace internal {

constexpr SpareArea kSimpleOob = {1, {2, 3, 4, 5}, {6, 7, 8, 9}, {10, 11, 12}, 0x5D, {14, 15}, 16};
constexpr SpareArea kEmptyOob = {0xFF,
                                 {0xFF, 0xFF, 0xFF, 0xFF},
                                 {0xFF, 0xFF, 0xFF, 0xFF},
                                 {0xFF, 0xFF, 0xFF},
                                 0xFF,
                                 {0xFF, 0xFF},
                                 0xFF};

TEST(SpareArea, DecodePageNum) {
  EXPECT_EQ(0x05040302, DecodePageNum(kSimpleOob));
  EXPECT_EQ(-1, DecodePageNum(kEmptyOob));
}

TEST(SpareArea, DecodeBlockCount) {
  EXPECT_EQ(0x09080706, DecodeBlockCount(kSimpleOob));
  EXPECT_EQ(-1, DecodeBlockCount(kEmptyOob));
}

TEST(SpareArea, DecodeWear) {
  EXPECT_EQ(0x050C0B0A, DecodeWear(kSimpleOob));
  EXPECT_EQ(-1, DecodeWear(kEmptyOob));
}

TEST(SpareArea, IsNdmBlock) {
  SpareArea oob = kSimpleOob;
  oob.ndm = 0;
  EXPECT_FALSE(IsNdmBlock(oob));

  memcpy(oob.page_num, kNdmSignature, sizeof(kNdmSignature) - 1);
  EXPECT_TRUE(IsNdmBlock(oob));
}

TEST(SpareArea, IsFtlBlock) {
  SpareArea oob = kSimpleOob;
  EXPECT_FALSE(IsFtlBlock(oob));

  oob.ndm = 0xFF;
  EXPECT_TRUE(IsFtlBlock(oob));
}

TEST(SpareArea, IsDataBlock) {
  SpareArea oob = kSimpleOob;
  EXPECT_FALSE(IsDataBlock(oob));

  memset(oob.block_count, 0xFF, 4);
  EXPECT_TRUE(IsDataBlock(oob));
}

TEST(SpareArea, IsCopyBlock) {
  SpareArea oob = kSimpleOob;
  EXPECT_FALSE(IsCopyBlock(oob));

  oob.block_count[0] = 0xFE;
  memset(oob.block_count + 1, 0xFF, 3);
  EXPECT_TRUE(IsCopyBlock(oob));
}

TEST(SpareArea, IsMapBlock) {
  SpareArea oob = kSimpleOob;
  EXPECT_TRUE(IsMapBlock(oob));

  memset(oob.block_count, 0xFF, 4);
  EXPECT_FALSE(IsMapBlock(oob));

  oob.block_count[0] = 0xFE;
  EXPECT_FALSE(IsMapBlock(oob));

  oob.block_count[0] = 0xFD;
  EXPECT_TRUE(IsMapBlock(oob));
}

constexpr uint32_t kControl1[] = {
    0x00010001, 0x00000002, 0x0ba819e4, 0x0000012c, 0x00040000, 0x0000012b, 0x0000012a, 0x0000011c,
    0x00000129, 0xffffffff, 0x00000000, 0x0000002a, 0x00000064, 0x0000012c, 0xffffffff, 0xffffffff,
};

TEST(NdmData, BadBlocks) {
  NdmData ndm;
  fbl::Vector<int32_t> bad_blocks;
  fbl::Vector<int32_t> replacements;
  ndm.ParseNdmData(kControl1, &bad_blocks, &replacements);

  ASSERT_EQ(2, bad_blocks.size());
  ASSERT_EQ(0, replacements.size());
  EXPECT_EQ(42, bad_blocks[0]);
  EXPECT_EQ(100, bad_blocks[1]);
}

constexpr uint32_t kControl2[] = {
    0x00010001, 0x00000002, 0x85241afd, 0x0000012c, 0x00040000, 0x0000012b, 0x0000012a, 0x0000011c,
    0x00000129, 0xffffffff, 0x00000001, 0x0000012c, 0x00000000, 0x0000011b, 0xffffffff, 0xffffffff,
    0x00000000, 0x0000011b, 0x006c7466, 0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0xffffffff};

TEST(NdmData, Replacements) {
  NdmData ndm;
  fbl::Vector<int32_t> bad_blocks;
  fbl::Vector<int32_t> replacements;
  ndm.ParseNdmData(kControl2, &bad_blocks, &replacements);

  ASSERT_EQ(1, bad_blocks.size());
  ASSERT_EQ(1, replacements.size());
  EXPECT_EQ(0, bad_blocks[0]);
  EXPECT_EQ(283, replacements[0]);
}

constexpr uint32_t kControl3[] = {
    0x00010001, 0x00000002, 0xb97253b3, 0x0000012c, 0x00040000, 0x0000012b, 0x0000012a, 0x0000011e,
    0x00000129, 0xffffffff, 0x00000001, 0x0000002a, 0x00000064, 0x0000012c, 0x00000000, 0x0000011d,
    0xffffffff, 0xffffffff, 0x00000000, 0x0000011b, 0x006c7466, 0x00000000, 0x00000000, 0x00000000};

TEST(NdmData, BothBadBlockTypes) {
  NdmData ndm;
  fbl::Vector<int32_t> bad_blocks;
  fbl::Vector<int32_t> replacements;
  ndm.ParseNdmData(kControl3, &bad_blocks, &replacements);

  ASSERT_EQ(3, bad_blocks.size());
  ASSERT_EQ(1, replacements.size());
  EXPECT_EQ(42, bad_blocks[0]);
  EXPECT_EQ(100, bad_blocks[1]);
  EXPECT_EQ(0, bad_blocks[2]);
  EXPECT_EQ(285, replacements[0]);
}

constexpr uint32_t kControl4[] = {
    0x00010001, 0x00000002, 0x19a0c54b, 0x0000012c, 0x00040000, 0x0000012b, 0x0000012a, 0x0000011c,
    0x00000129, 0x0000011c, 0x0000011b, 0x0000002d, 0x00000102, 0x00012c00, 0x00000000, 0x00011b00,
    0x00011b00, 0x00011c00, 0xffffff00, 0xffffffff, 0x000000ff, 0x00011b00, 0x6c746600, 0x00000000,
    0x00000000, 0x00000000, 0xffffff00, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};

TEST(NdmData, Transitional) {
  NdmData ndm;
  fbl::Vector<int32_t> bad_blocks;
  fbl::Vector<int32_t> replacements;
  ndm.ParseNdmData(kControl4, &bad_blocks, &replacements);

#if defined(__arm__) || defined(__aarch64__)
  ASSERT_EQ(0, bad_blocks.size());
  ASSERT_EQ(0, replacements.size());
#else
  ASSERT_EQ(2, bad_blocks.size());
  ASSERT_EQ(2, replacements.size());
  EXPECT_EQ(0, bad_blocks[0]);
  EXPECT_EQ(283, bad_blocks[1]);
  EXPECT_EQ(283, replacements[0]);
  EXPECT_EQ(284, replacements[1]);
#endif
}

// Goes over a few members to verify that the proper shift is taking place.
TEST(GetHeader, Version1Basic) {
  NdmHeader header = GetNdmHeader(kControl2);
  EXPECT_EQ(1, header.major_version);
  EXPECT_EQ(1, header.minor_version);
  EXPECT_EQ(0x12c, header.num_blocks);
  EXPECT_EQ(0x12a, header.control_block1);
}

TEST(GetHeader, Version1Transitional) {
  NdmHeader header = GetNdmHeader(kControl4);
  EXPECT_EQ(1, header.major_version);
  EXPECT_EQ(1, header.minor_version);
  EXPECT_EQ(0x12c, header.num_blocks);
  EXPECT_EQ(0x12a, header.control_block1);
  EXPECT_EQ(0x11c, header.transfer_to_block);
  EXPECT_EQ(0x11b, header.transfer_bad_block);
  EXPECT_EQ(0x2d, header.transfer_bad_page);
}

constexpr uint32_t kControlBlockBadBlocksV2[] = {
    0x00000002, 0x00010001, 0x00000002, 0x01148752, 0x0000001e, 0x00010000, 0x0000001d, 0x0000001c,
    0xffffffff, 0xffffffff, 0xffffffff, 0x00000003, 0x0000000d, 0x00000001, 0x00000000, 0x0000001e,
    0x00000003, 0x0000001b, 0xffffffff, 0xffffffff, 0x00000000, 0x0000001a, 0x006c7466, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};

TEST(GetHeader, Version2Basic) {
  NdmHeader header = GetNdmHeader(kControlBlockBadBlocksV2);
  EXPECT_EQ(2, header.major_version);
  EXPECT_EQ(0, header.minor_version);
  EXPECT_EQ(0x1e, header.num_blocks);
  EXPECT_EQ(0x1c, header.control_block1);
}

constexpr uint32_t kControlBlockTransferV2[] = {
    0x00000002, 0x00010001, 0x00000001, 0xdc1fd63c, 0x0000001e, 0x00010000, 0x0000001d, 0x0000001c,
    0xffffffff, 0xffffffff, 0x0000001b, 0x00000003, 0x0000000d, 0x00000001, 0x00000000, 0x0000001e,
    0x00000003, 0x0000001b, 0xffffffff, 0xffffffff, 0x00000000, 0x0000001a, 0x006c7466, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};

TEST(GetHeader, Version2Transitional) {
  NdmHeader header = GetNdmHeader(kControlBlockTransferV2);
  EXPECT_EQ(2, header.major_version);
  EXPECT_EQ(0, header.minor_version);
  EXPECT_EQ(0x1e, header.num_blocks);
  EXPECT_EQ(0x1c, header.control_block1);
  EXPECT_EQ(0x1b, header.transfer_to_block);
  EXPECT_EQ(0x3, header.transfer_bad_block);
  EXPECT_EQ(0xd, header.transfer_bad_page);
}

TEST(NdmData, BothBadBlockTypesVersion2) {
  NdmData ndm;
  fbl::Vector<int32_t> bad_blocks;
  fbl::Vector<int32_t> replacements;
  ndm.ParseNdmData(kControlBlockBadBlocksV2, &bad_blocks, &replacements);

  ASSERT_EQ(2, bad_blocks.size());
  ASSERT_EQ(1, replacements.size());
  EXPECT_EQ(0, bad_blocks[0]);
  EXPECT_EQ(3, bad_blocks[1]);
  EXPECT_EQ(27, replacements[0]);
}

TEST(NdmData, TransitionalVersion2) {
  NdmData ndm;
  fbl::Vector<int32_t> bad_blocks;
  fbl::Vector<int32_t> replacements;
  ndm.ParseNdmData(kControlBlockTransferV2, &bad_blocks, &replacements);

  ASSERT_EQ(2, bad_blocks.size());
  ASSERT_EQ(1, replacements.size());
  EXPECT_EQ(0, bad_blocks[0]);
  EXPECT_EQ(3, bad_blocks[1]);
  EXPECT_EQ(27, replacements[0]);
}

}  // namespace internal

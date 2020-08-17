// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mbr.h"

#include <zxtest/zxtest.h>

#include "mbr-test-data.h"

namespace mbr {

TEST(MbrTest, ParseShortBuffer) {
  Mbr mbr;
  uint8_t buffer[511] = {0};
  EXPECT_EQ(mbr::Parse(buffer, sizeof(buffer), &mbr), ZX_ERR_BUFFER_TOO_SMALL);
}

TEST(MbrTest, InvalidBootSignature) {
  Mbr mbr;
  uint8_t buffer[512];
  memcpy(buffer, kFuchsiaMbr, sizeof(buffer));
  buffer[510] = 0x12;
  buffer[511] = 0x34;
  EXPECT_EQ(mbr::Parse(buffer, sizeof(buffer), &mbr), ZX_ERR_NOT_SUPPORTED);
}

TEST(MbrTest, Parse) {
  Mbr mbr;
  EXPECT_OK(mbr::Parse(kFuchsiaMbr, sizeof(kFuchsiaMbr), &mbr));

  MbrPartitionEntry partition;
  memcpy(&partition, &mbr.partitions[0], kMbrPartitionEntrySize);
  EXPECT_EQ(partition.type, kPartitionTypeFuchsiaSys);
  EXPECT_EQ(partition.start_sector_lba, 2048);
  EXPECT_EQ(partition.num_sectors, 20480);

  memcpy(&partition, &mbr.partitions[1], kMbrPartitionEntrySize);
  EXPECT_EQ(partition.type, kPartitionTypeFuchsiaData);
  EXPECT_EQ(partition.start_sector_lba, 22528);
  EXPECT_EQ(partition.num_sectors, 60532736);

  memcpy(&partition, &mbr.partitions[2], kMbrPartitionEntrySize);
  EXPECT_EQ(partition.type, kPartitionTypeNone);

  memcpy(&partition, &mbr.partitions[3], kMbrPartitionEntrySize);
  EXPECT_EQ(partition.type, kPartitionTypeNone);

  EXPECT_EQ(mbr.boot_signature, kMbrBootSignature);
}

TEST(MbrTest, ParseFat) {
  Mbr mbr;
  EXPECT_OK(mbr::Parse(kFatMbr, sizeof(kFatMbr), &mbr));

  MbrPartitionEntry partition;
  memcpy(&partition, &mbr.partitions[0], kMbrPartitionEntrySize);
  EXPECT_EQ(partition.type, kPartitionTypeFat12);
  EXPECT_EQ(partition.start_sector_lba, 2048);
  EXPECT_EQ(partition.num_sectors, 20480);

  memcpy(&partition, &mbr.partitions[1], kMbrPartitionEntrySize);
  EXPECT_EQ(partition.type, kPartitionTypeFat32);
  EXPECT_EQ(partition.start_sector_lba, 22528);
  EXPECT_EQ(partition.num_sectors, 20480);

  memcpy(&partition, &mbr.partitions[2], kMbrPartitionEntrySize);
  EXPECT_EQ(partition.type, kPartitionTypeFat16B);
  EXPECT_EQ(partition.start_sector_lba, 43008);
  EXPECT_EQ(partition.num_sectors, 20480);

  memcpy(&partition, &mbr.partitions[3], kMbrPartitionEntrySize);
  EXPECT_EQ(partition.type, kPartitionTypeNone);

  EXPECT_EQ(mbr.boot_signature, kMbrBootSignature);
}

}  // namespace mbr

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mbr-test-data.h"
#include "mbr.h"

#include <inttypes.h>

#include <zircon/errors.h>
#include <zxtest/zxtest.h>

namespace mbr {

TEST(MbrTest, ParseShortBuffer) {
    Mbr mbr;
    uint8_t buffer[511] = {0};
    EXPECT_EQ(Mbr::Parse(buffer, sizeof(buffer), &mbr), ZX_ERR_BUFFER_TOO_SMALL);
}

TEST(MbrTest, InvalidBootSignature) {
    Mbr mbr;
    uint8_t buffer[512];
    memcpy(buffer, kFuchsiaMbr, sizeof(buffer));
    buffer[510] = 0x12;
    buffer[511] = 0x34;
    EXPECT_EQ(Mbr::Parse(buffer, sizeof(buffer), &mbr), ZX_ERR_NOT_SUPPORTED);
}

TEST(MbrTest, Parse) {
    Mbr mbr;
    EXPECT_OK(Mbr::Parse(kFuchsiaMbr, sizeof(kFuchsiaMbr), &mbr));

    const auto& partition0 = mbr.partitions[0];
    EXPECT_EQ(partition0.type, kPartitionTypeFuchsiaSys);
    EXPECT_EQ(partition0.start_sector_lba, 2048);
    EXPECT_EQ(partition0.num_sectors, 20480);

    const auto& partition1 = mbr.partitions[1];
    EXPECT_EQ(partition1.type, kPartitionTypeFuchsiaData);
    EXPECT_EQ(partition1.start_sector_lba, 22528);
    EXPECT_EQ(partition1.num_sectors, 60532736);

    EXPECT_EQ(mbr.partitions[2].type, kPartitionTypeNone);
    EXPECT_EQ(mbr.partitions[3].type, kPartitionTypeNone);

    EXPECT_EQ(mbr.boot_signature, kMbrBootSignature);
}

} // namespace mbr

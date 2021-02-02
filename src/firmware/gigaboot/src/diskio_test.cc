// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diskio.h"

#include <zircon/hw/gpt.h>

#include <utility>
#include <vector>

#include <gtest/gtest.h>

TEST(GuidValueFromName, KnownPartitionNames) {
  const std::pair<const char*, const std::vector<uint8_t>> known_partitions[] = {
      {"zircon-a", GUID_ZIRCON_A_VALUE}, {"zircon-b", GUID_ZIRCON_B_VALUE},
      {"zircon-r", GUID_ZIRCON_R_VALUE}, {"vbmeta_a", GUID_VBMETA_A_VALUE},
      {"vbmeta_b", GUID_VBMETA_B_VALUE}, {"vbmeta_r", GUID_VBMETA_R_VALUE},
      {"fuchsia-esp", GUID_EFI_VALUE},
  };

  for (const auto& [name, expected_guid] : known_partitions) {
    std::vector<uint8_t> guid(GPT_GUID_LEN);
    EXPECT_EQ(0, guid_value_from_name(name, guid.data()));
    EXPECT_EQ(expected_guid, guid);
  }
}

TEST(GuidValueFromName, UnknownPartitionName) {
  std::vector<uint8_t> guid(GPT_GUID_LEN);
  EXPECT_NE(0, guid_value_from_name("unknown_partition", guid.data()));
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <string>
#include <string_view>

#include <gpt/guid.h>
#include <gtest/gtest.h>

#include "src/storage/volume_image/utils/guid.h"

namespace storage::volume_image {
namespace {

// Returns a copy of |str| with any hex values converted to uppercase.
std::string HexToUpper(std::string str) {
  for (char& ch : str) {
    if (ch >= 'a' && ch <= 'f') {
      ch -= 'a';
      ch += 'A';
    }
  }
  return str;
}

TEST(GuidTest, KnownGuidsMatch) {
  std::array<uint8_t, kGuidLength> known_guid;
  for (const auto& known_guid_property : gpt::KnownGuid()) {
    memcpy(known_guid.data(), known_guid_property.type_guid().bytes(), kGuidLength);
    EXPECT_EQ(known_guid, Guid::FromString(known_guid_property.type_guid().ToString()).value());
    EXPECT_EQ(HexToUpper(known_guid_property.type_guid().ToString()),
              Guid::ToString(known_guid).value());
  }
}

}  // namespace
}  // namespace storage::volume_image

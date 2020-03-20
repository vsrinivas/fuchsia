// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/utils/guid.h"

#include <array>
#include <string>
#include <string_view>

#include <gpt/gpt.h>
#include <gpt/guid.h>
#include <gtest/gtest.h>

namespace storage::volume_image {
namespace {

TEST(GuidTest, ToStringFromSmallBufferIsError) {
  uint8_t buffer[kGuidLength - 1] = {};
  ASSERT_TRUE(Guid::ToString(buffer).is_error());
}

TEST(GuidTest, ToStringFromBigBufferIsError) {
  uint8_t buffer[kGuidLength + 1] = {};
  ASSERT_TRUE(Guid::ToString(buffer).is_error());
}

TEST(GuidTest, ToStringFromExactSizedBufferIsOk) {
  constexpr std::array<uint8_t, kGuidLength> guid = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                                                     0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
                                                     0x0C, 0x0D, 0xE,  0x0F};
  constexpr std::string_view kExpectedGuid = "03020100-0504-0706-0809-0A0B0C0D0E0F";

  auto guid_result = Guid::ToString(guid);

  EXPECT_EQ(kExpectedGuid, guid_result.value());
}

TEST(GuidTest, ToStringIsReverseOperationOfFromString) {
  constexpr std::string_view kGuid = "03020100-0504-0706-0809-0A0B0C0D0E0F";
  auto guid_result = Guid::ToString(Guid::FromString(kGuid).value()).value();
  EXPECT_EQ(kGuid, guid_result);
}

TEST(GuidTest, FromStringFromSmallBufferIsError) {
  constexpr std::array<char, kGuidStrLength - 1> buffer = {};
  ASSERT_TRUE(Guid::FromString(buffer).is_error());
}

TEST(GuidTest, FromStringFromBigBufferIsError) {
  constexpr std::array<char, kGuidStrLength + 1> buffer = {};
  ASSERT_TRUE(Guid::FromString(buffer).is_error());
}

TEST(GuidTest, FromStringFromExactSizedBufferIsOk) {
  constexpr std::array<uint8_t, kGuidLength> kExpectedGuid = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                                                              0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
                                                              0x0C, 0x0D, 0xE,  0x0F};
  constexpr std::string_view kGuid = "03020100-0504-0706-0809-0A0B0C0D0E0F";

  auto guid_result = Guid::FromString(kGuid);

  EXPECT_EQ(kExpectedGuid, guid_result.value());
}

TEST(GuidTest, FromStringIsReverseOperationOfToString) {
  constexpr std::array<uint8_t, kGuidLength> kGuid = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                                                      0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
                                                      0x0C, 0x0D, 0xE,  0x0F};
  auto guid_result = Guid::FromString(Guid::ToString(kGuid).value()).value();
  EXPECT_EQ(kGuid, guid_result);
}

TEST(GuidTest, KnownGuidsMatch) {
  std::array<uint8_t, kGuidLength> known_guid;
  for (const auto& known_guid_property : gpt::KnownGuid()) {
    memcpy(known_guid.data(), known_guid_property.guid(), kGuidLength);
    EXPECT_EQ(
        known_guid,
        Guid::FromString(fbl::Span<const char>(known_guid_property.str(), kGuidStrLength)).value());
    EXPECT_EQ(std::string(known_guid_property.str(), kGuidStrLength),
              Guid::ToString(known_guid).value());
  }
}

}  // namespace
}  // namespace storage::volume_image

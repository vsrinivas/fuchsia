// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-shim/boot-shim.h>
#include <lib/boot-shim/test-helper.h>
#include <lib/boot-shim/test-serial-number.h>

#include <zxtest/zxtest.h>

namespace {

using TestShim = boot_shim::BootShim<boot_shim::TestSerialNumberItem>;

TEST(BootShimTests, TestSerialNumberItem) {
  boot_shim::testing::TestHelper test;
  TestShim shim("TestSerialNumberItem", test.log());

  auto [buffer, owner] = test.GetZbiBuffer();
  TestShim::DataZbi zbi(buffer);
  ASSERT_TRUE(zbi.Append({.type = ZBI_TYPE_CMDLINE},
                         boot_shim::testing::Payload("foo bootloader.zbi.serial-number=xyz bar"))
                  .is_ok());

  auto& item = shim.Get<boot_shim::TestSerialNumberItem>();
  auto result = item.Init(TestShim::InputZbi(buffer));
  EXPECT_TRUE(result.is_ok());

  EXPECT_EQ(sizeof(zbi_header_t) + ZBI_ALIGN(3), item.size_bytes());

  EXPECT_TRUE(shim.AppendItems(zbi).is_ok());

  size_t serial_payload_count = 0;
  std::string_view serial_payload;
  for (auto [header, payload] : zbi) {
    if (header->type == ZBI_TYPE_SERIAL_NUMBER) {
      ++serial_payload_count;
      serial_payload = {
          reinterpret_cast<const char*>(payload.data()),
          payload.size(),
      };
    }
  }
  EXPECT_TRUE(zbi.take_error().is_ok());

  EXPECT_EQ(serial_payload_count, 1);
  EXPECT_STREQ(serial_payload, "xyz");
}

TEST(BootShimTests, TestSerialNumberItemNoSwitch) {
  boot_shim::testing::TestHelper test;
  TestShim shim("TestSerialNumberItem", test.log());

  auto [buffer, owner] = test.GetZbiBuffer();
  TestShim::DataZbi zbi(buffer);
  ASSERT_TRUE(
      zbi.Append({.type = ZBI_TYPE_CMDLINE}, boot_shim::testing::Payload("some command line"))
          .is_ok());

  auto& item = shim.Get<boot_shim::TestSerialNumberItem>();
  auto result = item.Init(TestShim::InputZbi(buffer));
  EXPECT_TRUE(result.is_ok());

  EXPECT_EQ(0, item.size_bytes());

  EXPECT_TRUE(shim.AppendItems(zbi).is_ok());

  for (auto [header, payload] : zbi) {
    EXPECT_NE(header->type, ZBI_TYPE_SERIAL_NUMBER);
  }
  EXPECT_TRUE(zbi.take_error().is_ok());
}

TEST(BootShimTests, TestSerialNumberItemHwPresent) {
  boot_shim::testing::TestHelper test;
  TestShim shim("TestSerialNumberItem", test.log());

  auto [buffer, owner] = test.GetZbiBuffer();
  TestShim::DataZbi zbi(buffer);
  ASSERT_TRUE(
      zbi.Append({.type = ZBI_TYPE_SERIAL_NUMBER}, boot_shim::testing::Payload("pdq")).is_ok());
  ASSERT_TRUE(zbi.Append({.type = ZBI_TYPE_CMDLINE},
                         boot_shim::testing::Payload("foo bootloader.zbi.serial-number=xyz bar"))
                  .is_ok());

  auto& item = shim.Get<boot_shim::TestSerialNumberItem>();
  auto result = item.Init(TestShim::InputZbi(buffer));
  EXPECT_TRUE(result.is_ok());

  EXPECT_EQ(0, item.size_bytes());

  EXPECT_TRUE(shim.AppendItems(zbi).is_ok());

  size_t serial_payload_count = 0;
  std::string_view serial_payload;
  for (auto [header, payload] : zbi) {
    if (header->type == ZBI_TYPE_SERIAL_NUMBER) {
      ++serial_payload_count;
      serial_payload = boot_shim::testing::StringPayload(payload);
    }
  }
  EXPECT_TRUE(zbi.take_error().is_ok());

  EXPECT_EQ(serial_payload_count, 1);
  EXPECT_STREQ(serial_payload, "pdq");
}

}  // namespace

// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-shim/boot-shim.h>
#include <lib/boot-shim/debugdata.h>
#include <lib/boot-shim/test-helper.h>
#include <lib/zbitl/items/debugdata.h>
#include <zircon/boot/image.h>

#include <cstring>

#include <zxtest/zxtest.h>

namespace {

using TestShim = boot_shim::BootShim<boot_shim::DebugdataItem>;

constexpr std::string_view kSinkName = "test-sink-name";
constexpr std::string_view kVmoName = "test-vmo-name";
constexpr std::string_view kVmoNameSuffix = ".tst";
constexpr std::string_view kLog = R"""(
When in the Course of human events, it becomes necessary for one people to
dissolve the political bands which have connected them with another, and to
assume among the powers of the earth
)""";

constexpr uint8_t kContents[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};

constexpr size_t kStringsSize =
    kSinkName.size() + kVmoName.size() + kVmoNameSuffix.size() + kLog.size();

constexpr size_t kFullSize =
    ZBI_ALIGN(static_cast<uint32_t>(sizeof(kContents) + kStringsSize)) + sizeof(zbi_debugdata_t);

TEST(BootShimTests, DebugdataItemUninitialized) {
  boot_shim::testing::TestHelper test;
  TestShim shim("DebugdataItem", test.log());

  auto [buffer, owner] = test.GetZbiBuffer();
  TestShim::DataZbi zbi(buffer);

  auto& item = shim.Get<boot_shim::DebugdataItem>();

  EXPECT_EQ(0u, item.size_bytes());

  EXPECT_TRUE(shim.AppendItems(zbi).is_ok());

  size_t debugdata_payload_count = 0;
  zbitl::Debugdata debugdata_payload;
  for (auto [header, payload] : zbi) {
    if (header->type == ZBI_TYPE_DEBUGDATA) {
      ++debugdata_payload_count;
      auto result = debugdata_payload.Init(payload);
      EXPECT_TRUE(result.is_ok());
    }
  }
  EXPECT_TRUE(zbi.take_error().is_ok());

  EXPECT_EQ(debugdata_payload_count, 0);
}

TEST(BootShimTests, DebugdataItemNoContentsNoLOg) {
  boot_shim::testing::TestHelper test;
  TestShim shim("DebugdataItem", test.log());

  auto [buffer, owner] = test.GetZbiBuffer();
  TestShim::DataZbi zbi(buffer);

  auto& item = shim.Get<boot_shim::DebugdataItem>();
  item.Init(kSinkName, kVmoName, kVmoNameSuffix);

  EXPECT_EQ(0u, item.size_bytes());

  EXPECT_TRUE(shim.AppendItems(zbi).is_ok());

  size_t debugdata_payload_count = 0;
  zbitl::Debugdata debugdata_payload;
  for (auto [header, payload] : zbi) {
    if (header->type == ZBI_TYPE_DEBUGDATA) {
      ++debugdata_payload_count;
      auto result = debugdata_payload.Init(payload);
      EXPECT_TRUE(result.is_ok());
    }
  }
  EXPECT_TRUE(zbi.take_error().is_ok());

  EXPECT_EQ(debugdata_payload_count, 0);
}

TEST(BootShimTests, DebugdataItemLogNoContents) {
  boot_shim::testing::TestHelper test;
  TestShim shim("DebugdataItem", test.log());

  auto [buffer, owner] = test.GetZbiBuffer();
  TestShim::DataZbi zbi(buffer);

  auto& item = shim.Get<boot_shim::DebugdataItem>();
  item.Init(kSinkName, kVmoName, kVmoNameSuffix);
  item.set_log(kLog);

  EXPECT_EQ(sizeof(zbi_header_t) + ZBI_ALIGN(static_cast<uint32_t>(kStringsSize)) +
                sizeof(zbi_debugdata_t),
            item.size_bytes());

  EXPECT_TRUE(shim.AppendItems(zbi).is_ok());

  size_t debugdata_payload_count = 0;
  zbitl::Debugdata debugdata_payload;
  for (auto [header, payload] : zbi) {
    if (header->type == ZBI_TYPE_DEBUGDATA) {
      ++debugdata_payload_count;
      auto result = debugdata_payload.Init(payload);
      EXPECT_TRUE(result.is_ok());
    }
  }
  EXPECT_TRUE(zbi.take_error().is_ok());

  EXPECT_EQ(debugdata_payload_count, 1);
  EXPECT_EQ(debugdata_payload.sink_name(), kSinkName);
  EXPECT_EQ(debugdata_payload.vmo_name(), std::string(kVmoName) + std::string(kVmoNameSuffix));
  EXPECT_EQ(debugdata_payload.log(), kLog);

  EXPECT_EQ(debugdata_payload.contents().size_bytes(), 0u);
  EXPECT_EQ(item.contents().size_bytes(), 0u);
}

TEST(BootShimTests, DebugdataItemContents) {
  boot_shim::testing::TestHelper test;
  TestShim shim("DebugdataItem", test.log());

  auto [buffer, owner] = test.GetZbiBuffer();
  TestShim::DataZbi zbi(buffer);

  auto& item = shim.Get<boot_shim::DebugdataItem>();
  item.Init(kSinkName, kVmoName, kVmoNameSuffix);
  item.set_log(kLog);
  item.set_content_size(sizeof(kContents));

  EXPECT_EQ(sizeof(zbi_header_t) + kFullSize, item.size_bytes());

  EXPECT_TRUE(shim.AppendItems(zbi).is_ok());

  size_t debugdata_payload_count = 0;
  zbitl::Debugdata debugdata_payload;
  for (auto [header, payload] : zbi) {
    if (header->type == ZBI_TYPE_DEBUGDATA) {
      ++debugdata_payload_count;
      auto result = debugdata_payload.Init(payload);
      EXPECT_TRUE(result.is_ok());
    }
  }
  EXPECT_TRUE(zbi.take_error().is_ok());

  EXPECT_EQ(debugdata_payload_count, 1);
  EXPECT_EQ(debugdata_payload.sink_name(), kSinkName);
  EXPECT_EQ(debugdata_payload.vmo_name(), std::string(kVmoName) + std::string(kVmoNameSuffix));
  EXPECT_EQ(debugdata_payload.log(), kLog);

  EXPECT_EQ(debugdata_payload.contents().size_bytes(), sizeof(kContents));
  EXPECT_EQ(item.contents().size_bytes(), sizeof(kContents));
  EXPECT_EQ(item.contents().data(), debugdata_payload.mutable_contents().data());
}

}  // namespace

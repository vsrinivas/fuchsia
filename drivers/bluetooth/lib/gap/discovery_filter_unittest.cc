// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/bluetooth/lib/gap/discovery_filter.h"

#include "gtest/gtest.h"

#include "apps/bluetooth/lib/hci/low_energy_scanner.h"

namespace bluetooth {
namespace gap {
namespace {

TEST(DiscoveryFilterTest, Flags) {
  hci::LowEnergyScanResult result;

  const common::BufferView kEmptyData;
  const auto kInvalidFlagsData = common::CreateStaticByteBuffer(0x01, 0x01);
  const auto kValidFlagsData = common::CreateStaticByteBuffer(0x02, 0x01, 0b101);

  DiscoveryFilter filter;

  // Empty filter should match everything.
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kEmptyData));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kInvalidFlagsData));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kValidFlagsData));

  filter.set_flags(0b100);
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kEmptyData));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kInvalidFlagsData));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kValidFlagsData));

  filter.set_flags(0b001);
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kEmptyData));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kInvalidFlagsData));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kValidFlagsData));

  // The following filters set multiple bits. As long as one of them is set, the filter should
  // match.
  filter.set_flags(0b101);
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kEmptyData));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kInvalidFlagsData));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kValidFlagsData));

  filter.set_flags(0b111);
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kEmptyData));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kInvalidFlagsData));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kValidFlagsData));

  filter.set_flags(0b011);
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kEmptyData));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kInvalidFlagsData));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kValidFlagsData));

  filter.set_flags(0b010);
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kEmptyData));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kInvalidFlagsData));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kValidFlagsData));

  // The following filters requre that *all* bits be present in the advertising data.
  filter.set_flags(0b101, true);
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kEmptyData));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kInvalidFlagsData));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kValidFlagsData));

  filter.set_flags(0b111, true);
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kEmptyData));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kInvalidFlagsData));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kValidFlagsData));

  filter.set_flags(0b011, true);
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kEmptyData));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kInvalidFlagsData));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kValidFlagsData));

  filter.set_flags(0b010, true);
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kEmptyData));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kInvalidFlagsData));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kValidFlagsData));
}

TEST(DiscoveryFilterTest, Connectable) {
  hci::LowEnergyScanResult conn_result, non_conn_result;
  conn_result.connectable = true;
  common::BufferView empty;
  DiscoveryFilter filter;

  // Empty filter should match both.
  EXPECT_TRUE(filter.MatchLowEnergyResult(conn_result, empty));
  EXPECT_TRUE(filter.MatchLowEnergyResult(non_conn_result, empty));

  // Filter connectable.
  filter.set_connectable(true);
  EXPECT_TRUE(filter.MatchLowEnergyResult(conn_result, empty));
  EXPECT_FALSE(filter.MatchLowEnergyResult(non_conn_result, empty));

  // Filter not connectable.
  filter.set_connectable(false);
  EXPECT_FALSE(filter.MatchLowEnergyResult(conn_result, empty));
  EXPECT_TRUE(filter.MatchLowEnergyResult(non_conn_result, empty));
}

TEST(DiscoveryFilterTest, 16BitServiceUUIDs) {
  hci::LowEnergyScanResult result;

  constexpr uint16_t kUUID0 = 0x180d;
  constexpr uint16_t kUUID1 = 0x1800;

  const common::BufferView kEmptyData;

  // Below, "Incomplete" refers to the "Incomplete Service UUIDs" field while "Complete" refers to
  // "Complete Service UUIDs".

  const auto kIncompleteEmpty = common::CreateStaticByteBuffer(0x01, 0x02);
  const auto kIncompleteNoMatch =
      common::CreateStaticByteBuffer(0x05, 0x02, 0x01, 0x02, 0x03, 0x04);
  const auto kIncompleteMatch1 = common::CreateStaticByteBuffer(0x05, 0x02, 0x01, 0x02, 0x0d, 0x18);
  const auto kIncompleteMatch2 = common::CreateStaticByteBuffer(0x05, 0x02, 0x00, 0x18, 0x03, 0x04);
  const auto kCompleteEmpty = common::CreateStaticByteBuffer(0x01, 0x03);
  const auto kCompleteNoMatch = common::CreateStaticByteBuffer(0x05, 0x03, 0x01, 0x02, 0x03, 0x04);
  const auto kCompleteMatch1 = common::CreateStaticByteBuffer(0x05, 0x03, 0x01, 0x02, 0x0d, 0x18);
  const auto kCompleteMatch2 = common::CreateStaticByteBuffer(0x05, 0x03, 0x00, 0x18, 0x03, 0x04);

  DiscoveryFilter filter;

  // An empty filter should match all payloads.
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kEmptyData));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kIncompleteEmpty));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kIncompleteNoMatch));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kIncompleteMatch1));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kIncompleteMatch2));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kCompleteEmpty));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kCompleteNoMatch));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kCompleteMatch1));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kCompleteMatch2));

  // Filter for kUUID0 and kUUID1.
  filter.set_service_uuids(std::vector<common::UUID>{common::UUID(kUUID0), common::UUID(kUUID1)});
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kEmptyData));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kIncompleteEmpty));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kIncompleteNoMatch));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kIncompleteMatch1));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kIncompleteMatch2));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kCompleteEmpty));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kCompleteNoMatch));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kCompleteMatch1));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kCompleteMatch2));
}

TEST(DiscoveryFilterTest, 32BitServiceUUIDs) {
  hci::LowEnergyScanResult result;

  constexpr uint16_t kUUID0 = 0x180d;
  constexpr uint16_t kUUID1 = 0x1800;

  const common::BufferView kEmptyData;

  // Below, "Incomplete" refers to the "Incomplete Service UUIDs" field while "Complete" refers to
  // "Complete Service UUIDs".

  const auto kIncompleteEmpty = common::CreateStaticByteBuffer(0x01, 0x04);
  const auto kIncompleteNoMatch =
      common::CreateStaticByteBuffer(0x09, 0x04, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08);
  const auto kIncompleteMatch1 =
      common::CreateStaticByteBuffer(0x09, 0x04, 0x01, 0x02, 0x03, 0x04, 0x0d, 0x18, 0x00, 0x00);
  const auto kIncompleteMatch2 =
      common::CreateStaticByteBuffer(0x09, 0x04, 0x00, 0x18, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04);
  const auto kCompleteEmpty = common::CreateStaticByteBuffer(0x01, 0x05);
  const auto kCompleteNoMatch =
      common::CreateStaticByteBuffer(0x09, 0x05, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08);
  const auto kCompleteMatch1 =
      common::CreateStaticByteBuffer(0x09, 0x05, 0x01, 0x02, 0x03, 0x04, 0x0d, 0x18, 0x00, 0x00);
  const auto kCompleteMatch2 =
      common::CreateStaticByteBuffer(0x09, 0x05, 0x00, 0x18, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04);

  DiscoveryFilter filter;

  // An empty filter should match all payloads.
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kEmptyData));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kIncompleteEmpty));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kIncompleteNoMatch));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kIncompleteMatch1));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kIncompleteMatch2));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kCompleteEmpty));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kCompleteNoMatch));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kCompleteMatch1));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kCompleteMatch2));

  // Filter for kUUID0 and kUUID1.
  filter.set_service_uuids(std::vector<common::UUID>{common::UUID(kUUID0), common::UUID(kUUID1)});
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kEmptyData));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kIncompleteEmpty));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kIncompleteNoMatch));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kIncompleteMatch1));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kIncompleteMatch2));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kCompleteEmpty));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kCompleteNoMatch));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kCompleteMatch1));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kCompleteMatch2));
}

TEST(DiscoveryFilterTest, 128BitServiceUUIDs) {
  hci::LowEnergyScanResult result;

  constexpr uint16_t kUUID0 = 0x180d;
  constexpr uint16_t kUUID1 = 0x1800;

  const common::BufferView kEmptyData;

  // Below, "Incomplete" refers to the "Incomplete Service UUIDs" field while "Complete" refers to
  // "Complete Service UUIDs".

  const auto kIncompleteEmpty = common::CreateStaticByteBuffer(0x01, 0x06);
  const auto kIncompleteNoMatch =
      common::CreateStaticByteBuffer(0x11, 0x06,

                                     // UUID
                                     0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
                                     0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F);
  const auto kIncompleteMatch1 =
      common::CreateStaticByteBuffer(0x21, 0x06,

                                     // UUID 1
                                     0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
                                     0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,

                                     // UUID 2
                                     0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10,
                                     0x00, 0x00, 0x0d, 0x18, 0x00, 0x00);
  const auto kIncompleteMatch2 =
      common::CreateStaticByteBuffer(0x21, 0x06,

                                     // UUID 1
                                     0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10,
                                     0x00, 0x00, 0x00, 0x18, 0x00, 0x00,

                                     // UUID 2
                                     0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
                                     0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F);
  const auto kCompleteEmpty = common::CreateStaticByteBuffer(0x01, 0x07);
  const auto kCompleteNoMatch =
      common::CreateStaticByteBuffer(0x11, 0x07,

                                     // UUID
                                     0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
                                     0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F);
  const auto kCompleteMatch1 =
      common::CreateStaticByteBuffer(0x21, 0x07,

                                     // UUID 1
                                     0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
                                     0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,

                                     // UUID 2
                                     0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10,
                                     0x00, 0x00, 0x0d, 0x18, 0x00, 0x00);
  const auto kCompleteMatch2 =
      common::CreateStaticByteBuffer(0x21, 0x07,

                                     // UUID 1
                                     0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10,
                                     0x00, 0x00, 0x00, 0x18, 0x00, 0x00,

                                     // UUID 2
                                     0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
                                     0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F);

  DiscoveryFilter filter;

  // An empty filter should match all payloads.
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kEmptyData));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kIncompleteEmpty));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kIncompleteNoMatch));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kIncompleteMatch1));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kIncompleteMatch2));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kCompleteEmpty));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kCompleteNoMatch));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kCompleteMatch1));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kCompleteMatch2));

  // Filter for kUUID0 and kUUID1.
  filter.set_service_uuids(std::vector<common::UUID>{common::UUID(kUUID0), common::UUID(kUUID1)});
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kEmptyData));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kIncompleteEmpty));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kIncompleteNoMatch));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kIncompleteMatch1));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kIncompleteMatch2));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kCompleteEmpty));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kCompleteNoMatch));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kCompleteMatch1));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kCompleteMatch2));
}

TEST(DiscoveryFilterTest, NameSubstring) {
  hci::LowEnergyScanResult result;

  const common::BufferView kEmptyData;
  const auto kShortenedName = common::CreateStaticByteBuffer(0x05, 0x08, 'T', 'e', 's', 't');
  const auto kCompleteName = common::CreateStaticByteBuffer(0x0E, 0x09, 'T', 'e', 's', 't', ' ',
                                                            'C', 'o', 'm', 'p', 'l', 'e', 't', 'e');

  DiscoveryFilter filter;

  // An empty filter should match all payloads.
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kEmptyData));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kShortenedName));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kCompleteName));

  // Assigning an empty string for the name filter should have the same effect as an empty filter.
  filter.set_name_substring("");
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kEmptyData));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kShortenedName));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kCompleteName));

  filter.set_name_substring("foo");
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kEmptyData));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kShortenedName));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kCompleteName));

  filter.set_name_substring("est");
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kEmptyData));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kShortenedName));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kCompleteName));

  filter.set_name_substring("Compl");
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kEmptyData));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kShortenedName));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kCompleteName));
}

TEST(DiscoveryFilterTest, RSSI) {
  constexpr int8_t kRSSIThreshold = 60;
  hci::LowEnergyScanResult result;
  const common::BufferView kEmptyData;

  DiscoveryFilter filter;
  filter.set_rssi(hci::kRSSIInvalid);

  // |result| reports an invalid RSSI. This should fail to match even though the value numerically
  // satisfies the filter.
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kEmptyData));

  filter.set_rssi(kRSSIThreshold);
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kEmptyData));

  result.rssi = kRSSIThreshold;
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kEmptyData));

  result.rssi++;
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kEmptyData));

  // When a pathloss filter value is set and the scan result does not satisfy it because it didn't
  // include the transmission power level, the filter should match since an RSSI value has been set
  // which was used as a fallback.
  filter.set_pathloss(5);
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kEmptyData));

  // Finally, an empty filter should always succeed.
  filter.Reset();
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kEmptyData));
}

TEST(DiscoveryFilterTest, Pathloss) {
  constexpr int8_t kPathlossThreshold = 70;
  constexpr int8_t kTxPower = 5;
  constexpr int8_t kMatchingRSSI = -65;
  constexpr int8_t kNotMatchingRSSI = -66;
  constexpr int8_t kTooLargeRSSI = 71;

  hci::LowEnergyScanResult result;
  const common::BufferView kEmptyData;
  const auto kDataWithTxPower = common::CreateStaticByteBuffer(0x02, 0x0A, kTxPower);

  DiscoveryFilter filter;
  filter.set_pathloss(kPathlossThreshold);

  // No Tx Power and no RSSI. Filter should not match.
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kEmptyData));

  // Tx Power is reported but RSSI is unknown. Filter should not match.
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kDataWithTxPower));

  // RSSI is known but Tx Power is not reported.
  result.rssi = kMatchingRSSI;
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kEmptyData));

  // RSSI and Tx Power are present and pathloss is within threshold.
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kDataWithTxPower));

  // RSSI and Tx Power are present but RSSI is larger than Tx Power.
  result.rssi = kTooLargeRSSI;
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kDataWithTxPower));

  // RSSI and Tx Power are present but pathloss is above threshold.
  result.rssi = kNotMatchingRSSI;
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kDataWithTxPower));

  // Assign a RSSI filter. Even though this field alone WOULD satisfy the filter, the match
  // function should not fall back to it when Tx Power is present and the pathloss filter is
  // unsatisfied.
  filter.set_rssi(kNotMatchingRSSI);
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kDataWithTxPower));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kEmptyData));

  // Finally, an empty filter should always succeed.
  filter.Reset();
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kDataWithTxPower));
}

TEST(DiscoveryFilterTest, ManufacturerCode) {
  hci::LowEnergyScanResult result;

  const common::BufferView kEmptyData;
  const auto kValidData0 = common::CreateStaticByteBuffer(0x03, 0xFF, 0xE0, 0x00);
  const auto kValidData1 = common::CreateStaticByteBuffer(0x06, 0xFF, 0xE0, 0x00, 0x01, 0x02, 0x03);
  const auto kInvalidData0 = common::CreateStaticByteBuffer(0x02, 0xFF, 0xE0);
  const auto kInvalidData1 = common::CreateStaticByteBuffer(0x03, 0xFF, 0x4C, 0x00);

  DiscoveryFilter filter;

  // Empty filter should match everything.
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kEmptyData));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kValidData0));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kValidData1));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kInvalidData0));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kInvalidData1));

  filter.set_manufacturer_code(0x00E0);
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kEmptyData));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kValidData0));
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kValidData1));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kInvalidData0));
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kInvalidData1));
}

TEST(DiscoveryFilterTest, Combined) {
  constexpr int8_t kMatchingPathlossThreshold = 70;
  constexpr int8_t kNotMatchingPathlossThreshold = 69;
  constexpr int8_t kTxPower = 5;
  constexpr int8_t kRSSI = -65;

  constexpr uint16_t kMatchingUUID = 0x180d;
  constexpr uint16_t kNotMatchingUUID = 0x1800;

  constexpr char kMatchingName[] = "test";
  constexpr char kNotMatchingName[] = "foo";

  hci::LowEnergyScanResult result;
  result.connectable = true;
  result.rssi = kRSSI;

  const auto kAdvertisingData = common::CreateStaticByteBuffer(
      // Flags
      0x02, 0x01, 0x01,

      // 16 Bit UUIDs
      0x03, 0x02, 0x0d, 0x18,

      // Complete name
      0x05, 0x09, 't', 'e', 's', 't',

      // Tx Power Level
      0x02, 0x0A, kTxPower,

      // Manufacturer specific data
      0x05, 0xFF, 0xE0, 0x00, 0x01, 0x02);

  DiscoveryFilter filter;

  // Empty filter should match.
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kAdvertisingData));

  // Assign all fields and make them match.
  filter.set_flags(0x01);
  filter.set_connectable(true);
  filter.set_service_uuids(std::vector<common::UUID>{common::UUID(kMatchingUUID)});
  filter.set_name_substring(kMatchingName);
  filter.set_pathloss(kMatchingPathlossThreshold);
  filter.set_manufacturer_code(0x00E0);
  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kAdvertisingData));

  // Toggle each field one by one to test that a single mismatch causes the filter to fail.
  filter.set_flags(0x03, true);
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kAdvertisingData));
  filter.set_flags(0x01);

  filter.set_connectable(false);
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kAdvertisingData));
  filter.set_connectable(true);

  filter.set_service_uuids(std::vector<common::UUID>{common::UUID(kNotMatchingUUID)});
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kAdvertisingData));
  filter.set_service_uuids(std::vector<common::UUID>{common::UUID(kMatchingUUID)});

  filter.set_name_substring(kNotMatchingName);
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kAdvertisingData));
  filter.set_name_substring(kMatchingName);

  filter.set_pathloss(kNotMatchingPathlossThreshold);
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kAdvertisingData));
  filter.set_pathloss(kMatchingPathlossThreshold);

  filter.set_manufacturer_code(0x004C);
  EXPECT_FALSE(filter.MatchLowEnergyResult(result, kAdvertisingData));
  filter.set_manufacturer_code(0x00E0);

  EXPECT_TRUE(filter.MatchLowEnergyResult(result, kAdvertisingData));
}

}  // namespace
}  // namespace gap
}  // namespace bluetooth

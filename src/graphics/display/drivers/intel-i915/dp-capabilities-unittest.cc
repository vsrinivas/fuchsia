// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "dp-display.h"

namespace {

constexpr uint8_t kDefaultLaneCount = 2;
constexpr size_t kMaxLinkRateTableEntries =
    (dpcd::DPCD_SUPPORTED_LINK_RATE_END + 1 - dpcd::DPCD_SUPPORTED_LINK_RATE_START) / 2;

class FakeDpcdChannel : public i915::DpcdChannel {
 public:
  FakeDpcdChannel() { registers.fill(0); }

  // Populates the bare minimum of required fields to form a valid set of capabilities.
  void SetDefaults() {
    SetDpcdRevision(dpcd::Revision::k1_4);
    SetMaxLaneCount(kDefaultLaneCount);
    SetMaxLinkRate(dpcd::LinkBw::k1620Mbps);
  }

  void SetDpcdRevision(dpcd::Revision rev) {
    registers[dpcd::DPCD_REV] = static_cast<uint8_t>(rev);
  }

  void SetMaxLinkRate(uint8_t value) { registers[dpcd::DPCD_MAX_LINK_RATE] = value; }

  void SetMaxLaneCount(uint8_t value) { registers[dpcd::DPCD_MAX_LANE_COUNT] = value; }

  void SetSinkCount(uint8_t value) { registers[dpcd::DPCD_SINK_COUNT] = value; }

  void SetEdpCapable(dpcd::EdpRevision rev) {
    dpcd::EdpConfigCap reg;
    reg.set_dpcd_display_ctrl_capable(1);
    registers[dpcd::DPCD_EDP_CONFIG] = reg.reg_value();
    registers[dpcd::DPCD_EDP_REV] = static_cast<uint8_t>(rev);
  }

  void PopulateLinkRateTable(std::vector<uint16_t> values) {
    std::memset(registers.data() + dpcd::DPCD_SUPPORTED_LINK_RATE_START, 0,
                kMaxLinkRateTableEntries * 2);
    for (unsigned i = 0; i < values.size() && i < kMaxLinkRateTableEntries; i++) {
      unsigned offset = dpcd::DPCD_SUPPORTED_LINK_RATE_START + (i * 2);
      registers[offset] = static_cast<uint8_t>(values[i] & 0xFF);
      registers[offset + 1] = (values[i] >> 8);
    }
  }

  // i915::DpcdChannel override:
  bool DpcdRead(uint32_t addr, uint8_t* buf, size_t size) override {
    if (addr + size > registers.size()) {
      return false;
    }

    std::memcpy(buf, registers.data() + addr, size);
    return true;
  }

  // i915::DpcdChannel override:
  bool DpcdWrite(uint32_t addr, const uint8_t* buf, size_t size) override {
    if (addr + size > registers.size()) {
      return false;
    }

    std::memcpy(registers.data() + addr, buf, size);
    return true;
  }

  // The full DPCD field mapping spans addresses 0x00000-0xFFFFF however it's sufficient for us to
  // allocate only the subset that the driver uses. 0x800 contains all addresses up to and
  // including eDP-specific registers (see eDP v1.4a, 2.9.3 "DPCD Field Address Mapping").
  std::array<uint8_t, 0x800> registers;
};

TEST(DpCapabilitiesTest, NoSupportedLinkRates) {
  inspect::Node node;
  FakeDpcdChannel fake_dpcd;

  auto cap = i915::DpCapabilities::Read(&fake_dpcd, &node);
  EXPECT_TRUE(cap.is_error());
}

// Tests that invalid lane counts are rejected.
TEST(DpCapabilitiesTest, InvalidMaxLaneCount) {
  inspect::Node node;
  FakeDpcdChannel fake_dpcd;
  fake_dpcd.SetMaxLinkRate(dpcd::LinkBw::k1620Mbps);

  fake_dpcd.SetMaxLaneCount(0);
  EXPECT_TRUE(i915::DpCapabilities::Read(&fake_dpcd, &node).is_error());

  fake_dpcd.SetMaxLaneCount(3);
  EXPECT_TRUE(i915::DpCapabilities::Read(&fake_dpcd, &node).is_error());

  fake_dpcd.SetMaxLaneCount(5);
  EXPECT_TRUE(i915::DpCapabilities::Read(&fake_dpcd, &node).is_error());
}

// Tests that the basic set of getters work for non-EDP.
TEST(DpCapabilitiesTest, BasicFields) {
  inspect::Node node;
  FakeDpcdChannel fake_dpcd;

  fake_dpcd.SetDpcdRevision(dpcd::Revision::k1_4);
  fake_dpcd.SetMaxLaneCount(kDefaultLaneCount);
  fake_dpcd.SetMaxLinkRate(dpcd::LinkBw::k1620Mbps);
  fake_dpcd.SetSinkCount(1);

  auto cap = i915::DpCapabilities::Read(&fake_dpcd, &node);
  ASSERT_TRUE(cap.is_ok());
  EXPECT_EQ(dpcd::Revision::k1_4, cap.value().dpcd_revision());
  EXPECT_EQ(kDefaultLaneCount, cap.value().max_lane_count());
  EXPECT_EQ(1u, cap.value().sink_count());
  EXPECT_EQ(1u, cap.value().supported_link_rates_mbps().size());

  // eDP capabilities should be unavailable.
  EXPECT_EQ(std::nullopt, cap.value().edp_revision());
  EXPECT_FALSE(cap.value().backlight_aux_power());
  EXPECT_FALSE(cap.value().backlight_aux_brightness());
}

// Tests that eDP registers are processed when supported.
TEST(DpCapabilitiesTest, EdpRegisters) {
  inspect::Node node;
  FakeDpcdChannel fake_dpcd;
  fake_dpcd.SetDefaults();
  fake_dpcd.SetEdpCapable(dpcd::EdpRevision::k1_2);

  auto cap = i915::DpCapabilities::Read(&fake_dpcd, &node);
  ASSERT_TRUE(cap.is_ok());
  EXPECT_EQ(std::optional{dpcd::EdpRevision::k1_2}, cap.value().edp_revision());
  EXPECT_FALSE(cap.value().backlight_aux_power());
  EXPECT_FALSE(cap.value().backlight_aux_brightness());
}

TEST(DpCapabilitiesTest, EdpBacklight) {
  inspect::Node node;
  FakeDpcdChannel fake_dpcd;
  fake_dpcd.SetDefaults();
  fake_dpcd.SetEdpCapable(dpcd::EdpRevision::k1_2);

  dpcd::EdpGeneralCap1 gc;
  gc.set_tcon_backlight_adjustment_cap(1);
  gc.set_backlight_aux_enable_cap(1);
  fake_dpcd.registers[dpcd::DPCD_EDP_GENERAL_CAP1] = gc.reg_value();

  dpcd::EdpBacklightCap bc;
  bc.set_brightness_aux_set_cap(1);
  fake_dpcd.registers[dpcd::DPCD_EDP_BACKLIGHT_CAP] = bc.reg_value();

  auto cap = i915::DpCapabilities::Read(&fake_dpcd, &node);
  ASSERT_TRUE(cap.is_ok());
  EXPECT_TRUE(cap.value().backlight_aux_power());
  EXPECT_TRUE(cap.value().backlight_aux_brightness());
}

// Tests that the list of supported link rates is populated correctly using the "Max Link Rate"
// method.
TEST(DpCapabilitiesTest, MaxLinkRate1620NoEdp) {
  inspect::Node node;
  FakeDpcdChannel fake_dpcd;
  fake_dpcd.SetDefaults();
  fake_dpcd.SetMaxLinkRate(dpcd::LinkBw::k1620Mbps);

  auto cap = i915::DpCapabilities::Read(&fake_dpcd, &node);
  ASSERT_TRUE(cap.is_ok());
  EXPECT_FALSE(cap.value().use_link_rate_table());
  ASSERT_EQ(1u, cap.value().supported_link_rates_mbps().size());
  EXPECT_EQ(1620, cap.value().supported_link_rates_mbps()[0]);
}

// Tests that the list of supported link rates is populated correctly using the "Max Link Rate"
// method.
TEST(DpCapabilitiesTest, MaxLinkRate2700NoEdp) {
  inspect::Node node;
  FakeDpcdChannel fake_dpcd;
  fake_dpcd.SetDefaults();
  fake_dpcd.SetMaxLinkRate(dpcd::LinkBw::k2700Mbps);

  auto cap = i915::DpCapabilities::Read(&fake_dpcd, &node);
  ASSERT_TRUE(cap.is_ok());
  EXPECT_FALSE(cap.value().use_link_rate_table());
  ASSERT_EQ(2u, cap.value().supported_link_rates_mbps().size());
  EXPECT_EQ(1620, cap.value().supported_link_rates_mbps()[0]);
  EXPECT_EQ(2700, cap.value().supported_link_rates_mbps()[1]);
}

// Tests that the list of supported link rates is populated correctly using the "Max Link Rate"
// method.
TEST(DpCapabilitiesTest, MaxLinkRate5400NoEdp) {
  inspect::Node node;
  FakeDpcdChannel fake_dpcd;
  fake_dpcd.SetDefaults();
  fake_dpcd.SetMaxLinkRate(dpcd::LinkBw::k5400Mbps);

  auto cap = i915::DpCapabilities::Read(&fake_dpcd, &node);
  ASSERT_TRUE(cap.is_ok());
  EXPECT_FALSE(cap.value().use_link_rate_table());
  ASSERT_EQ(3u, cap.value().supported_link_rates_mbps().size());
  EXPECT_EQ(1620, cap.value().supported_link_rates_mbps()[0]);
  EXPECT_EQ(2700, cap.value().supported_link_rates_mbps()[1]);
  EXPECT_EQ(5400, cap.value().supported_link_rates_mbps()[2]);
}

// Tests that the list of supported link rates is populated correctly using the "Max Link Rate"
// method.
TEST(DpCapabilitiesTest, MaxLinkRate8100NoEdp) {
  inspect::Node node;
  FakeDpcdChannel fake_dpcd;
  fake_dpcd.SetDefaults();
  fake_dpcd.SetMaxLinkRate(dpcd::LinkBw::k8100Mbps);

  auto cap = i915::DpCapabilities::Read(&fake_dpcd, &node);
  ASSERT_TRUE(cap.is_ok());
  EXPECT_FALSE(cap.value().use_link_rate_table());
  ASSERT_EQ(4u, cap.value().supported_link_rates_mbps().size());
  EXPECT_EQ(1620, cap.value().supported_link_rates_mbps()[0]);
  EXPECT_EQ(2700, cap.value().supported_link_rates_mbps()[1]);
  EXPECT_EQ(5400, cap.value().supported_link_rates_mbps()[2]);
  EXPECT_EQ(8100, cap.value().supported_link_rates_mbps()[3]);
}

// Tests that link rate discovery falls back to MAX_LINK_RATE if eDP v1.4 is supported but the
// link rate table is empty.
TEST(DpCapabilitiesTest, FallbackToMaxLinkRateWhenLinkRateTableIsEmpty) {
  inspect::Node node;
  FakeDpcdChannel fake_dpcd;
  fake_dpcd.SetDefaults();
  fake_dpcd.SetEdpCapable(dpcd::EdpRevision::k1_4);
  fake_dpcd.SetMaxLinkRate(dpcd::LinkBw::k1620Mbps);

  auto cap = i915::DpCapabilities::Read(&fake_dpcd, &node);
  ASSERT_TRUE(cap.is_ok());
  EXPECT_FALSE(cap.value().use_link_rate_table());
  EXPECT_FALSE(cap.value().supported_link_rates_mbps().empty());
}

// Tests that the list of supported link rates is populated correctly when using the "Link Rate
// Table" method.
TEST(DpCapabilitiesTest, LinkRateTableOneEntry) {
  inspect::Node node;
  FakeDpcdChannel fake_dpcd;
  fake_dpcd.SetDefaults();
  fake_dpcd.SetEdpCapable(dpcd::EdpRevision::k1_4);
  fake_dpcd.SetMaxLinkRate(0);  // Not supported

  fake_dpcd.PopulateLinkRateTable({100});  // 100 * 200kHz ==> 20MHz

  auto cap = i915::DpCapabilities::Read(&fake_dpcd, &node);
  ASSERT_TRUE(cap.is_ok());
  EXPECT_TRUE(cap.value().use_link_rate_table());
  EXPECT_EQ(1u, cap.value().supported_link_rates_mbps().size());
  EXPECT_EQ(20, cap.value().supported_link_rates_mbps()[0]);
}

// Tests that the list of supported link rates is populated correctly when using the "Link Rate
// Table" method.
TEST(DpCapabilitiesTest, LinkRateTableSomeEntries) {
  inspect::Node node;
  FakeDpcdChannel fake_dpcd;
  fake_dpcd.SetDefaults();
  fake_dpcd.SetEdpCapable(dpcd::EdpRevision::k1_4);
  fake_dpcd.SetMaxLinkRate(0);  // Not supported

  // 100 * 200kHz ==> 20MHz
  // 200 * 200kHz ==> 40MHz
  // 300 * 200kHz ==> 60MHz
  fake_dpcd.PopulateLinkRateTable({100, 200, 300});

  auto cap = i915::DpCapabilities::Read(&fake_dpcd, &node);
  ASSERT_TRUE(cap.is_ok());
  EXPECT_TRUE(cap.value().use_link_rate_table());
  EXPECT_EQ(3u, cap.value().supported_link_rates_mbps().size());
  EXPECT_EQ(std::vector<uint32_t>({20, 40, 60}), cap.value().supported_link_rates_mbps());
}

// Tests that the list of supported link rates is populated correctly when using the "Link Rate
// Table" method.
TEST(DpCapabilitiesTest, LinkRateTableMaxEntries) {
  inspect::Node node;
  FakeDpcdChannel fake_dpcd;
  fake_dpcd.SetDefaults();
  fake_dpcd.SetEdpCapable(dpcd::EdpRevision::k1_4);
  fake_dpcd.SetMaxLinkRate(0);  // Not supported

  // Link rate table entries are stored in units of 200kHz (or kbps). The DpCapabilities data
  // structure stores them in units of Mbps. 1 Mbps = 5 * 200kbps.
  constexpr uint16_t kConversionFactor = 5;
  std::vector<uint16_t> input;
  std::vector<uint32_t> output;
  for (unsigned i = 1; i <= kMaxLinkRateTableEntries; i++) {
    input.push_back(kConversionFactor * i);
    output.push_back(i);
  }
  fake_dpcd.PopulateLinkRateTable(std::move(input));

  auto cap = i915::DpCapabilities::Read(&fake_dpcd, &node);
  ASSERT_TRUE(cap.is_ok());
  EXPECT_TRUE(cap.value().use_link_rate_table());
  EXPECT_EQ(kMaxLinkRateTableEntries, cap.value().supported_link_rates_mbps().size());
  EXPECT_EQ(output, cap.value().supported_link_rates_mbps());
}

// Tests that the list of supported link rates is populated based on the "Link Rate Table" method
// when both the table and the MAX_LINK_RATE register hold valid values (which is optional but
// allowed by the eDP specification).
TEST(DpCapabilitiesTest, LinkRateTableUsedWhenMaxLinkRateIsAlsoPresent) {
  inspect::Node node;
  FakeDpcdChannel fake_dpcd;
  fake_dpcd.SetDefaults();
  fake_dpcd.SetEdpCapable(dpcd::EdpRevision::k1_4);
  fake_dpcd.SetMaxLinkRate(dpcd::LinkBw::k2700Mbps);

  // Link rate table entries are stored in units of 200kHz (or kbps). The DpCapabilities data
  // structure stores them in units of Mbps. 1 Mbps = 5 * 200kbps.
  constexpr uint16_t kConversionFactor = 5;
  constexpr uint32_t kExpectedLinkRate = 5400;
  fake_dpcd.PopulateLinkRateTable({kExpectedLinkRate * kConversionFactor});

  auto cap = i915::DpCapabilities::Read(&fake_dpcd, &node);
  ASSERT_TRUE(cap.is_ok());
  EXPECT_TRUE(cap.value().use_link_rate_table());
  EXPECT_EQ(1u, cap.value().supported_link_rates_mbps().size());
  EXPECT_EQ(kExpectedLinkRate, cap.value().supported_link_rates_mbps()[0]);
}

}  // namespace

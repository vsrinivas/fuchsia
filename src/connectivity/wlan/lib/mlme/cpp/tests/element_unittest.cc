// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>

#include <ddk/hw/wlan/ieee80211.h>
#include <gtest/gtest.h>
#include <wlan/common/buffer_writer.h>
#include <wlan/common/write_element.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/wlan.h>

namespace wlan {
namespace {

class Elements : public ::testing::Test {
 protected:
  Elements() { buf_offset_ = buf_; }

  template <typename T>
  void add_to_buf(const T& value) {
    memcpy(buf_offset_, &value, sizeof(value));
    buf_offset_ += sizeof(value);
  }
  uint8_t* buf_offset_;
  uint8_t buf_[1024] = {};
  size_t actual_ = 0;
};

TEST_F(Elements, Tspec) {
  // Values are chosen randomly.
  constexpr uint8_t ts_info[3] = {97, 54, 13};
  constexpr uint16_t nominal_msdu_size = 1068;
  constexpr uint16_t max_msdu_size = 17223;
  constexpr uint32_t min_svc_interval = 3463625064;
  constexpr uint32_t max_svc_interval = 1348743544;
  constexpr uint32_t inactivity_interval = 3254177988;
  constexpr uint32_t suspension_interval = 3114872601;
  constexpr uint32_t svc_start_time = 1977490251;
  constexpr uint32_t min_data_rate = 2288957164;
  constexpr uint32_t mean_data_rate = 3691476893;
  constexpr uint32_t peak_data_rate = 3115603983;
  constexpr uint32_t burst_size = 2196032537;
  constexpr uint32_t delay_bound = 4120916503;
  constexpr uint32_t min_phy_rate = 4071757759;
  constexpr uint32_t surplus_bw_allowance = 12936;
  constexpr uint32_t medium_time = 2196;

  add_to_buf(ts_info);
  add_to_buf<uint16_t>(nominal_msdu_size);
  add_to_buf<uint16_t>(max_msdu_size);
  add_to_buf<uint32_t>(min_svc_interval);
  add_to_buf<uint32_t>(max_svc_interval);
  add_to_buf<uint32_t>(inactivity_interval);
  add_to_buf<uint32_t>(suspension_interval);
  add_to_buf<uint32_t>(svc_start_time);
  add_to_buf<uint32_t>(min_data_rate);
  add_to_buf<uint32_t>(mean_data_rate);
  add_to_buf<uint32_t>(peak_data_rate);
  add_to_buf<uint32_t>(burst_size);
  add_to_buf<uint32_t>(delay_bound);
  add_to_buf<uint32_t>(min_phy_rate);
  add_to_buf<uint16_t>(surplus_bw_allowance);
  add_to_buf<uint16_t>(medium_time);

  auto element = FromBytes<Tspec>(buf_, sizeof(buf_));
  ASSERT_NE(nullptr, element);
  EXPECT_EQ(element->nominal_msdu_size.size(), nominal_msdu_size);
  EXPECT_EQ(element->nominal_msdu_size.fixed(), 0);
  EXPECT_EQ(element->max_msdu_size, max_msdu_size);
  EXPECT_EQ(element->min_service_interval, min_svc_interval);
  EXPECT_EQ(element->max_service_interval, max_svc_interval);
  EXPECT_EQ(element->inactivity_interval, inactivity_interval);
  EXPECT_EQ(element->suspension_interval, suspension_interval);
  EXPECT_EQ(element->service_start_time, svc_start_time);
  EXPECT_EQ(element->min_data_rate, min_data_rate);
  EXPECT_EQ(element->mean_data_rate, mean_data_rate);
  EXPECT_EQ(element->peak_data_rate, peak_data_rate);
  EXPECT_EQ(element->burst_size, burst_size);
  EXPECT_EQ(element->delay_bound, delay_bound);
  EXPECT_EQ(element->min_phy_rate, min_phy_rate);
  EXPECT_EQ(element->surplus_bw_allowance, surplus_bw_allowance);
  EXPECT_EQ(element->medium_time, medium_time);
}

TEST_F(Elements, TsInfoAggregation) {
  TsInfo ts_info;
  ts_info.p1.set_access_policy(TsAccessPolicy::kHccaSpca);
  EXPECT_TRUE(ts_info.IsValidAggregation());
  EXPECT_TRUE(ts_info.IsScheduleReserved());

  ts_info.p1.set_access_policy(TsAccessPolicy::kEdca);
  EXPECT_FALSE(ts_info.IsValidAggregation());
  EXPECT_FALSE(ts_info.IsScheduleReserved());

  ts_info.p2.set_schedule(1);
  EXPECT_TRUE(ts_info.IsValidAggregation());
}

TEST_F(Elements, TsInfoScheduleSetting) {
  TsInfo ts_info;
  EXPECT_EQ(ts_info.GetScheduleSetting(), TsScheduleSetting::kNoSchedule);

  ts_info.p1.set_apsd(1);
  EXPECT_EQ(ts_info.GetScheduleSetting(), TsScheduleSetting::kUnschedledApsd);

  ts_info.p1.set_apsd(0);
  ts_info.p2.set_schedule(1);
  EXPECT_EQ(ts_info.GetScheduleSetting(), TsScheduleSetting::kScheduledPsmp_GcrSp);

  ts_info.p1.set_apsd(1);
  EXPECT_EQ(ts_info.GetScheduleSetting(), TsScheduleSetting::kScheduledApsd);
}

TEST(HtCapabilities, DdkConversion) {
  ieee80211_ht_capabilities_t ddk{
      .ht_capability_info = 0x016e,
      .ampdu_params = 0x17,
      .supported_mcs_set.fields.rx_mcs_head = 0x00000001000000ff,
      .supported_mcs_set.fields.rx_mcs_tail = 0x01000000,
      .supported_mcs_set.fields.tx_mcs = 0x00000000,
      .ht_ext_capabilities = 0x1234,
      .tx_beamforming_capabilities = 0x12345678,
      .asel_capabilities = 0xff,
  };

  auto ieee = HtCapabilities::FromDdk(ddk);
  EXPECT_EQ(0x016eU, ieee.ht_cap_info.val());
  EXPECT_EQ(0x17U, ieee.ampdu_params.val());
  EXPECT_EQ(0x00000001000000ffU, ieee.mcs_set.rx_mcs_head.val());
  EXPECT_EQ(0x01000000U, ieee.mcs_set.rx_mcs_tail.val());
  EXPECT_EQ(0x00000000U, ieee.mcs_set.tx_mcs.val());
  EXPECT_EQ(0x1234U, ieee.ht_ext_cap.val());
  EXPECT_EQ(0x12345678U, ieee.txbf_cap.val());
  EXPECT_EQ(0xffU, ieee.asel_cap.val());

  auto ddk2 = ieee.ToDdk();
  EXPECT_EQ(ddk.ht_capability_info, ddk2.ht_capability_info);
  EXPECT_EQ(ddk.ampdu_params, ddk2.ampdu_params);
  EXPECT_EQ(ddk.supported_mcs_set.fields.rx_mcs_head, ddk2.supported_mcs_set.fields.rx_mcs_head);
  EXPECT_EQ(ddk.supported_mcs_set.fields.rx_mcs_tail, ddk2.supported_mcs_set.fields.rx_mcs_tail);
  EXPECT_EQ(ddk.supported_mcs_set.fields.tx_mcs, ddk2.supported_mcs_set.fields.tx_mcs);
  EXPECT_EQ(ddk.ht_ext_capabilities, ddk2.ht_ext_capabilities);
  EXPECT_EQ(ddk.tx_beamforming_capabilities, ddk2.tx_beamforming_capabilities);
  EXPECT_EQ(ddk.asel_capabilities, ddk2.asel_capabilities);
}

TEST(HtOperation, DdkConversion) {
  wlan_ht_op ddk{
      .primary_chan = 123,
      .head = 0x01020304,
      .tail = 0x05,
      .rx_mcs_head = 0x00000001000000ff,
      .rx_mcs_tail = 0x01000000,
      .tx_mcs = 0x00000000,
  };

  auto ieee = HtOperation::FromDdk(ddk);
  EXPECT_EQ(123U, ieee.primary_chan);
  EXPECT_EQ(0x01020304U, ieee.head.val());
  EXPECT_EQ(0x05U, ieee.tail.val());
  EXPECT_EQ(0x00000001000000ffU, ieee.basic_mcs_set.rx_mcs_head.val());
  EXPECT_EQ(0x01000000U, ieee.basic_mcs_set.rx_mcs_tail.val());
  EXPECT_EQ(0x00000000U, ieee.basic_mcs_set.tx_mcs.val());

  auto ddk2 = ieee.ToDdk();
  EXPECT_EQ(ddk.primary_chan, ddk2.primary_chan);
  EXPECT_EQ(ddk.head, ddk2.head);
  EXPECT_EQ(ddk.tail, ddk2.tail);
  EXPECT_EQ(ddk.rx_mcs_head, ddk2.rx_mcs_head);
  EXPECT_EQ(ddk.rx_mcs_tail, ddk2.rx_mcs_tail);
  EXPECT_EQ(ddk.tx_mcs, ddk2.tx_mcs);
}

TEST(VhtCapabilities, DdkConversion) {
  ieee80211_vht_capabilities_t ddk{
      .vht_capability_info = 0xaabbccdd,
      .supported_vht_mcs_and_nss_set = 0x0011223344556677,
  };

  auto ieee = VhtCapabilities::FromDdk(ddk);
  EXPECT_EQ(0xaabbccddU, ieee.vht_cap_info.val());
  EXPECT_EQ(0x0011223344556677U, ieee.vht_mcs_nss.val());

  auto ddk2 = ieee.ToDdk();
  EXPECT_EQ(ddk.vht_capability_info, ddk2.vht_capability_info);
  EXPECT_EQ(ddk.supported_vht_mcs_and_nss_set, ddk2.supported_vht_mcs_and_nss_set);
}

TEST(VhtOperation, DdkConversion) {
  wlan_vht_op ddk{
      .vht_cbw = 0x01,
      .center_freq_seg0 = 42,
      .center_freq_seg1 = 106,
      .basic_mcs = 0x1122,
  };

  auto ieee = VhtOperation::FromDdk(ddk);
  EXPECT_EQ(0x01U, ieee.vht_cbw);
  EXPECT_EQ(42U, ieee.center_freq_seg0);
  EXPECT_EQ(106U, ieee.center_freq_seg1);
  EXPECT_EQ(0x1122U, ieee.basic_mcs.val());

  auto ddk2 = ieee.ToDdk();
  EXPECT_EQ(ddk.vht_cbw, ddk2.vht_cbw);
  EXPECT_EQ(ddk.center_freq_seg0, ddk2.center_freq_seg0);
  EXPECT_EQ(ddk.center_freq_seg1, ddk2.center_freq_seg1);
  EXPECT_EQ(ddk.basic_mcs, ddk2.basic_mcs);
}

TEST(SupportedRate, Create) {
  SupportedRate rate = {};
  ASSERT_EQ(rate.rate(), 0);
  ASSERT_EQ(rate.is_basic(), 0);

  // Create a rate with basic bit set.
  rate = SupportedRate(0xF9);
  ASSERT_EQ(rate.rate(), 0x79);
  ASSERT_EQ(rate.is_basic(), 1);

  // Create a rate with basic bit set but explicitly override basic setting.
  rate = SupportedRate(0xF9, false);
  ASSERT_EQ(rate.rate(), 0x79);
  ASSERT_EQ(rate.is_basic(), 0);

  // Create a rate with explicitly setting basic bit.
  rate = SupportedRate::basic(0x79);
  ASSERT_EQ(rate.rate(), 0x79);
  ASSERT_EQ(rate.is_basic(), 1);
}

TEST(SupportedRate, ToUint8) {
  SupportedRate rate = {};
  ASSERT_EQ(static_cast<uint8_t>(rate), 0);

  rate = SupportedRate(0xF9);
  ASSERT_EQ(static_cast<uint8_t>(rate), 0xF9);

  rate = SupportedRate::basic(0x79);
  ASSERT_EQ(static_cast<uint8_t>(rate), 0xF9);
}

TEST(SupportedRate, Compare) {
  // Ignore basic bit when comparing rates.
  SupportedRate rate1(0x79);
  SupportedRate rate2(0xF9);
  ASSERT_TRUE(rate1 == rate2);
  ASSERT_FALSE(rate1 != rate2);
  ASSERT_FALSE(rate1 < rate2);
  ASSERT_FALSE(rate1 > rate2);

  // Test smaller.
  rate1 = SupportedRate(0x78);
  rate2 = SupportedRate(0xF9);
  ASSERT_FALSE(rate1 == rate2);
  ASSERT_TRUE(rate1 != rate2);
  ASSERT_TRUE(rate1 < rate2);
  ASSERT_FALSE(rate1 > rate2);

  // Test larger.
  rate1 = SupportedRate(0x7A);
  rate2 = SupportedRate(0xF9);
  ASSERT_FALSE(rate1 == rate2);
  ASSERT_TRUE(rate1 != rate2);
  ASSERT_FALSE(rate1 < rate2);
  ASSERT_TRUE(rate1 > rate2);
}

struct RateVector {
  std::vector<SupportedRate> ap;
  std::vector<SupportedRate> client;
  std::vector<SupportedRate> want;
};

TEST(Intersector, IntersectRates) {
  // Rates are in 0.5Mbps increment: 12 -> 6 Mbps, 11 -> 5.5 Mbps, etc.
  std::vector<RateVector> list = {
      {{}, {}, {}},
      {{SupportedRate(12)}, {SupportedRate(12)}, {SupportedRate(12)}},
      {{SupportedRate::basic(12)}, {SupportedRate(12)}, {SupportedRate::basic(12)}},
      {{SupportedRate(12)}, {SupportedRate::basic(12)}, {SupportedRate(12)}},
      {{SupportedRate::basic(12)}, {}, {}},
      {{}, {SupportedRate::basic(12)}, {}},
      {{SupportedRate(12)}, {}, {}},
      {{}, {SupportedRate(12)}, {}},
      {{SupportedRate::basic(12), SupportedRate(24)},
       {SupportedRate::basic(24), SupportedRate(12)},
       {SupportedRate::basic(12), SupportedRate(24)}},
      {{SupportedRate(24), SupportedRate::basic(12)},
       {SupportedRate(12), SupportedRate::basic(24)},
       {SupportedRate::basic(12), SupportedRate(24)}},
      {{SupportedRate(72), SupportedRate::basic(108), SupportedRate::basic(96)},
       {SupportedRate(96)},
       {SupportedRate::basic(96)}},
      {{SupportedRate(72), SupportedRate::basic(108), SupportedRate::basic(96)},
       {SupportedRate::basic(72)},
       {SupportedRate(72)}},
  };

  for (auto vec : list) {
    auto got = IntersectRatesAp(vec.ap, vec.client);
    EXPECT_EQ(vec.want, got);
    for (size_t i = 0; i < got.size(); ++i) {
      EXPECT_EQ(vec.want[i].val(), got[i].val());
    }
  }
}

}  // namespace
}  // namespace wlan

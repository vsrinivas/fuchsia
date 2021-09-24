// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/wlan/info/c/banjo.h>
#include <fuchsia/hardware/wlanif/c/banjo.h>
#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/common/cpp/fidl.h>
#include <fuchsia/wlan/ieee80211/cpp/fidl.h>
#include <fuchsia/wlan/internal/c/banjo.h>
#include <fuchsia/wlan/internal/cpp/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <fuchsia/wlan/stats/cpp/fidl.h>

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <src/connectivity/wlan/drivers/wlanif/convert.h>
#include <wlan/common/element.h>

namespace wlanif {
namespace {
namespace wlan_ieee80211 = ::fuchsia::wlan::ieee80211;
namespace wlan_internal = ::fuchsia::wlan::internal;
namespace wlan_mlme = ::fuchsia::wlan::mlme;
namespace wlan_stats = ::fuchsia::wlan::stats;

using ::testing::IsEmpty;
using ::testing::IsNull;
using ::testing::Matcher;
using ::testing::NotNull;
using ::testing::UnorderedElementsAreArray;

template <typename T>
zx_status_t ValidateMessage(T* msg) {
  fidl::Encoder enc(0);
  enc.Alloc(fidl::EncodingInlineSize<T>(&enc));
  msg->Encode(&enc, sizeof(fidl_message_header_t));

  auto encoded = enc.GetMessage();
  auto msg_body = encoded.payload();
  const char* err_msg = nullptr;
  return fidl_validate(T::FidlType, msg_body.data(), msg_body.size(), 0, &err_msg);
}

TEST(ConvertTest, ToFidlBssDescription) {
  uint8_t ies[] = {0, 4, 0x73, 0x73, 0x69, 0x64};
  bss_description_t banjo_desc{.bssid = {1, 2, 3, 4, 5, 6},
                               .bss_type = BSS_TYPE_INFRASTRUCTURE,
                               .beacon_period = 2,
                               .capability_info = 1337,
                               .ies_list = ies,
                               .ies_count = sizeof(ies),

                               .channel{
                                   .primary = 32,
                                   .cbw = CHANNEL_BANDWIDTH_CBW40,
                                   .secondary80 = 0,
                               },
                               .rssi_dbm = -40,
                               .snr_db = 20};

  wlan_internal::BssDescription fidl_desc = {};
  ConvertBssDescription(&fidl_desc, banjo_desc);

  auto status = ValidateMessage(&fidl_desc);
  EXPECT_EQ(status, ZX_OK);
  auto expected_bssid = std::array<uint8_t, 6>{1, 2, 3, 4, 5, 6};
  EXPECT_EQ(fidl_desc.bssid, expected_bssid);
  EXPECT_EQ(fidl_desc.bss_type, fuchsia::wlan::internal::BssType::INFRASTRUCTURE);
  EXPECT_EQ(fidl_desc.beacon_period, 2u);
  EXPECT_EQ(fidl_desc.capability_info, 1337);
  auto expected_ies = std::vector<uint8_t>(ies, ies + sizeof(ies));
  EXPECT_EQ(fidl_desc.ies, expected_ies);
  EXPECT_EQ(fidl_desc.channel.primary, 32);
  EXPECT_EQ(fidl_desc.channel.cbw, fuchsia::wlan::common::ChannelBandwidth::CBW40);
  EXPECT_EQ(fidl_desc.channel.secondary80, 0);
  EXPECT_EQ(fidl_desc.rssi_dbm, -40);
  EXPECT_EQ(fidl_desc.snr_db, 20);
}

TEST(ConvertTest, ToFidlAssocInd) {
  wlan_mlme::AssociateIndication fidl_ind = {};
  wlanif_assoc_ind_t assoc_ind = {
      .rsne_len = 64,
  };
  // Check if rsne gets copied over
  ConvertAssocInd(&fidl_ind, assoc_ind);
  ASSERT_TRUE(fidl_ind.rsne.has_value());
  ASSERT_TRUE(fidl_ind.rsne->size() == 64);
  auto status = ValidateMessage(&fidl_ind);
  EXPECT_EQ(status, ZX_OK);

  // Check to see rsne is not copied in this case and also ensure
  // the FIDL message gets reset
  assoc_ind.rsne_len = 0;
  ConvertAssocInd(&fidl_ind, assoc_ind);
  ASSERT_FALSE(fidl_ind.rsne.has_value());
  status = ValidateMessage(&fidl_ind);
  EXPECT_EQ(status, ZX_OK);
}

TEST(ConvertTest, ToFidlEapolConf) {
  wlan_mlme::EapolConfirm fidl_resp = {};
  wlanif_eapol_confirm_t eapol_resp = {
      .result_code = WLAN_EAPOL_RESULT_SUCCESS,
      .dst_addr = {1, 2, 3, 4, 5, 6},
  };
  ConvertEapolConf(&fidl_resp, eapol_resp);
  auto expected_dst_addr = std::array<uint8_t, 6>{1, 2, 3, 4, 5, 6};
  EXPECT_EQ(fidl_resp.dst_addr, expected_dst_addr);
  EXPECT_EQ(fidl_resp.result_code, wlan_mlme::EapolResultCode::SUCCESS);
}

// Fancier parameterized tests use PER_ANTENNA scope, so let's do quick smoke tests with STATION
// scope.
TEST(ConvertTest, ToFidlNoiseFloorHistogramSmokeTest) {
  wlan_stats::NoiseFloorHistogram fidl_hist;
  const wlanif_noise_floor_histogram_t hist_input = {
      .hist_scope = WLANIF_HIST_SCOPE_STATION,
  };

  ConvertNoiseFloorHistogram(&fidl_hist, hist_input);
  const auto status = ValidateMessage(&fidl_hist);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(fidl_hist.hist_scope, wlan_stats::HistScope::STATION);
  EXPECT_THAT(fidl_hist.antenna_id, IsNull());
  EXPECT_THAT(fidl_hist.noise_floor_samples, IsEmpty());
  EXPECT_EQ(fidl_hist.invalid_samples, 0U);
}

TEST(ConvertTest, ToFidlRssiHistogramSmokeTest) {
  wlan_stats::RssiHistogram fidl_hist;
  const wlanif_rssi_histogram_t hist_input = {
      .hist_scope = WLANIF_HIST_SCOPE_STATION,
  };

  ConvertRssiHistogram(&fidl_hist, hist_input);
  const auto status = ValidateMessage(&fidl_hist);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(fidl_hist.hist_scope, wlan_stats::HistScope::STATION);
  EXPECT_THAT(fidl_hist.antenna_id, IsNull());
  EXPECT_THAT(fidl_hist.rssi_samples, IsEmpty());
  EXPECT_EQ(fidl_hist.invalid_samples, 0U);
}

TEST(ConvertTest, ToFidlRxRateIndexHistogramSmokeTest) {
  wlan_stats::RxRateIndexHistogram fidl_hist;
  const wlanif_rx_rate_index_histogram_t hist_input = {
      .hist_scope = WLANIF_HIST_SCOPE_STATION,
  };

  ConvertRxRateIndexHistogram(&fidl_hist, hist_input);
  const auto status = ValidateMessage(&fidl_hist);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(fidl_hist.hist_scope, wlan_stats::HistScope::STATION);
  EXPECT_THAT(fidl_hist.antenna_id, IsNull());
  EXPECT_THAT(fidl_hist.rx_rate_index_samples, IsEmpty());
  EXPECT_EQ(fidl_hist.invalid_samples, 0U);
}

TEST(ConvertTest, ToFidlSnrHistogramSmokeTest) {
  wlan_stats::SnrHistogram fidl_hist;
  const wlanif_snr_histogram_t hist_input = {
      .hist_scope = WLANIF_HIST_SCOPE_STATION,
  };

  ConvertSnrHistogram(&fidl_hist, hist_input);
  const auto status = ValidateMessage(&fidl_hist);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(fidl_hist.hist_scope, wlan_stats::HistScope::STATION);
  EXPECT_THAT(fidl_hist.antenna_id, IsNull());
  EXPECT_THAT(fidl_hist.snr_samples, IsEmpty());
  EXPECT_EQ(fidl_hist.invalid_samples, 0U);
}

// Custom Gmock matcher for comparing FIDL HistBucket structs for equality.
MATCHER_P(HistBucketEq, bucket, "") {
  if (arg.bucket_index == bucket.bucket_index && arg.num_samples == bucket.num_samples) {
    return true;
  }
  return false;
}

// Histogram conversion tests that are parameterized, allowing tests with varying size sample
// inputs.
class ConvertNoiseFloorHistogramTest
    : public testing::TestWithParam<std::vector<wlan_stats::HistBucket>> {};

TEST_P(ConvertNoiseFloorHistogramTest, ToFidlHistogram) {
  wlan_stats::NoiseFloorHistogram fidl_hist;

  const auto expected_hist_scope = wlan_stats::HistScope::PER_ANTENNA;
  const auto expected_antenna_freq = wlan_stats::AntennaFreq::ANTENNA_5_G;
  const uint8_t expected_antenna_index = 0;
  const uint64_t expected_invalid_samples = 15;

  // This will hold the Banjo buckets that will be input into the conversion.
  std::vector<wlanif_hist_bucket_t> samples_input;

  // To compare the FIDL buckets to the Banjo buckets, we will need a vector of Gmock matchers, one
  // for each expected FIDL bucket.
  std::vector<Matcher<const wlan_stats::HistBucket&>> expected_samples_matchers;
  const auto& expected_samples = GetParam();
  for (const auto& expected_sample : expected_samples) {
    // Add each expected bucket to the Banjo samples input.
    samples_input.push_back(
        {.bucket_index = expected_sample.bucket_index, .num_samples = expected_sample.num_samples});
    // And add a matcher for each expected FIDL bucket. We expect only non-empty buckets.
    if (expected_sample.num_samples > 0) {
      expected_samples_matchers.push_back(HistBucketEq(expected_sample));
    }
  }

  const wlanif_noise_floor_histogram_t hist_input = {
      .hist_scope = WLANIF_HIST_SCOPE_PER_ANTENNA,
      .antenna_id =
          {
              .freq = WLANIF_ANTENNA_FREQ_ANTENNA_5_G,
              .index = expected_antenna_index,
          },
      .noise_floor_samples_list = samples_input.data(),
      .noise_floor_samples_count = samples_input.size(),
      .invalid_samples = expected_invalid_samples,
  };

  ConvertNoiseFloorHistogram(&fidl_hist, hist_input);
  const auto status = ValidateMessage(&fidl_hist);
  EXPECT_EQ(status, ZX_OK);
  ASSERT_THAT(fidl_hist.antenna_id, NotNull());
  EXPECT_EQ(fidl_hist.antenna_id->freq, expected_antenna_freq);
  EXPECT_EQ(fidl_hist.antenna_id->index, expected_antenna_index);
  EXPECT_EQ(fidl_hist.hist_scope, expected_hist_scope);
  EXPECT_THAT(fidl_hist.noise_floor_samples, UnorderedElementsAreArray(expected_samples_matchers));
  EXPECT_EQ(fidl_hist.invalid_samples, expected_invalid_samples);
}

INSTANTIATE_TEST_SUITE_P(
    NoiseFloorHistogram, ConvertNoiseFloorHistogramTest,
    testing::Values(std::vector<wlan_stats::HistBucket>{},
                    std::vector<wlan_stats::HistBucket>{
                        {.bucket_index = 60, .num_samples = 0},
                    },
                    std::vector<wlan_stats::HistBucket>{
                        {.bucket_index = 80, .num_samples = 10},
                    },
                    std::vector<wlan_stats::HistBucket>{
                        {.bucket_index = 80, .num_samples = 10},
                        {.bucket_index = 66, .num_samples = 0},
                    },
                    std::vector<wlan_stats::HistBucket>{
                        {.bucket_index = 100, .num_samples = 1450},
                        {.bucket_index = 102, .num_samples = 20},
                        // Let's throw in one big number, just within type range.
                        {.bucket_index = 107, .num_samples = 18446744073709551615U},
                    }));

class ConvertRssiHistogramTest
    : public testing::TestWithParam<std::vector<wlan_stats::HistBucket>> {};

TEST_P(ConvertRssiHistogramTest, ToFidlHistogram) {
  wlan_stats::RssiHistogram fidl_hist;

  const auto expected_hist_scope = wlan_stats::HistScope::PER_ANTENNA;
  const auto expected_antenna_freq = wlan_stats::AntennaFreq::ANTENNA_2_G;
  const uint8_t expected_antenna_index = 0;
  const uint64_t expected_invalid_samples = 1;

  // This will hold the Banjo buckets that will be input into the conversion.
  std::vector<wlanif_hist_bucket_t> samples_input;

  // To compare the FIDL buckets to the Banjo buckets, we will need a vector of Gmock matchers, one
  // for each expected FIDL bucket.
  std::vector<Matcher<const wlan_stats::HistBucket&>> expected_samples_matchers;
  const auto& expected_samples = GetParam();
  for (const auto& expected_sample : expected_samples) {
    // Add each expected bucket to the Banjo samples input.
    samples_input.push_back(
        {.bucket_index = expected_sample.bucket_index, .num_samples = expected_sample.num_samples});
    // And add a matcher for each expected FIDL bucket. We expect only non-empty buckets.
    if (expected_sample.num_samples > 0) {
      expected_samples_matchers.push_back(HistBucketEq(expected_sample));
    }
  }

  const wlanif_rssi_histogram_t hist_input = {
      .hist_scope = WLANIF_HIST_SCOPE_PER_ANTENNA,
      .antenna_id =
          {
              .freq = WLANIF_ANTENNA_FREQ_ANTENNA_2_G,
              .index = expected_antenna_index,
          },
      .rssi_samples_list = samples_input.data(),
      .rssi_samples_count = samples_input.size(),
      .invalid_samples = expected_invalid_samples,
  };

  ConvertRssiHistogram(&fidl_hist, hist_input);
  const auto status = ValidateMessage(&fidl_hist);
  EXPECT_EQ(status, ZX_OK);
  ASSERT_THAT(fidl_hist.antenna_id, NotNull());
  EXPECT_EQ(fidl_hist.antenna_id->freq, expected_antenna_freq);
  EXPECT_EQ(fidl_hist.antenna_id->index, expected_antenna_index);
  EXPECT_EQ(fidl_hist.hist_scope, expected_hist_scope);
  EXPECT_THAT(fidl_hist.rssi_samples, UnorderedElementsAreArray(expected_samples_matchers));
  EXPECT_EQ(fidl_hist.invalid_samples, expected_invalid_samples);
}

INSTANTIATE_TEST_SUITE_P(RssiHistogram, ConvertRssiHistogramTest,
                         testing::Values(std::vector<wlan_stats::HistBucket>{},
                                         std::vector<wlan_stats::HistBucket>{
                                             {.bucket_index = 9, .num_samples = 0},
                                         },
                                         std::vector<wlan_stats::HistBucket>{
                                             {.bucket_index = 99, .num_samples = 1000},
                                         },
                                         std::vector<wlan_stats::HistBucket>{
                                             {.bucket_index = 84, .num_samples = 2},
                                             {.bucket_index = 89, .num_samples = 0},
                                         },
                                         std::vector<wlan_stats::HistBucket>{
                                             {.bucket_index = 200, .num_samples = 1765},
                                             {.bucket_index = 2,
                                              .num_samples = 18446744073709551615U},
                                             {.bucket_index = 50, .num_samples = 1},
                                         }));

class ConvertRxRateIndexHistogramTest
    : public testing::TestWithParam<std::vector<wlan_stats::HistBucket>> {};

TEST_P(ConvertRxRateIndexHistogramTest, ToFidlHistogram) {
  wlan_stats::RxRateIndexHistogram fidl_hist;

  const auto expected_hist_scope = wlan_stats::HistScope::PER_ANTENNA;
  const auto expected_antenna_freq = wlan_stats::AntennaFreq::ANTENNA_2_G;
  const uint8_t expected_antenna_index = 1;
  const uint64_t expected_invalid_samples = 2;

  // This will hold the Banjo buckets that will be input into the conversion.
  std::vector<wlanif_hist_bucket_t> samples_input;

  // To compare the FIDL buckets to the Banjo buckets, we will need a vector of Gmock matchers, one
  // for each expected FIDL bucket.
  std::vector<Matcher<const wlan_stats::HistBucket&>> expected_samples_matchers;
  const auto& expected_samples = GetParam();
  for (const auto& expected_sample : expected_samples) {
    // Add each expected bucket to the Banjo samples input.
    samples_input.push_back(
        {.bucket_index = expected_sample.bucket_index, .num_samples = expected_sample.num_samples});
    // And add a matcher for each expected FIDL bucket. We expect only non-empty buckets.
    if (expected_sample.num_samples > 0) {
      expected_samples_matchers.push_back(HistBucketEq(expected_sample));
    }
  }

  const wlanif_rx_rate_index_histogram_t hist_input = {
      .hist_scope = WLANIF_HIST_SCOPE_PER_ANTENNA,
      .antenna_id =
          {
              .freq = WLANIF_ANTENNA_FREQ_ANTENNA_2_G,
              .index = expected_antenna_index,
          },
      .rx_rate_index_samples_list = samples_input.data(),
      .rx_rate_index_samples_count = samples_input.size(),
      .invalid_samples = expected_invalid_samples,
  };

  ConvertRxRateIndexHistogram(&fidl_hist, hist_input);
  const auto status = ValidateMessage(&fidl_hist);
  EXPECT_EQ(status, ZX_OK);
  ASSERT_THAT(fidl_hist.antenna_id, NotNull());
  EXPECT_EQ(fidl_hist.antenna_id->freq, expected_antenna_freq);
  EXPECT_EQ(fidl_hist.antenna_id->index, expected_antenna_index);
  EXPECT_EQ(fidl_hist.hist_scope, expected_hist_scope);
  EXPECT_THAT(fidl_hist.rx_rate_index_samples,
              UnorderedElementsAreArray(expected_samples_matchers));
  EXPECT_EQ(fidl_hist.invalid_samples, expected_invalid_samples);
}

INSTANTIATE_TEST_SUITE_P(RxRateIndexHistogram, ConvertRxRateIndexHistogramTest,
                         testing::Values(std::vector<wlan_stats::HistBucket>{},
                                         std::vector<wlan_stats::HistBucket>{
                                             {.bucket_index = 103, .num_samples = 0},
                                         },
                                         std::vector<wlan_stats::HistBucket>{
                                             {.bucket_index = 42, .num_samples = 8},
                                         },
                                         std::vector<wlan_stats::HistBucket>{
                                             {.bucket_index = 121, .num_samples = 0},
                                             {.bucket_index = 120, .num_samples = 66},
                                         },
                                         std::vector<wlan_stats::HistBucket>{
                                             {.bucket_index = 122,
                                              .num_samples = 18446744073709551615U},
                                             {.bucket_index = 1, .num_samples = 1},
                                             {.bucket_index = 11, .num_samples = 54356},
                                         }));

class ConvertSnrHistogramTest : public testing::TestWithParam<std::vector<wlan_stats::HistBucket>> {
};

TEST_P(ConvertSnrHistogramTest, ToFidlHistogram) {
  wlan_stats::SnrHistogram fidl_hist;

  const auto expected_hist_scope = wlan_stats::HistScope::PER_ANTENNA;
  const auto expected_antenna_freq = wlan_stats::AntennaFreq::ANTENNA_2_G;
  const uint8_t expected_antenna_index = 2;
  const uint64_t expected_invalid_samples = 2890967;

  // This will hold the Banjo buckets that will be input into the conversion.
  std::vector<wlanif_hist_bucket_t> samples_input;

  // To compare the FIDL buckets to the Banjo buckets, we will need a vector of Gmock matchers, one
  // for each expected FIDL bucket.
  std::vector<Matcher<const wlan_stats::HistBucket&>> expected_samples_matchers;
  const auto& expected_samples = GetParam();
  for (const auto& expected_sample : expected_samples) {
    // Add each expected bucket to the Banjo samples input.
    samples_input.push_back(
        {.bucket_index = expected_sample.bucket_index, .num_samples = expected_sample.num_samples});
    // And add a matcher for each expected FIDL bucket. We expect only non-empty buckets.
    if (expected_sample.num_samples > 0) {
      expected_samples_matchers.push_back(HistBucketEq(expected_sample));
    }
  }

  const wlanif_snr_histogram_t hist_input = {
      .hist_scope = WLANIF_HIST_SCOPE_PER_ANTENNA,
      .antenna_id =
          {
              .freq = WLANIF_ANTENNA_FREQ_ANTENNA_2_G,
              .index = expected_antenna_index,
          },
      .snr_samples_list = samples_input.data(),
      .snr_samples_count = samples_input.size(),
      .invalid_samples = expected_invalid_samples,
  };

  ConvertSnrHistogram(&fidl_hist, hist_input);
  const auto status = ValidateMessage(&fidl_hist);
  EXPECT_EQ(status, ZX_OK);
  ASSERT_THAT(fidl_hist.antenna_id, NotNull());
  EXPECT_EQ(fidl_hist.antenna_id->freq, expected_antenna_freq);
  EXPECT_EQ(fidl_hist.antenna_id->index, expected_antenna_index);
  EXPECT_EQ(fidl_hist.hist_scope, expected_hist_scope);
  EXPECT_THAT(fidl_hist.snr_samples, UnorderedElementsAreArray(expected_samples_matchers));
  EXPECT_EQ(fidl_hist.invalid_samples, expected_invalid_samples);
}

INSTANTIATE_TEST_SUITE_P(SnrHistogram, ConvertSnrHistogramTest,
                         testing::Values(std::vector<wlan_stats::HistBucket>{},
                                         std::vector<wlan_stats::HistBucket>{
                                             {.bucket_index = 76, .num_samples = 0},
                                         },
                                         std::vector<wlan_stats::HistBucket>{
                                             {.bucket_index = 77, .num_samples = 23},
                                         },
                                         std::vector<wlan_stats::HistBucket>{
                                             {.bucket_index = 78, .num_samples = 0},
                                             {.bucket_index = 8, .num_samples = 231450},
                                         },
                                         std::vector<wlan_stats::HistBucket>{
                                             {.bucket_index = 178,
                                              .num_samples = 18446744073709551615U},
                                             {.bucket_index = 237, .num_samples = 46723},
                                             {.bucket_index = 0, .num_samples = 13245},
                                         }));

TEST(ConvertTest, ToFidlPmkInfo) {
  std::vector<uint8_t> pmk = {1, 2, 3, 4, 5, 6, 7, 8};
  std::vector<uint8_t> pmkid = {1, 1, 2, 2, 3, 3, 4, 4};
  wlanif_pmk_info_t info{
      .pmk_list = pmk.data(),
      .pmk_count = pmk.size(),
      .pmkid_list = pmkid.data(),
      .pmkid_count = pmkid.size(),
  };
  wlan_mlme::PmkInfo fidl_info;
  ConvertPmkInfo(&fidl_info, info);
  EXPECT_EQ(fidl_info.pmk, pmk);
  EXPECT_EQ(fidl_info.pmkid, pmkid);
}

TEST(ConvertTest, ToWlanifBssDescription) {
  uint8_t ies[] = {0, 4, 0x73, 0x73, 0x69, 0x64};
  wlan_internal::BssDescription fidl_desc{
      .bssid = std::array<uint8_t, 6>{1, 2, 3, 4, 5, 6},
      .bss_type = fuchsia::wlan::internal::BssType::INFRASTRUCTURE,
      .beacon_period = 2,
      .capability_info = 1337,
      .ies = std::vector<uint8_t>(ies, ies + sizeof(ies)),

      .channel{
          .primary = 32, .cbw = fuchsia::wlan::common::ChannelBandwidth::CBW40, .secondary80 = 0},
      .rssi_dbm = -40,
      .snr_db = 20};

  bss_description_t banjo_desc = {};
  ConvertBssDescription(&banjo_desc, fidl_desc);

  uint8_t expected_bssid[] = {1, 2, 3, 4, 5, 6};
  EXPECT_EQ(memcmp(banjo_desc.bssid, expected_bssid, sizeof(banjo_desc.bssid)), 0);
  EXPECT_EQ(banjo_desc.bss_type, BSS_TYPE_INFRASTRUCTURE);
  EXPECT_EQ(banjo_desc.beacon_period, 2u);
  EXPECT_EQ(banjo_desc.capability_info, 1337);
  ASSERT_EQ(banjo_desc.ies_count, sizeof(ies));
  EXPECT_EQ(memcmp(banjo_desc.ies_list, ies, sizeof(ies)), 0);
  EXPECT_EQ(banjo_desc.channel.primary, 32);
  EXPECT_EQ(banjo_desc.channel.cbw, CHANNEL_BANDWIDTH_CBW40);
  EXPECT_EQ(banjo_desc.channel.secondary80, 0);
  EXPECT_EQ(banjo_desc.rssi_dbm, -40);
  EXPECT_EQ(banjo_desc.snr_db, 20);
}

TEST(ConvertTest, ToWlanifOrFidlSaeAuthFrame) {
  std::array<uint8_t, 6> peer_sta_address = {1, 1, 2, 2, 3, 4};
  std::vector<unsigned char> sae_fields = {9, 8, 7, 6, 5, 5, 4, 3, 2, 2, 1};
  wlan_ieee80211::StatusCode fidl_status_code = wlan_ieee80211::StatusCode::SUCCESS;
  uint16_t status_code = static_cast<uint16_t>(fidl_status_code);

  wlan_mlme::SaeFrame fidl_frame = {
      .peer_sta_address = peer_sta_address,
      .status_code = fidl_status_code,
      .seq_num = 1,
      .sae_fields = sae_fields,
  };

  wlanif_sae_frame_t frame = {};

  ConvertSaeAuthFrame(fidl_frame, &frame);

  EXPECT_EQ(memcmp(frame.peer_sta_address, fidl_frame.peer_sta_address.data(), ETH_ALEN), 0);
  EXPECT_EQ(frame.status_code, status_code);
  EXPECT_EQ(frame.seq_num, 1);
  EXPECT_EQ(frame.sae_fields_count, fidl_frame.sae_fields.size());
  EXPECT_EQ(memcmp(frame.sae_fields_list, fidl_frame.sae_fields.data(), frame.sae_fields_count), 0);

  ConvertSaeAuthFrame(&frame, fidl_frame);

  EXPECT_EQ(memcmp(frame.peer_sta_address, fidl_frame.peer_sta_address.data(), ETH_ALEN), 0);
  EXPECT_EQ(fidl_frame.status_code, fidl_status_code);
  EXPECT_EQ(frame.seq_num, 1);
  EXPECT_EQ(frame.sae_fields_count, fidl_frame.sae_fields.size());
  EXPECT_EQ(memcmp(frame.sae_fields_list, fidl_frame.sae_fields.data(), frame.sae_fields_count), 0);
}

TEST(ConvertTest, ToFidlWmmStatus) {
  wlan_wmm_params_t params;
  params.apsd = true;

  params.ac_be_params.aifsn = 1;
  params.ac_be_params.ecw_min = 2;
  params.ac_be_params.ecw_max = 3;
  params.ac_be_params.txop_limit = 4;
  params.ac_be_params.acm = false;

  params.ac_bk_params.aifsn = 5;
  params.ac_bk_params.ecw_min = 6;
  params.ac_bk_params.ecw_max = 7;
  params.ac_bk_params.txop_limit = 8;
  params.ac_bk_params.acm = false;

  params.ac_vi_params.aifsn = 9;
  params.ac_vi_params.ecw_min = 10;
  params.ac_vi_params.ecw_max = 11;
  params.ac_vi_params.txop_limit = 12;
  params.ac_vi_params.acm = true;

  params.ac_vo_params.aifsn = 13;
  params.ac_vo_params.ecw_min = 14;
  params.ac_vo_params.ecw_max = 15;
  params.ac_vo_params.txop_limit = 16;
  params.ac_vo_params.acm = true;

  ::fuchsia::wlan::internal::WmmStatusResponse resp;
  ConvertWmmStatus(&params, &resp);

  EXPECT_TRUE(resp.apsd);

  EXPECT_EQ(resp.ac_be_params.aifsn, 1);
  EXPECT_EQ(resp.ac_be_params.ecw_min, 2);
  EXPECT_EQ(resp.ac_be_params.ecw_max, 3);
  EXPECT_EQ(resp.ac_be_params.txop_limit, 4);
  EXPECT_FALSE(resp.ac_be_params.acm);

  EXPECT_EQ(resp.ac_bk_params.aifsn, 5);
  EXPECT_EQ(resp.ac_bk_params.ecw_min, 6);
  EXPECT_EQ(resp.ac_bk_params.ecw_max, 7);
  EXPECT_EQ(resp.ac_bk_params.txop_limit, 8);
  EXPECT_FALSE(resp.ac_bk_params.acm);

  EXPECT_EQ(resp.ac_vi_params.aifsn, 9);
  EXPECT_EQ(resp.ac_vi_params.ecw_min, 10);
  EXPECT_EQ(resp.ac_vi_params.ecw_max, 11);
  EXPECT_EQ(resp.ac_vi_params.txop_limit, 12);
  EXPECT_TRUE(resp.ac_vi_params.acm);

  EXPECT_EQ(resp.ac_vo_params.aifsn, 13);
  EXPECT_EQ(resp.ac_vo_params.ecw_min, 14);
  EXPECT_EQ(resp.ac_vo_params.ecw_max, 15);
  EXPECT_EQ(resp.ac_vo_params.txop_limit, 16);
  EXPECT_TRUE(resp.ac_vo_params.acm);
}

}  // namespace
}  // namespace wlanif

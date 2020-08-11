// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <fuchsia/wlan/stats/cpp/fidl.h>

#include <vector>

#include <ddk/protocol/wlanif.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <src/connectivity/wlan/drivers/wlanif/convert.h>
#include <wlan/common/element.h>

namespace wlanif {
namespace {
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

wlanif_bss_description_t FakeBssWithSsidLen(uint8_t ssid_len) {
  return {
      .ssid =
          {
              .len = ssid_len,
          },
      .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
  };
}

TEST(ConvertTest, ToFidlBSSDescription_SsidEmpty) {
  wlan_mlme::BSSDescription fidl_desc = {};
  ConvertBSSDescription(&fidl_desc, FakeBssWithSsidLen(0));
  auto status = ValidateMessage(&fidl_desc);
  EXPECT_EQ(status, ZX_OK);
}

TEST(ConvertTest, ToFidlBSSDescription_Ssid) {
  wlan_mlme::BSSDescription fidl_desc = {};
  ConvertBSSDescription(&fidl_desc, FakeBssWithSsidLen(3));
  auto status = ValidateMessage(&fidl_desc);
  EXPECT_EQ(status, ZX_OK);
}

TEST(ConvertTest, ToFidlBSSDescription_SsidMaxLength) {
  wlan_mlme::BSSDescription fidl_desc = {};
  ConvertBSSDescription(&fidl_desc, FakeBssWithSsidLen(32));
  auto status = ValidateMessage(&fidl_desc);
  EXPECT_EQ(status, ZX_OK);
}

TEST(ConvertTest, ToFidlBSSDescription_SsidTooLong) {
  wlan_mlme::BSSDescription fidl_desc = {};
  ConvertBSSDescription(&fidl_desc, FakeBssWithSsidLen(33));
  auto status = ValidateMessage(&fidl_desc);
  EXPECT_EQ(status, ZX_OK);
}

TEST(ConvertTest, ToVectorRateSets_InvalidRateCount) {
  wlanif_bss_description bss_desc{};
  std::vector<uint8_t> expected;

  bss_desc.num_rates = WLAN_MAC_MAX_RATES + 1;
  for (unsigned i = 0; i < WLAN_MAC_MAX_RATES + 1; i++) {
    bss_desc.rates[i] = i;
    expected.push_back(i);
  }
  std::vector<uint8_t> rates;

  ConvertRates(&rates, bss_desc);

  expected.resize(wlan_mlme::RATES_MAX_LEN);  // ConvertRates will truncate excess rates
  EXPECT_EQ(rates, expected);
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

}  // namespace
}  // namespace wlanif

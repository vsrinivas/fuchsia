/*
 * Copyright (c) 2020 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "device_inspect_test.h"

namespace wlan {
namespace brcmfmac {

constexpr uint16_t kUintPropertyNum = 5;
constexpr uint16_t kWindowPropertyNum = 5;

const std::vector<std::string> kRootMetrics = {"brcmfmac-phy"};
const std::vector<std::string> kConnMetrics = {"brcmfmac-phy", "connection-metrics"};

struct PropertyTestUnit {
  const std::vector<std::string> path_;
  std::string name_;
  std::function<void()> log_callback_;
  PropertyTestUnit(const std::vector<std::string> path, std::string name,
                   std::function<void()> callback)
      : path_(path), name_(name), log_callback_(callback) {}
};

class DeviceInspectTest : public DeviceInspectTestHelper {
 public:
  void LogTxQfull() { device_->inspect_.LogTxQueueFull(); }
  void LogConnSuccess() { device_->inspect_.LogConnSuccess(); }
  void LogConnNoNetworkFail() { device_->inspect_.LogConnNoNetworkFail(); }
  void LogConnAuthFail() { device_->inspect_.LogConnAuthFail(); }
  void LogConnOtherFail() { device_->inspect_.LogConnOtherFail(); }

  uint64_t GetUintProperty(const std::vector<std::string>& path, std::string name) {
    FetchHierarchy();
    auto* root = hierarchy_.value().GetByPath(path);
    EXPECT_TRUE(root);
    auto* uint_property = root->node().get_property<inspect::UintPropertyValue>(name);
    EXPECT_TRUE(uint_property);
    return uint_property->value();
  }

  // Defining properties which will be covered in the test cases.
  const PropertyTestUnit uint_properties_[kUintPropertyNum] = {
      PropertyTestUnit(kRootMetrics, "tx_qfull", std::bind(&DeviceInspectTest::LogTxQfull, this)),
      PropertyTestUnit(kConnMetrics, "success",
                       std::bind(&DeviceInspectTest::LogConnSuccess, this)),
      PropertyTestUnit(kConnMetrics, "no_network_fail",
                       std::bind(&DeviceInspectTest::LogConnNoNetworkFail, this)),
      PropertyTestUnit(kConnMetrics, "auth_fail",
                       std::bind(&DeviceInspectTest::LogConnAuthFail, this)),
      PropertyTestUnit(kConnMetrics, "other_fail",
                       std::bind(&DeviceInspectTest::LogConnOtherFail, this)),
  };
  const PropertyTestUnit window_properties_[kWindowPropertyNum] = {
      PropertyTestUnit(kRootMetrics, "tx_qfull_24hrs",
                       std::bind(&DeviceInspectTest::LogTxQfull, this)),
      PropertyTestUnit(kConnMetrics, "success_24hrs",
                       std::bind(&DeviceInspectTest::LogConnSuccess, this)),
      PropertyTestUnit(kConnMetrics, "no_network_fail_24hrs",
                       std::bind(&DeviceInspectTest::LogConnNoNetworkFail, this)),
      PropertyTestUnit(kConnMetrics, "auth_fail_24hrs",
                       std::bind(&DeviceInspectTest::LogConnAuthFail, this)),
      PropertyTestUnit(kConnMetrics, "other_fail_24hrs",
                       std::bind(&DeviceInspectTest::LogConnOtherFail, this)),
  };
};

TEST_F(DeviceInspectTest, HierarchyCreation) {
  FetchHierarchy();
  ASSERT_TRUE(hierarchy_.is_ok());
}

TEST_F(DeviceInspectTest, SimpleIncrementCounterSingle) {
  // Going over all inspect counters inside this loop.
  for (uint16_t k = 0; k < kUintPropertyNum; k++) {
    BRCMF_INFO("Testing %s", uint_properties_[k].name_.c_str());
    EXPECT_EQ(0u, GetUintProperty(uint_properties_[k].path_, uint_properties_[k].name_));
    uint_properties_[k].log_callback_();
    EXPECT_EQ(1u, GetUintProperty(uint_properties_[k].path_, uint_properties_[k].name_));
  }
}

TEST_F(DeviceInspectTest, SimpleIncrementCounterMultiple) {
  const uint32_t property_cnt = 100;

  // Outer Loop is to go over all inspect counters.
  for (uint16_t k = 0; k < kUintPropertyNum; k++) {
    BRCMF_INFO("Testing %s", uint_properties_[k].name_.c_str());
    EXPECT_EQ(0u, GetUintProperty(uint_properties_[k].path_, uint_properties_[k].name_));
    for (uint32_t i = 0; i < property_cnt; i++) {
      uint_properties_[k].log_callback_();
    }
    EXPECT_EQ(property_cnt, GetUintProperty(uint_properties_[k].path_, uint_properties_[k].name_));
  }
}

TEST_F(DeviceInspectTest, SimpleIncrementCounter24HrsFor10Hrs) {
  // Outer Loop is to go over all inspect counters.
  for (uint16_t k = 0; k < kWindowPropertyNum; k++) {
    BRCMF_INFO("Testing %s", window_properties_[k].name_.c_str());
    EXPECT_EQ(0u, GetUintProperty(window_properties_[k].path_, window_properties_[k].name_));
    const uint32_t log_duration = 10;
    // Log 1 queue full event every hour, for 'log_duration' hours.
    for (uint32_t i = 0; i < log_duration; i++) {
      SCHEDULE_CALL(zx::hour(i), window_properties_[k].log_callback_);
    }
    env_->Run(zx::hour(log_duration));

    // Since we also log once at the beginning of the run, we will have one more count.
    EXPECT_EQ(log_duration,
              GetUintProperty(window_properties_[k].path_, window_properties_[k].name_));
  }
}

TEST_F(DeviceInspectTest, LogTxQfull24HrsFor100Hrs) {
  // Outer Loop is to go over all inspect counters.
  for (uint16_t k = 0; k < kWindowPropertyNum; k++) {
    BRCMF_INFO("Testing %s", window_properties_[k].name_.c_str());
    EXPECT_EQ(0u, GetUintProperty(window_properties_[k].path_, window_properties_[k].name_));
    const uint32_t log_duration = 100;

    // Log 1 queue full event every hour, for 'log_duration' hours.
    for (uint32_t i = 0; i < log_duration; i++) {
      SCHEDULE_CALL(zx::hour(i), window_properties_[k].log_callback_);
    }
    env_->Run(zx::hour(log_duration));

    // Since log_duration is > 24hrs, we expect the rolling counter to show
    // a count of only 24.
    EXPECT_EQ(24u, GetUintProperty(window_properties_[k].path_, window_properties_[k].name_));
  }
}

}  // namespace brcmfmac
}  // namespace wlan

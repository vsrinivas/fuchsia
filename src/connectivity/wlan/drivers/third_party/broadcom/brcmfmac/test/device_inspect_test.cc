/*
 * Copyright (c) 2021 The Fuchsia Authors
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

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/inspect/device_inspect.h"

#include <lib/async/cpp/task.h>
#include <lib/zx/time.h>

#include <functional>
#include <memory>

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/test/device_inspect_test_utils.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace wlan {
namespace brcmfmac {

constexpr uint16_t kUintPropertyNum = 2;
constexpr uint16_t kWindowPropertyNum = 2;

struct PropertyTestUnit {
  std::string name_;
  std::function<void()> log_callback_;
  PropertyTestUnit(std::string name, std::function<void()> callback)
      : name_(name), log_callback_(callback) {}
};

class DeviceInspectTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override { ASSERT_EQ(ZX_OK, DeviceInspect::Create(dispatcher(), &device_inspect_)); }

  void LogTxQfull() { device_inspect_->LogTxQueueFull(); }
  void LogFwRcvr() { device_inspect_->LogFwRecovered(); }

  uint64_t GetUintProperty(std::string name) {
    auto hierarchy = FetchHierarchy(device_inspect_->inspector());
    auto* root = hierarchy.value().GetByPath({"brcmfmac-phy"});
    EXPECT_TRUE(root);
    auto* uint_property = root->node().get_property<inspect::UintPropertyValue>(name);
    EXPECT_TRUE(uint_property);
    return uint_property->value();
  }

  std::unique_ptr<DeviceInspect> device_inspect_;
  // Defining properties which will be covered in the test cases.
  const PropertyTestUnit uint_properties_[kUintPropertyNum] = {
      PropertyTestUnit("tx_qfull", std::bind(&DeviceInspectTest::LogTxQfull, this)),
      PropertyTestUnit("fw_recovered", std::bind(&DeviceInspectTest::LogFwRcvr, this)),
  };
  const PropertyTestUnit window_properties_[kWindowPropertyNum] = {
      PropertyTestUnit("tx_qfull_24hrs", std::bind(&DeviceInspectTest::LogTxQfull, this)),
      PropertyTestUnit("fw_recovered_24hrs", std::bind(&DeviceInspectTest::LogFwRcvr, this)),
  };
};

TEST_F(DeviceInspectTest, HierarchyCreation) {
  auto hierarchy = FetchHierarchy(device_inspect_->inspector());
  ASSERT_TRUE(hierarchy.is_ok());
}

TEST_F(DeviceInspectTest, SimpleIncrementCounterSingle) {
  // Going over all inspect counters inside this loop.
  for (uint16_t k = 0; k < kUintPropertyNum; k++) {
    EXPECT_EQ(0u, GetUintProperty(uint_properties_[k].name_));
    uint_properties_[k].log_callback_();
    EXPECT_EQ(1u, GetUintProperty(uint_properties_[k].name_));
  }
}

TEST_F(DeviceInspectTest, SimpleIncrementCounterMultiple) {
  const uint32_t property_cnt = 100;

  // Outer Loop is to go over all inspect counters.
  for (uint16_t k = 0; k < kUintPropertyNum; k++) {
    EXPECT_EQ(0u, GetUintProperty(uint_properties_[k].name_));
    for (uint32_t i = 0; i < property_cnt; i++) {
      uint_properties_[k].log_callback_();
    }
    EXPECT_EQ(property_cnt, GetUintProperty(uint_properties_[k].name_));
  }
}

TEST_F(DeviceInspectTest, SimpleIncrementCounter24HrsFor10Hrs) {
  constexpr zx::duration kLogDuration = zx::hour(10);

  // Outer Loop is to go over all inspect counters.
  for (uint16_t k = 0; k < kWindowPropertyNum; k++) {
    EXPECT_EQ(0u, GetUintProperty(window_properties_[k].name_));
    // Log 1 queue full event every hour, including the first and last, for 'log_duration' hours.
    for (zx::duration i; i <= kLogDuration; i += zx::hour(1)) {
      async::PostDelayedTask(dispatcher(), window_properties_[k].log_callback_, i);
    }
    RunLoopFor(kLogDuration);

    // Since we also log once at the beginning of the run, we will have one more count.
    EXPECT_EQ(static_cast<uint64_t>(kLogDuration / zx::hour(1)) + 1,
              GetUintProperty(window_properties_[k].name_));
  }
}

TEST_F(DeviceInspectTest, LogTxQfull24HrsFor100Hrs) {
  constexpr zx::duration kLogDuration = zx::hour(100);

  // Outer Loop is to go over all inspect counters.
  for (uint16_t k = 0; k < kWindowPropertyNum; k++) {
    EXPECT_EQ(0u, GetUintProperty(window_properties_[k].name_));

    // Log 1 queue full event every hour, including the first and last, for 'log_duration' hours.
    for (zx::duration i; i <= kLogDuration; i += zx::hour(1)) {
      async::PostDelayedTask(dispatcher(), window_properties_[k].log_callback_, i);
    }
    RunLoopFor(kLogDuration);

    // Since log_duration is > 24hrs, we expect the rolling counter to show
    // a count of only 24.
    EXPECT_EQ(24u, GetUintProperty(window_properties_[k].name_));
  }
}

}  // namespace brcmfmac
}  // namespace wlan

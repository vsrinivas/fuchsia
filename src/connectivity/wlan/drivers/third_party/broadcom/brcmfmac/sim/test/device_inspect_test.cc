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

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/device_inspect_test.h"

#include <functional>

namespace wlan {
namespace brcmfmac {

class DeviceInspectTest : public DeviceInspectTestHelper {
 public:
  void LogTxQfull() { device_->inspect_.LogTxQueueFull(); }

  uint64_t GetTxQfull() {
    FetchHierarchy();
    auto* root = hierarchy_.value().GetByPath({"brcmfmac-phy"});
    EXPECT_TRUE(root);
    auto* tx_qfull = root->node().get_property<inspect::UintPropertyValue>("tx_qfull");
    EXPECT_TRUE(tx_qfull);
    return tx_qfull->value();
  }

  uint64_t GetTxQfull24Hrs() {
    FetchHierarchy();
    auto* root = hierarchy_.value().GetByPath({"brcmfmac-phy"});
    EXPECT_TRUE(root);
    auto* tx_qfull_24hrs = root->node().get_property<inspect::UintPropertyValue>("tx_qfull_24hrs");
    EXPECT_TRUE(tx_qfull_24hrs);
    return tx_qfull_24hrs->value();
  }
};

TEST_F(DeviceInspectTest, HierarchyCreation) {
  FetchHierarchy();
  ASSERT_TRUE(hierarchy_.is_ok());
}

TEST_F(DeviceInspectTest, LogTxQfullSingle) {
  EXPECT_EQ(0u, GetTxQfull());
  LogTxQfull();
  EXPECT_EQ(1u, GetTxQfull());
}

TEST_F(DeviceInspectTest, LogTxQfullMultiple) {
  const uint32_t qfull_cnt = 100;

  EXPECT_EQ(0u, GetTxQfull());
  for (uint32_t i = 0; i < qfull_cnt; i++) {
    LogTxQfull();
  }
  EXPECT_EQ(qfull_cnt, GetTxQfull());
}

TEST_F(DeviceInspectTest, LogTxQfull24HrsFor10Hrs) {
  EXPECT_EQ(0u, GetTxQfull24Hrs());
  const uint32_t log_duration = 10;

  // Log 1 queue full event every hour, including the first and last, for 'log_duration' hours.
  for (uint32_t i = 0; i <= log_duration; i++) {
    env_->ScheduleNotification(std::bind(&DeviceInspectTest::LogTxQfull, this), zx::hour(i));
  }
  env_->Run(zx::hour(log_duration));

  // Since we also log once at the beginning of the run, we will have one more count.
  EXPECT_EQ(log_duration + 1, GetTxQfull24Hrs());
}

TEST_F(DeviceInspectTest, LogTxQfull24HrsFor100Hrs) {
  EXPECT_EQ(0u, GetTxQfull24Hrs());
  const uint32_t log_duration = 100;

  // Log 1 queue full event every hour, including the first and last, for 'log_duration' hours.
  for (uint32_t i = 0; i <= log_duration; i++) {
    env_->ScheduleNotification(std::bind(&DeviceInspectTest::LogTxQfull, this), zx::hour(i));
  }
  env_->Run(zx::hour(log_duration));

  // Since log_duration is > 24hrs, we expect the rolling counter to show
  // a count of only 24.
  EXPECT_EQ(24u, GetTxQfull24Hrs());
}

}  // namespace brcmfmac
}  // namespace wlan

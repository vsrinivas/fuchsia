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
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/time.h>

#include <functional>
#include <memory>

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/test/device_inspect_test_utils.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace wlan {
namespace brcmfmac {

class DeviceInspectTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override { ASSERT_EQ(ZX_OK, DeviceInspect::Create(dispatcher(), &device_inspect_)); }

  void LogTxQfull() { device_inspect_->LogTxQueueFull(); }

  uint64_t GetTxQfull() {
    auto hierarchy = FetchHierarchy(device_inspect_->inspector());
    auto* root = hierarchy.value().GetByPath({"brcmfmac-phy"});
    EXPECT_TRUE(root);
    auto* tx_qfull = root->node().get_property<inspect::UintPropertyValue>("tx_qfull");
    EXPECT_TRUE(tx_qfull);
    return tx_qfull->value();
  }

  uint64_t GetTxQfull24Hrs() {
    auto hierarchy = FetchHierarchy(device_inspect_->inspector());
    auto* root = hierarchy.value().GetByPath({"brcmfmac-phy"});
    EXPECT_TRUE(root);
    auto* tx_qfull_24hrs = root->node().get_property<inspect::UintPropertyValue>("tx_qfull_24hrs");
    EXPECT_TRUE(tx_qfull_24hrs);
    return tx_qfull_24hrs->value();
  }

  std::unique_ptr<DeviceInspect> device_inspect_;
};

TEST_F(DeviceInspectTest, HierarchyCreation) {
  auto hierarchy = FetchHierarchy(device_inspect_->inspector());
  ASSERT_TRUE(hierarchy.is_ok());
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
  constexpr zx::duration kLogDuration = zx::hour(10);

  // Log 1 queue full event every hour, including the first and last, for 'log_duration' hours.
  for (zx::duration i; i <= kLogDuration; i += zx::hour(1)) {
    async::PostDelayedTask(dispatcher(), std::bind(&DeviceInspectTest::LogTxQfull, this), i);
  }
  RunLoopFor(kLogDuration);

  // Since we also log once at the beginning of the run, we will have one more count.
  EXPECT_EQ(static_cast<uint64_t>(kLogDuration / zx::hour(1)) + 1, GetTxQfull24Hrs());
}

TEST_F(DeviceInspectTest, LogTxQfull24HrsFor100Hrs) {
  EXPECT_EQ(0u, GetTxQfull24Hrs());
  constexpr zx::duration kLogDuration = zx::hour(100);

  // Log 1 queue full event every hour, including the first and last, for 'log_duration' hours.
  for (zx::duration i; i <= kLogDuration; i += zx::hour(1)) {
    async::PostDelayedTask(dispatcher(), std::bind(&DeviceInspectTest::LogTxQfull, this), i);
  }
  RunLoopFor(kLogDuration);

  // Since log_duration is > 24hrs, we expect the rolling counter to show a count of only 24.
  EXPECT_EQ(24u, GetTxQfull24Hrs());
}

}  // namespace brcmfmac
}  // namespace wlan

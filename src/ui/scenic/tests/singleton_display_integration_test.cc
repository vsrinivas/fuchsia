// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/display/singleton/cpp/fidl.h>
#include <lib/async-loop/testing/cpp/real_loop.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

#include <zxtest/zxtest.h>

#include "src/ui/scenic/tests/utils/scenic_realm_builder.h"

namespace integration_tests {

using fuc_FlatlandDisplay = fuchsia::ui::composition::FlatlandDisplay;
using fuds_Metrics = fuchsia::ui::display::singleton::Metrics;
using fuds_Info = fuchsia::ui::display::singleton::Info;
using fuds_InfoPtr = fuchsia::ui::display::singleton::InfoPtr;
using RealmRoot = component_testing::RealmRoot;

// Max timeout in failure cases.
// Set this as low as you can that still works across all test platforms.
static constexpr zx::duration kTimeout = zx::min(5);

class SingletonDisplayIntegrationTest : public zxtest::Test, public loop_fixture::RealLoop {
 protected:
  SingletonDisplayIntegrationTest() = default;

  void SetUp() override {
    // Post a "just in case" quit task, if the test hangs.
    async::PostDelayedTask(
        dispatcher(),
        [] { FX_LOGS(FATAL) << "\n\n>> Test did not complete in time, terminating.  <<\n\n"; },
        kTimeout);

    realm_ = std::make_unique<RealmRoot>(ScenicRealmBuilder()
                                             .AddRealmProtocol(fuds_Info::Name_)
                                             .AddRealmProtocol(fuc_FlatlandDisplay::Name_)
                                             .Build());
    singleton_display_ = realm_->Connect<fuds_Info>();
    singleton_display_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to SingletonDisplay: %s", zx_status_get_string(status));
    });
  }

  std::unique_ptr<RealmRoot> realm_;
  fuds_InfoPtr singleton_display_;
};

TEST_F(SingletonDisplayIntegrationTest, GetMetrics) {
  std::optional<fuds_Metrics> metrics;
  singleton_display_->GetMetrics([&metrics](fuds_Metrics di) { metrics = std::move(di); });

  RunLoopUntil([&metrics] { return metrics.has_value(); });

  // All of the expected values below are hard-coded within the "Fake HDCP component", except for
  // the recommended_device_pixel_ratio, which is computed heuristically based on the other values.

  ASSERT_TRUE(metrics->has_extent_in_px());
  ASSERT_TRUE(metrics->has_extent_in_mm());
  ASSERT_TRUE(metrics->has_recommended_device_pixel_ratio());

  EXPECT_EQ(1280, metrics->extent_in_px().width);
  EXPECT_EQ(800, metrics->extent_in_px().height);
  EXPECT_EQ(160, metrics->extent_in_mm().width);
  EXPECT_EQ(90, metrics->extent_in_mm().height);
  EXPECT_EQ(1.f, metrics->recommended_device_pixel_ratio().x);
  EXPECT_EQ(1.f, metrics->recommended_device_pixel_ratio().y);
}

TEST_F(SingletonDisplayIntegrationTest, DevicePixelRatioChange) {
  auto flatland_display = realm_->Connect<fuc_FlatlandDisplay>();
  const float kDPRx = 1.25f;
  const float kDPRy = 1.25f;
  flatland_display->SetDevicePixelRatio({kDPRx, kDPRy});

  // FlatlandDisplay lives on a Flatland thread and SingletonDisplay lives on the main thread, so
  // the update may not be sequential.
  RunLoopUntil([this, kDPRx, kDPRy] {
    std::optional<fuds_Metrics> metrics;
    singleton_display_->GetMetrics([&metrics](fuds_Metrics di) { metrics = std::move(di); });
    RunLoopUntil([&metrics] { return metrics.has_value(); });
    return metrics->has_recommended_device_pixel_ratio() &&
           kDPRx == metrics->recommended_device_pixel_ratio().x &&
           kDPRy == metrics->recommended_device_pixel_ratio().y;
  });
}

}  // namespace integration_tests

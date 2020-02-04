// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/attachments/screenshot_ptr.h"

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <memory>
#include <vector>

#include "src/developer/feedback/feedback_agent/tests/stub_scenic.h"
#include "src/developer/feedback/testing/cobalt_test_fixture.h"
#include "src/developer/feedback/testing/stubs/stub_cobalt_logger_factory.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/developer/feedback/utils//cobalt_metrics.h"
#include "src/developer/feedback/utils/cobalt_event.h"
#include "src/lib/fxl/logging.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using fuchsia::ui::scenic::ScreenshotData;
using testing::UnorderedElementsAreArray;

constexpr bool kSuccess = true;

class TakeScreenshotTest : public UnitTestFixture, public CobaltTestFixture {
 public:
  TakeScreenshotTest() : CobaltTestFixture(/*unit_test_fixture=*/this), executor_(dispatcher()) {}

 protected:
  void SetUpScreenshotProvider(std::unique_ptr<StubScenic> screenshot_provider) {
    screenshot_provider_ = std::move(screenshot_provider);
    if (screenshot_provider_) {
      InjectServiceProvider(screenshot_provider_.get());
    }
  }

  fit::result<ScreenshotData> TakeScreenshot(const zx::duration timeout = zx::sec(1)) {
    fit::result<ScreenshotData> result;
    executor_.schedule_task(
        feedback::TakeScreenshot(dispatcher(), services(), timeout,
                                 std::make_shared<Cobalt>(dispatcher(), services()))
            .then([&result](fit::result<ScreenshotData>& res) { result = std::move(res); }));
    RunLoopFor(timeout);
    return result;
  }

  async::Executor executor_;

 private:
  std::unique_ptr<StubScenic> screenshot_provider_;
};

TEST_F(TakeScreenshotTest, Succeed_CheckerboardScreenshot) {
  const size_t image_dim_in_px = 100;
  std::vector<TakeScreenshotResponse> screenshot_provider_responses;
  screenshot_provider_responses.emplace_back(CreateCheckerboardScreenshot(image_dim_in_px),
                                             kSuccess);
  std::unique_ptr<StubScenic> scenic = std::make_unique<StubScenic>();
  scenic->set_take_screenshot_responses(std::move(screenshot_provider_responses));
  SetUpScreenshotProvider(std::move(scenic));

  fit::result<ScreenshotData> result = TakeScreenshot();

  ASSERT_TRUE(result.is_ok());
  ScreenshotData screenshot = result.take_value();
  EXPECT_TRUE(screenshot.data.vmo.is_valid());
  EXPECT_EQ(static_cast<size_t>(screenshot.info.height), image_dim_in_px);
  EXPECT_EQ(static_cast<size_t>(screenshot.info.width), image_dim_in_px);
  EXPECT_EQ(screenshot.info.stride, image_dim_in_px * 4u);
  EXPECT_EQ(screenshot.info.pixel_format, fuchsia::images::PixelFormat::BGRA_8);
}

TEST_F(TakeScreenshotTest, Fail_ScenicNotAvailable) {
  SetUpScreenshotProvider(nullptr);

  fit::result<ScreenshotData> result = TakeScreenshot();

  ASSERT_TRUE(result.is_error());
}

TEST_F(TakeScreenshotTest, Fail_ScenicReturningFalse) {
  SetUpScreenshotProvider(std::make_unique<StubScenicAlwaysReturnsFalse>());

  fit::result<ScreenshotData> result = TakeScreenshot();

  ASSERT_TRUE(result.is_error());
}

TEST_F(TakeScreenshotTest, Fail_ScenicClosesConnection) {
  SetUpScreenshotProvider(std::make_unique<StubScenicClosesConnection>());

  fit::result<ScreenshotData> result = TakeScreenshot();

  ASSERT_TRUE(result.is_error());
}

TEST_F(TakeScreenshotTest, Fail_ScenicNeverReturns) {
  SetUpScreenshotProvider(std::make_unique<StubScenicNeverReturns>());
  SetUpCobaltLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<ScreenshotData> result = TakeScreenshot();

  ASSERT_TRUE(result.is_error());
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          CobaltEvent(TimedOutData::kScreenshot),
                                      }));
}

TEST_F(TakeScreenshotTest, Fail_CallTakeScreenshotTwice) {
  std::vector<TakeScreenshotResponse> screenshot_provider_responses;
  screenshot_provider_responses.emplace_back(CreateEmptyScreenshot(), kSuccess);
  auto screenshot_provider = std::make_unique<StubScenic>();
  screenshot_provider->set_take_screenshot_responses(std::move(screenshot_provider_responses));
  SetUpScreenshotProvider(std::move(screenshot_provider));

  const zx::duration unused_timeout = zx::sec(1);
  Scenic scenic(dispatcher(), services(), std::make_shared<Cobalt>(dispatcher(), services()));
  executor_.schedule_task(scenic.TakeScreenshot(unused_timeout));
  ASSERT_DEATH(scenic.TakeScreenshot(unused_timeout),
               testing::HasSubstr("TakeScreenshot() is not intended to be called twice"));
}

}  // namespace
}  // namespace feedback

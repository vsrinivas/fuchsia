// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/annotations/last_reboot_info_provider.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/cpp/executor.h>

#include <memory>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/stubs/last_reboot_info_provider.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/cobalt/event.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/time.h"
#include "src/lib/timekeeper/test_clock.h"

namespace forensics {
namespace feedback_data {
namespace {

using fuchsia::feedback::LastReboot;
using fuchsia::feedback::RebootReason;
using testing::ElementsAreArray;
using testing::Pair;

constexpr RebootReason kRebootReason = RebootReason::KERNEL_PANIC;
constexpr zx::duration kUptime = zx::msec(100);

class LastRebootInfoProviderTest : public UnitTestFixture {
 public:
  LastRebootInfoProviderTest() : executor_(dispatcher()) {}

 protected:
  void SetUpLastRebootInfoProviderServer(
      std::unique_ptr<stubs::LastRebootInfoProviderBase> server) {
    last_reboot_info_provider_server_ = std::move(server);
    if (last_reboot_info_provider_server_) {
      InjectServiceProvider(last_reboot_info_provider_server_.get());
    }
  }

  Annotations GetLastRebootReason(const AnnotationKeys& allowlist,
                                  const zx::duration timeout = zx::sec(1)) {
    SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
    cobalt::Logger cobalt(dispatcher(), services(), &clock_);

    LastRebootInfoProvider provider(dispatcher(), services(), &cobalt);
    auto promise = provider.GetAnnotations(timeout, allowlist);

    Annotations annotations;
    executor_.schedule_task(
        std::move(promise).then([&annotations](::fit::result<Annotations>& res) {
          if (res.is_ok()) {
            annotations = res.take_value();
          }
        }));
    RunLoopFor(timeout);

    return annotations;
  }

 private:
  async::Executor executor_;
  timekeeper::TestClock clock_;
  std::unique_ptr<stubs::LastRebootInfoProviderBase> last_reboot_info_provider_server_;
};

TEST_F(LastRebootInfoProviderTest, Success_ReasonAndUptimeReturned) {
  const auto uptime_str = FormatDuration(kUptime);
  ASSERT_TRUE(uptime_str.has_value());

  LastReboot last_reboot;
  last_reboot.set_reason(kRebootReason).set_uptime(kUptime.get());

  SetUpLastRebootInfoProviderServer(
      std::make_unique<stubs::LastRebootInfoProvider>(std::move(last_reboot)));

  const auto result = GetLastRebootReason({
      kAnnotationSystemLastRebootReason,
      kAnnotationSystemLastRebootUptime,
  });
  EXPECT_THAT(result, ElementsAreArray({
                          Pair(kAnnotationSystemLastRebootReason, AnnotationOr("kernel panic")),
                          Pair(kAnnotationSystemLastRebootUptime, AnnotationOr(uptime_str.value())),
                      }));
}

TEST_F(LastRebootInfoProviderTest, Succeed_NoUptimeReturned) {
  LastReboot last_reboot;
  last_reboot.set_reason(kRebootReason);

  SetUpLastRebootInfoProviderServer(
      std::make_unique<stubs::LastRebootInfoProvider>(std::move(last_reboot)));

  const auto result = GetLastRebootReason({
      kAnnotationSystemLastRebootReason,
      kAnnotationSystemLastRebootUptime,
  });
  EXPECT_THAT(result,
              ElementsAreArray({
                  Pair(kAnnotationSystemLastRebootReason, AnnotationOr("kernel panic")),
                  Pair(kAnnotationSystemLastRebootUptime, AnnotationOr(Error::kMissingValue)),
              }));
}

TEST_F(LastRebootInfoProviderTest, Succeed_NoRequestedKeysInAllowlist) {
  LastReboot last_reboot;
  last_reboot.set_reason(kRebootReason);

  SetUpLastRebootInfoProviderServer(
      std::make_unique<stubs::LastRebootInfoProvider>(std::move(last_reboot)));

  const auto result = GetLastRebootReason({
      "not-returned-by-last-reboot-reason-provider",
  });
  EXPECT_TRUE(result.empty());
}

TEST_F(LastRebootInfoProviderTest, Success_GracefulWithoutReason) {
  const auto uptime_str = FormatDuration(kUptime);
  ASSERT_TRUE(uptime_str.has_value());

  LastReboot last_reboot;
  last_reboot.set_graceful(true).set_uptime(kUptime.get());

  SetUpLastRebootInfoProviderServer(
      std::make_unique<stubs::LastRebootInfoProvider>(std::move(last_reboot)));

  const auto result = GetLastRebootReason({
      kAnnotationSystemLastRebootReason,
      kAnnotationSystemLastRebootUptime,
  });
  EXPECT_THAT(result, ElementsAreArray({
                          Pair(kAnnotationSystemLastRebootReason, AnnotationOr("graceful")),
                          Pair(kAnnotationSystemLastRebootUptime, AnnotationOr(uptime_str.value())),
                      }));
}

TEST_F(LastRebootInfoProviderTest, Success_UngracefulWithoutReason) {
  const auto uptime_str = FormatDuration(kUptime);
  ASSERT_TRUE(uptime_str.has_value());

  LastReboot last_reboot;
  last_reboot.set_graceful(false).set_uptime(kUptime.get());

  SetUpLastRebootInfoProviderServer(
      std::make_unique<stubs::LastRebootInfoProvider>(std::move(last_reboot)));

  const auto result = GetLastRebootReason({
      kAnnotationSystemLastRebootReason,
      kAnnotationSystemLastRebootUptime,
  });
  EXPECT_THAT(result, ElementsAreArray({
                          Pair(kAnnotationSystemLastRebootReason, AnnotationOr("ungraceful")),
                          Pair(kAnnotationSystemLastRebootUptime, AnnotationOr(uptime_str.value())),
                      }));
}

TEST_F(LastRebootInfoProviderTest, Success_NoReasonOrGraceful) {
  const auto uptime_str = FormatDuration(kUptime);
  ASSERT_TRUE(uptime_str.has_value());

  LastReboot last_reboot;
  last_reboot.set_uptime(kUptime.get());

  SetUpLastRebootInfoProviderServer(
      std::make_unique<stubs::LastRebootInfoProvider>(std::move(last_reboot)));

  const auto result = GetLastRebootReason({
      kAnnotationSystemLastRebootReason,
      kAnnotationSystemLastRebootUptime,
  });
  EXPECT_THAT(result,
              ElementsAreArray({
                  Pair(kAnnotationSystemLastRebootReason, AnnotationOr(Error::kMissingValue)),
                  Pair(kAnnotationSystemLastRebootUptime, AnnotationOr(uptime_str.value())),
              }));
}

TEST_F(LastRebootInfoProviderTest, Check_CobaltLogsTimeout) {
  SetUpLastRebootInfoProviderServer(std::make_unique<stubs::LastRebootInfoProviderNeverReturns>());

  const auto result = GetLastRebootReason({
      kAnnotationSystemLastRebootReason,
      kAnnotationSystemLastRebootUptime,
  });
  EXPECT_THAT(result, ElementsAreArray({
                          Pair(kAnnotationSystemLastRebootReason, AnnotationOr(Error::kTimeout)),
                          Pair(kAnnotationSystemLastRebootUptime, AnnotationOr(Error::kTimeout)),
                      }));
  EXPECT_THAT(ReceivedCobaltEvents(), ElementsAreArray({
                                          cobalt::Event(cobalt::TimedOutData::kLastRebootInfo),
                                      }));
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/annotations/timezone_provider.h"

#include <lib/async/cpp/executor.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <map>
#include <memory>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/testing/stubs/timezone_provider.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/lib/fxl/strings/split_string.h"

namespace forensics::feedback_data {
namespace {

using testing::ElementsAreArray;
using testing::Pair;

class TimezoneProviderTest : public UnitTestFixture {
 public:
  TimezoneProviderTest() : executor_(dispatcher()) {}

 protected:
  void SetUpTimezoneProviderServer(std::unique_ptr<stubs::TimezoneProviderBase> server) {
    timezone_provider_server_ = std::move(server);
    if (timezone_provider_server_) {
      InjectServiceProvider(timezone_provider_server_.get());
    }
  }

  async::Executor executor_;

 private:
  std::unique_ptr<stubs::TimezoneProviderBase> timezone_provider_server_;
};

TEST_F(TimezoneProviderTest, GetAnnotations) {
  auto server = std::make_unique<stubs::TimezoneProvider>("timezone-one");
  auto& server_ref = *server.get();
  SetUpTimezoneProviderServer(std::move(server));

  TimezoneProvider provider(dispatcher(), services());
  RunLoopUntilIdle();

  Annotations annotations;
  executor_.schedule_task(provider.GetAnnotations(zx::sec(1), {kAnnotationSystemTimezonePrimary})
                              .then([&annotations](::fpromise::result<Annotations>& res) {
                                FX_CHECK(res.is_ok());
                                annotations = res.take_value();
                              }));

  RunLoopUntilIdle();
  EXPECT_THAT(annotations, ElementsAreArray({
                               Pair(kAnnotationSystemTimezonePrimary, "timezone-one"),
                           }));

  server_ref.SetTimezone("timezone-two");
  RunLoopUntilIdle();

  executor_.schedule_task(provider.GetAnnotations(zx::sec(1), {kAnnotationSystemTimezonePrimary})
                              .then([&annotations](::fpromise::result<Annotations>& res) {
                                FX_CHECK(res.is_ok());
                                annotations = res.take_value();
                              }));

  RunLoopUntilIdle();
  EXPECT_THAT(annotations, ElementsAreArray({
                               Pair(kAnnotationSystemTimezonePrimary, "timezone-two"),
                           }));
}

TEST_F(TimezoneProviderTest, GetAnnotations_Delay) {
  const auto kDelay = zx::sec(5);
  SetUpTimezoneProviderServer(std::make_unique<stubs::TimezoneProviderDelaysResponse>(
      dispatcher(), kDelay, "timezone-one"));

  TimezoneProvider provider(dispatcher(), services());
  RunLoopUntilIdle();

  Annotations annotations;
  executor_.schedule_task(provider.GetAnnotations(zx::sec(10), {kAnnotationSystemTimezonePrimary})
                              .then([&annotations](::fpromise::result<Annotations>& res) {
                                FX_CHECK(res.is_ok());
                                annotations = res.take_value();
                              }));

  RunLoopUntilIdle();
  EXPECT_TRUE(annotations.empty());

  RunLoopFor(kDelay);
  EXPECT_THAT(annotations, ElementsAreArray({
                               Pair(kAnnotationSystemTimezonePrimary, "timezone-one"),
                           }));
}

TEST_F(TimezoneProviderTest, GetAnnotations_LosesConnection) {
  auto server = std::make_unique<stubs::TimezoneProvider>("timezone-one");
  auto& server_ref = *server.get();
  SetUpTimezoneProviderServer(std::move(server));

  TimezoneProvider provider(dispatcher(), services());
  RunLoopUntilIdle();

  Annotations annotations;
  executor_.schedule_task(provider.GetAnnotations(zx::sec(1), {kAnnotationSystemTimezonePrimary})
                              .then([&annotations](::fpromise::result<Annotations>& res) {
                                FX_CHECK(res.is_ok());
                                annotations = res.take_value();
                              }));

  RunLoopUntilIdle();
  EXPECT_THAT(annotations, ElementsAreArray({
                               Pair(kAnnotationSystemTimezonePrimary, "timezone-one"),
                           }));

  server_ref.CloseConnection();
  RunLoopUntilIdle();

  executor_.schedule_task(provider.GetAnnotations(zx::sec(1), {kAnnotationSystemTimezonePrimary})
                              .then([&annotations](::fpromise::result<Annotations>& res) {
                                FX_CHECK(res.is_ok());
                                annotations = res.take_value();
                              }));

  // |provider| isn't expected to have reconnected yet.
  server_ref.SetTimezone("timezone-two");
  RunLoopUntilIdle();
  EXPECT_THAT(annotations, ElementsAreArray({
                               Pair(kAnnotationSystemTimezonePrimary, "timezone-one"),
                           }));

  // Run the loop for longer than the reconnection delay.
  RunLoopFor(zx::min(1));
  executor_.schedule_task(provider.GetAnnotations(zx::sec(1), {kAnnotationSystemTimezonePrimary})
                              .then([&annotations](::fpromise::result<Annotations>& res) {
                                FX_CHECK(res.is_ok());
                                annotations = res.take_value();
                              }));
  RunLoopUntilIdle();
  EXPECT_THAT(annotations, ElementsAreArray({
                               Pair(kAnnotationSystemTimezonePrimary, "timezone-two"),
                           }));
}

}  // namespace
}  // namespace forensics::feedback_data

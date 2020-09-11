// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/activity_dispatcher.h"

#include <lib/fidl/cpp/binding.h>
#include <lib/gtest/test_loop_fixture.h>

#include <gtest/gtest.h>

namespace media::audio {

using RenderActivity = ActivityDispatcherImpl::RenderActivity;
using RenderUsageVector = std::vector<fuchsia::media::AudioRenderUsage>;

namespace {
RenderActivity UsageVectorToActivity(
    const std::vector<fuchsia::media::AudioRenderUsage>& usage_vector) {
  RenderActivity activity;
  for (const auto& usage : usage_vector) {
    activity.set(static_cast<int>(usage));
  }
  return activity;
}
}  // namespace

class ActivityDispatcherTest : public gtest::TestLoopFixture {
 protected:
  // Simulates a consumer connecting to the dispatcher.
  fuchsia::media::ActivityReporterPtr GetClient() {
    fuchsia::media::ActivityReporterPtr activity_reporter;
    activity_dispatcher_.GetFidlRequestHandler()(activity_reporter.NewRequest());
    return activity_reporter;
  }

  // Simulates a new set of usages being active.
  void UpdateActivity(const RenderUsageVector& usage_vector) {
    activity_dispatcher_.OnRenderActivityChanged(UsageVectorToActivity(usage_vector));
  }

 private:
  ActivityDispatcherImpl activity_dispatcher_;
};

TEST_F(ActivityDispatcherTest, FirstWatchReturnsImmediately) {
  fuchsia::media::ActivityReporterPtr activity_reporter = GetClient();

  bool called = false;
  activity_reporter->WatchRenderActivity([&](const RenderUsageVector& activity) { called = true; });
  RunLoopUntilIdle();

  EXPECT_TRUE(called);
}

TEST_F(ActivityDispatcherTest, SecondWatchHangsWithoutUpdate) {
  fuchsia::media::ActivityReporterPtr activity_reporter = GetClient();

  bool first_called = false;
  activity_reporter->WatchRenderActivity(
      [&](const RenderUsageVector& activity) { first_called = true; });
  RunLoopUntilIdle();
  EXPECT_TRUE(first_called);

  // Check that the Watch does not return without an update in the activity.
  bool second_called = false;
  activity_reporter->WatchRenderActivity(
      [&](const RenderUsageVector& activity) { second_called = true; });
  RunLoopUntilIdle();
  EXPECT_FALSE(second_called);
}

TEST_F(ActivityDispatcherTest, SecondWatchReturnsWithUpdate) {
  fuchsia::media::ActivityReporterPtr activity_reporter = GetClient();

  bool first_called = false;
  activity_reporter->WatchRenderActivity(
      [&](const RenderUsageVector& activity) { first_called = true; });
  RunLoopUntilIdle();
  EXPECT_TRUE(first_called);

  bool second_called = false;
  RenderUsageVector actual_usages;
  activity_reporter->WatchRenderActivity([&](const RenderUsageVector& usages) {
    second_called = true;
    actual_usages = usages;
  });
  RunLoopUntilIdle();
  EXPECT_FALSE(second_called);

  RenderUsageVector expected_usages = {fuchsia::media::AudioRenderUsage::BACKGROUND};
  UpdateActivity(expected_usages);

  // Check that the Watch does return with an update in the activity.
  RunLoopUntilIdle();
  EXPECT_TRUE(second_called);
  EXPECT_EQ(expected_usages, actual_usages);
}

TEST_F(ActivityDispatcherTest, WatchReturnsCachedValue) {
  RenderUsageVector expected_usages = {fuchsia::media::AudioRenderUsage::BACKGROUND};
  UpdateActivity(expected_usages);
  RunLoopUntilIdle();

  fuchsia::media::ActivityReporterPtr activity_reporter = GetClient();

  bool called = false;
  RenderUsageVector actual_usages;
  activity_reporter->WatchRenderActivity([&](const RenderUsageVector& usages) {
    called = true;
    actual_usages = usages;
  });
  RunLoopUntilIdle();

  EXPECT_TRUE(called);
  EXPECT_EQ(expected_usages, actual_usages);
}

TEST_F(ActivityDispatcherTest, WatchSkipsTransientValue) {
  fuchsia::media::ActivityReporterPtr activity_reporter = GetClient();

  bool first_called = false;
  activity_reporter->WatchRenderActivity(
      [&](const RenderUsageVector& activity) { first_called = true; });
  RunLoopUntilIdle();
  EXPECT_TRUE(first_called);

  RenderUsageVector transient_usages = {fuchsia::media::AudioRenderUsage::BACKGROUND};
  UpdateActivity(transient_usages);
  RunLoopUntilIdle();

  RenderUsageVector expected_usages = {fuchsia::media::AudioRenderUsage::BACKGROUND,
                                       fuchsia::media::AudioRenderUsage::SYSTEM_AGENT};
  UpdateActivity(expected_usages);
  RunLoopUntilIdle();

  // Check that the Watch returns the latest value and not the transient one.
  bool second_called = false;
  RenderUsageVector actual_usages;
  activity_reporter->WatchRenderActivity([&](const RenderUsageVector& usages) {
    second_called = true;
    actual_usages = usages;
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(second_called);
  EXPECT_EQ(expected_usages, actual_usages);
}

TEST_F(ActivityDispatcherTest, WatchHangsAfterFlap) {
  fuchsia::media::ActivityReporterPtr activity_reporter = GetClient();

  bool first_called = false;
  activity_reporter->WatchRenderActivity(
      [&](const RenderUsageVector& activity) { first_called = true; });
  RunLoopUntilIdle();
  EXPECT_TRUE(first_called);

  RenderUsageVector transient_usages = {fuchsia::media::AudioRenderUsage::BACKGROUND};
  UpdateActivity(transient_usages);
  RunLoopUntilIdle();

  RenderUsageVector original_usages = {};
  UpdateActivity(original_usages);
  RunLoopUntilIdle();

  // Check that the Watch does not return if original activity is restored before next Watch.
  bool second_called = false;
  activity_reporter->WatchRenderActivity(
      [&](const RenderUsageVector& usages) { second_called = true; });
  RunLoopUntilIdle();
  EXPECT_FALSE(second_called);
}

TEST_F(ActivityDispatcherTest, WatchHangsOnRedundantChange) {
  fuchsia::media::ActivityReporterPtr activity_reporter = GetClient();

  bool first_called = false;
  activity_reporter->WatchRenderActivity(
      [&](const RenderUsageVector& activity) { first_called = true; });
  RunLoopUntilIdle();
  EXPECT_TRUE(first_called);

  RenderUsageVector redundant_usages = {};
  UpdateActivity(redundant_usages);
  RunLoopUntilIdle();

  // Check that redundant changes are not dispatched.
  bool second_called = false;
  RenderUsageVector actual_usages;
  activity_reporter->WatchRenderActivity(
      [&](const RenderUsageVector& usages) { second_called = true; });
  RunLoopUntilIdle();
  EXPECT_FALSE(second_called);
}

TEST_F(ActivityDispatcherTest, MultipleClients) {
  fuchsia::media::ActivityReporterPtr client = GetClient();

  // First client gets first activity.
  bool first_called = false;
  client->WatchRenderActivity([&](const RenderUsageVector& activity) { first_called = true; });
  RunLoopUntilIdle();
  EXPECT_TRUE(first_called);

  RenderUsageVector expected_usages = {fuchsia::media::AudioRenderUsage::BACKGROUND};
  UpdateActivity(expected_usages);
  RunLoopUntilIdle();

  // First client gets second activity.
  bool second_called = false;
  RenderUsageVector actual_usages;
  client->WatchRenderActivity([&](const RenderUsageVector& usages) {
    second_called = true;
    actual_usages = usages;
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(second_called);
  EXPECT_EQ(expected_usages, actual_usages);

  fuchsia::media::ActivityReporterPtr other_client = GetClient();

  // Second client gets second activty.
  bool third_called = false;
  RenderUsageVector other_actual_usages;
  other_client->WatchRenderActivity([&](const RenderUsageVector& usages) {
    third_called = true;
    other_actual_usages = usages;
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(third_called);
  EXPECT_EQ(expected_usages, other_actual_usages);

  // Both client get the activity update.
  bool first_client_called = false;
  bool second_client_called = false;
  RenderUsageVector first_client_new_actual_usages;
  RenderUsageVector second_client_new_actual_usages;
  client->WatchRenderActivity([&](const RenderUsageVector& usages) {
    first_client_called = true;
    first_client_new_actual_usages = usages;
  });
  other_client->WatchRenderActivity([&](const RenderUsageVector& usages) {
    second_client_called = true;
    second_client_new_actual_usages = usages;
  });
  RunLoopUntilIdle();

  RenderUsageVector new_expected_usages = {fuchsia::media::AudioRenderUsage::BACKGROUND,
                                           fuchsia::media::AudioRenderUsage::SYSTEM_AGENT};
  UpdateActivity(new_expected_usages);
  RunLoopUntilIdle();

  EXPECT_TRUE(first_client_called);
  EXPECT_TRUE(second_client_called);
  EXPECT_EQ(new_expected_usages, first_client_new_actual_usages);
  EXPECT_EQ(new_expected_usages, second_client_new_actual_usages);
}

TEST_F(ActivityDispatcherTest, TwoHangingGetsTriggerError) {
  fuchsia::media::ActivityReporterPtr activity_reporter = GetClient();

  bool client_error_handler_invoked = false;
  zx_status_t client_error_handler_status = ZX_OK;
  activity_reporter.set_error_handler([&](zx_status_t status) {
    client_error_handler_status = status;
    client_error_handler_invoked = true;
  });

  bool called = false;
  activity_reporter->WatchRenderActivity([&](const RenderUsageVector& activity) { called = true; });
  RunLoopUntilIdle();

  EXPECT_TRUE(called);

  activity_reporter->WatchRenderActivity([&](const RenderUsageVector& activity) {});
  activity_reporter->WatchRenderActivity([&](const RenderUsageVector& activity) {});
  RunLoopUntilIdle();

  ASSERT_TRUE(client_error_handler_invoked);
  EXPECT_EQ(client_error_handler_status, ZX_ERR_PEER_CLOSED);
}
}  // namespace media::audio

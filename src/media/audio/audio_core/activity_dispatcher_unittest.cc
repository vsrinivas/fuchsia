// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/activity_dispatcher.h"

#include <lib/fidl/cpp/binding.h>
#include <lib/gtest/test_loop_fixture.h>

#include <gtest/gtest.h>

namespace media::audio {

using RenderActivity = ActivityDispatcherImpl::RenderActivity;
using CaptureActivity = ActivityDispatcherImpl::CaptureActivity;
using RenderUsageVector = std::vector<fuchsia::media::AudioRenderUsage>;
using CaptureUsageVector = std::vector<fuchsia::media::AudioCaptureUsage>;

namespace {
RenderActivity UsageVectorToActivity(
    const std::vector<fuchsia::media::AudioRenderUsage>& usage_vector) {
  RenderActivity activity;
  for (const auto& usage : usage_vector) {
    activity.set(static_cast<int>(usage));
  }
  return activity;
}

CaptureActivity UsageVectorToActivity(
    const std::vector<fuchsia::media::AudioCaptureUsage>& usage_vector) {
  CaptureActivity activity;
  for (const auto& usage : usage_vector) {
    activity.set(static_cast<int>(usage));
  }
  return activity;
}
}  // namespace

typedef ::testing::Types<RenderUsageVector, CaptureUsageVector> UsageVectorTypes;
TYPED_TEST_SUITE(ActivityDispatcherTest, UsageVectorTypes);

template <typename Type>
class ActivityDispatcherTest : public gtest::TestLoopFixture {
 protected:
  fuchsia::media::ActivityReporterPtr GetClient() {
    fuchsia::media::ActivityReporterPtr activity_reporter;
    activity_dispatcher_.GetFidlRequestHandler()(activity_reporter.NewRequest());
    return activity_reporter;
  }

  void SetErrorHandler(fuchsia::media::ActivityReporterPtr& reporter,
                       std::function<void(zx_status_t status)> err) {
    reporter.set_error_handler(err);
  }

  void WatchActivity(fuchsia::media::ActivityReporterPtr& reporter,
                     std::function<void(const Type& v)> cb) {
    WatchActivity<Type>(reporter, cb);
  }

  template <typename UsageVector>
  void WatchActivity(fuchsia::media::ActivityReporterPtr& reporter,
                     std::function<void(const UsageVector& v)> cb);

  template <>
  void WatchActivity<RenderUsageVector>(fuchsia::media::ActivityReporterPtr& reporter,
                                        std::function<void(const RenderUsageVector& v)> cb) {
    reporter->WatchRenderActivity(cb);
  }

  template <>
  void WatchActivity<CaptureUsageVector>(fuchsia::media::ActivityReporterPtr& reporter,
                                         std::function<void(const CaptureUsageVector& v)> cb) {
    reporter->WatchCaptureActivity(cb);
  }

  // Simulates a new set of usages being active.
  void UpdateActivity(const Type& usage_vector) { UpdateActivity<Type>(usage_vector); }

  template <typename UsageVector>
  void UpdateActivity(const UsageVector& usage_vector);

  template <>
  void UpdateActivity<RenderUsageVector>(const RenderUsageVector& usage_vector) {
    activity_dispatcher_.OnRenderActivityChanged(UsageVectorToActivity(usage_vector));
  }

  template <>
  void UpdateActivity<CaptureUsageVector>(const CaptureUsageVector& usage_vector) {
    activity_dispatcher_.OnCaptureActivityChanged(UsageVectorToActivity(usage_vector));
  }

  // Methods to produce templatized usage vectors.
  Type EmptyVector() { return {}; }

  Type SingleVector() { return SingleVector<Type>(); }

  template <typename UsageVector>
  UsageVector SingleVector();

  template <>
  RenderUsageVector SingleVector<RenderUsageVector>() {
    return {fuchsia::media::AudioRenderUsage::BACKGROUND};
  }
  template <>
  CaptureUsageVector SingleVector<CaptureUsageVector>() {
    return {fuchsia::media::AudioCaptureUsage::BACKGROUND};
  }

  Type MultiVector() { return MultiVector<Type>(); }

  template <typename UsageVector>
  UsageVector MultiVector();

  template <>
  RenderUsageVector MultiVector<RenderUsageVector>() {
    return {fuchsia::media::AudioRenderUsage::BACKGROUND,
            fuchsia::media::AudioRenderUsage::SYSTEM_AGENT};
  }
  template <>
  CaptureUsageVector MultiVector<CaptureUsageVector>() {
    return {fuchsia::media::AudioCaptureUsage::BACKGROUND,
            fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT};
  }

 private:
  ActivityDispatcherImpl activity_dispatcher_;
};

TYPED_TEST(ActivityDispatcherTest, FirstWatchReturnsImmediately) {
  fuchsia::media::ActivityReporterPtr activity_reporter = this->GetClient();

  bool called = false;
  this->WatchActivity(activity_reporter, [&](auto v) { called = true; });
  this->RunLoopUntilIdle();
  EXPECT_TRUE(called);
}

TYPED_TEST(ActivityDispatcherTest, SecondWatchHangsWithoutUpdate) {
  fuchsia::media::ActivityReporterPtr activity_reporter = this->GetClient();

  bool first_called = false;
  this->WatchActivity(activity_reporter, [&](auto v) { first_called = true; });
  this->RunLoopUntilIdle();
  EXPECT_TRUE(first_called);

  // Check that the Watch does not return without an update in the activity.
  bool second_called = false;
  this->WatchActivity(activity_reporter, [&](auto v) { second_called = true; });
  this->RunLoopUntilIdle();
  EXPECT_FALSE(second_called);
}

TYPED_TEST(ActivityDispatcherTest, SecondWatchReturnsWithUpdate) {
  fuchsia::media::ActivityReporterPtr activity_reporter = this->GetClient();

  bool first_called = false;
  this->WatchActivity(activity_reporter, [&](auto activity) { first_called = true; });
  this->RunLoopUntilIdle();
  EXPECT_TRUE(first_called);

  bool second_called = false;
  auto actual_usages = this->EmptyVector();
  this->WatchActivity(activity_reporter, [&](auto usages) {
    second_called = true;
    actual_usages = usages;
  });
  this->RunLoopUntilIdle();
  EXPECT_FALSE(second_called);

  auto expected_usages = this->SingleVector();
  this->UpdateActivity(expected_usages);

  // Check that the Watch does return with an update in the activity.
  this->RunLoopUntilIdle();
  EXPECT_TRUE(second_called);
  EXPECT_EQ(expected_usages, actual_usages);
}

TYPED_TEST(ActivityDispatcherTest, WatchReturnsCachedValue) {
  fuchsia::media::ActivityReporterPtr activity_reporter = this->GetClient();

  auto expected_usages = this->SingleVector();
  this->UpdateActivity(expected_usages);
  this->RunLoopUntilIdle();

  bool called = false;
  auto actual_usages = this->EmptyVector();
  this->WatchActivity(activity_reporter, [&](auto usages) {
    called = true;
    actual_usages = usages;
  });
  this->RunLoopUntilIdle();

  EXPECT_TRUE(called);
  EXPECT_EQ(expected_usages, actual_usages);
}

TYPED_TEST(ActivityDispatcherTest, WatchSkipsTransientValue) {
  fuchsia::media::ActivityReporterPtr activity_reporter = this->GetClient();

  bool first_called = false;
  this->WatchActivity(activity_reporter, [&](auto activity) { first_called = true; });
  this->RunLoopUntilIdle();
  EXPECT_TRUE(first_called);

  auto transient_usages = this->SingleVector();
  this->UpdateActivity(transient_usages);
  this->RunLoopUntilIdle();

  auto expected_usages = this->MultiVector();
  this->UpdateActivity(expected_usages);
  this->RunLoopUntilIdle();

  // Check that the Watch returns the latest value and not the transient one.
  bool second_called = false;
  auto actual_usages = this->EmptyVector();
  this->WatchActivity(activity_reporter, [&](auto usages) {
    second_called = true;
    actual_usages = usages;
  });
  this->RunLoopUntilIdle();
  EXPECT_TRUE(second_called);
  EXPECT_EQ(expected_usages, actual_usages);
}

TYPED_TEST(ActivityDispatcherTest, WatchHangsAfterFlap) {
  fuchsia::media::ActivityReporterPtr activity_reporter = this->GetClient();

  bool first_called = false;
  this->WatchActivity(activity_reporter, [&](auto activity) { first_called = true; });
  this->RunLoopUntilIdle();
  EXPECT_TRUE(first_called);

  auto transient_usages = this->SingleVector();
  this->UpdateActivity(transient_usages);
  this->RunLoopUntilIdle();

  auto original_usages = this->EmptyVector();
  this->UpdateActivity(original_usages);
  this->RunLoopUntilIdle();

  // Check that the Watch does not return if original activity is restored before next Watch.
  bool second_called = false;
  this->WatchActivity(activity_reporter, [&](auto usages) { second_called = true; });
  this->RunLoopUntilIdle();
  EXPECT_FALSE(second_called);
}

TYPED_TEST(ActivityDispatcherTest, WatchHangsOnRedundantChange) {
  fuchsia::media::ActivityReporterPtr activity_reporter = this->GetClient();

  bool first_called = false;
  this->WatchActivity(activity_reporter, [&](auto activity) { first_called = true; });
  this->RunLoopUntilIdle();
  EXPECT_TRUE(first_called);

  auto redundant_usages = this->EmptyVector();
  this->UpdateActivity(redundant_usages);
  this->RunLoopUntilIdle();

  // Check that redundant changes are not dispatched.
  bool second_called = false;
  this->WatchActivity(activity_reporter, [&](auto usages) { second_called = true; });
  this->RunLoopUntilIdle();
  EXPECT_FALSE(second_called);
}

TYPED_TEST(ActivityDispatcherTest, MultipleClients) {
  fuchsia::media::ActivityReporterPtr client = this->GetClient();

  // First client gets first activity.
  bool first_called = false;
  this->WatchActivity(client, [&](auto activity) { first_called = true; });
  this->RunLoopUntilIdle();
  EXPECT_TRUE(first_called);

  auto expected_usages = this->SingleVector();
  this->UpdateActivity(expected_usages);
  this->RunLoopUntilIdle();

  // First client gets second activity.
  bool second_called = false;
  auto actual_usages = this->EmptyVector();
  this->WatchActivity(client, [&](auto usages) {
    second_called = true;
    actual_usages = usages;
  });
  this->RunLoopUntilIdle();
  EXPECT_TRUE(second_called);
  EXPECT_EQ(expected_usages, actual_usages);

  fuchsia::media::ActivityReporterPtr second_client = this->GetClient();

  // Second client gets second activty.
  bool third_called = false;
  auto other_actual_usages = this->EmptyVector();
  this->WatchActivity(second_client, [&](auto usages) {
    third_called = true;
    other_actual_usages = usages;
  });
  this->RunLoopUntilIdle();
  EXPECT_TRUE(third_called);
  EXPECT_EQ(expected_usages, other_actual_usages);

  // Both clients get the activity update.
  bool first_client_called = false;
  bool second_client_called = false;
  auto first_client_new_actual_usages = this->EmptyVector();
  auto second_client_new_actual_usages = this->EmptyVector();
  this->WatchActivity(client, [&](auto usages) {
    first_client_called = true;
    first_client_new_actual_usages = usages;
  });
  this->WatchActivity(second_client, [&](auto usages) {
    second_client_called = true;
    second_client_new_actual_usages = usages;
  });
  this->RunLoopUntilIdle();

  auto new_expected_usages = this->MultiVector();
  this->UpdateActivity(new_expected_usages);
  this->RunLoopUntilIdle();

  EXPECT_TRUE(first_client_called);
  EXPECT_TRUE(second_client_called);
  EXPECT_EQ(new_expected_usages, first_client_new_actual_usages);
  EXPECT_EQ(new_expected_usages, second_client_new_actual_usages);
}

TYPED_TEST(ActivityDispatcherTest, TwoHangingGetsTriggerError) {
  fuchsia::media::ActivityReporterPtr activity_reporter = this->GetClient();

  bool client_error_handler_invoked = false;
  zx_status_t client_error_handler_status = ZX_OK;
  this->SetErrorHandler(activity_reporter, [&](zx_status_t status) {
    client_error_handler_status = status;
    client_error_handler_invoked = true;
  });

  bool called = false;
  this->WatchActivity(activity_reporter, [&](auto activity) { called = true; });
  this->RunLoopUntilIdle();

  EXPECT_TRUE(called);

  this->WatchActivity(activity_reporter, [&](auto activity) {});
  this->WatchActivity(activity_reporter, [&](auto activity) {});
  this->RunLoopUntilIdle();

  ASSERT_TRUE(client_error_handler_invoked);
  EXPECT_EQ(client_error_handler_status, ZX_ERR_PEER_CLOSED);
}

}  // namespace media::audio

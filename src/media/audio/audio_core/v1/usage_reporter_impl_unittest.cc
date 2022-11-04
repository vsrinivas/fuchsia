// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/usage_reporter_impl.h"

#include <lib/fidl/cpp/binding.h>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/media/audio/audio_core/v1/usage_settings.h"

namespace media::audio {

const auto kMediaUsage =
    fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA);
const auto kMutedState = fuchsia::media::UsageState::WithMuted({});
const auto kUnadjustedState = fuchsia::media::UsageState::WithUnadjusted({});
const auto kActivateCallback = true;
const auto kDeactivateCallback = false;

class FakeUsageWatcher : public fuchsia::media::UsageWatcher {
 public:
  explicit FakeUsageWatcher(bool activate_callback) : activate_callback_(activate_callback) {}

  const fuchsia::media::Usage& last_usage() const { return last_usage_; }

  const fuchsia::media::UsageState& last_usage_state() const { return last_usage_state_; }

 private:
  void OnStateChanged(fuchsia::media::Usage usage, fuchsia::media::UsageState usage_state,
                      OnStateChangedCallback callback) override {
    last_usage_ = std::move(usage);
    last_usage_state_ = std::move(usage_state);

    if (activate_callback_) {
      callback();
    }
  }

  bool activate_callback_;
  fuchsia::media::Usage last_usage_;
  fuchsia::media::UsageState last_usage_state_;
};

class UsageReporterImplTest : public gtest::TestLoopFixture {
 protected:
  fidl::Binding<fuchsia::media::UsageWatcher, std::unique_ptr<FakeUsageWatcher>> Watch(
      fuchsia::media::Usage usage, bool activate_callback) {
    fidl::InterfaceHandle<fuchsia::media::UsageWatcher> state_watcher_handle;
    auto request = state_watcher_handle.NewRequest();
    usage_reporter_->Watch(std::move(usage), std::move(state_watcher_handle));
    return fidl::Binding(std::make_unique<FakeUsageWatcher>(activate_callback), std::move(request));
  }

  bool AcksComplete(const fuchsia::media::Usage& usage) {
    auto& set = usage_reporter_impl_.watcher_set(usage);
    for (auto& watcher : set.watchers) {
      if (watcher.second.outstanding_ack_count != 0) {
        return false;
      }
    }
    return true;
  }

  bool WatchersDisconnected(const fuchsia::media::Usage& usage) {
    auto& set = usage_reporter_impl_.watcher_set(usage);
    return set.watchers.empty();
  }

  UsageReporterImpl usage_reporter_impl_;
  AudioAdmin::PolicyActionReporter* policy_action_reporter_ = &usage_reporter_impl_;
  fuchsia::media::UsageReporter* usage_reporter_ = &usage_reporter_impl_;
  const int kMaxStates = UsageReporterImpl::MAX_STATES;
};

TEST_F(UsageReporterImplTest, StateIsEmittedToWatcher) {
  auto watcher = Watch(fidl::Clone(kMediaUsage), kActivateCallback);

  policy_action_reporter_->ReportPolicyAction(fidl::Clone(kMediaUsage),
                                              fuchsia::media::Behavior::MUTE);

  RunLoopUntilIdle();
  EXPECT_TRUE(fidl::Equals(watcher.impl()->last_usage(), kMediaUsage));
  EXPECT_TRUE(fidl::Equals(watcher.impl()->last_usage_state(), kMutedState));
  EXPECT_TRUE(AcksComplete(kMediaUsage));
}

TEST_F(UsageReporterImplTest, StatesAreEmittedToWatcher) {
  auto watcher = Watch(fidl::Clone(kMediaUsage), kActivateCallback);

  policy_action_reporter_->ReportPolicyAction(fidl::Clone(kMediaUsage),
                                              fuchsia::media::Behavior::MUTE);
  policy_action_reporter_->ReportPolicyAction(fidl::Clone(kMediaUsage),
                                              fuchsia::media::Behavior::MUTE);

  RunLoopUntilIdle();
  EXPECT_TRUE(fidl::Equals(watcher.impl()->last_usage(), kMediaUsage));
  EXPECT_TRUE(fidl::Equals(watcher.impl()->last_usage_state(), kMutedState));
  EXPECT_TRUE(AcksComplete(kMediaUsage));
}

TEST_F(UsageReporterImplTest, ErrorHandlerDisconnectsWatcher) {
  // Watcher dropped after block scope to trigger error handler.
  {
    auto watcher = Watch(fidl::Clone(kMediaUsage), kDeactivateCallback);

    policy_action_reporter_->ReportPolicyAction(fidl::Clone(kMediaUsage),
                                                fuchsia::media::Behavior::MUTE);

    EXPECT_FALSE(WatchersDisconnected(kMediaUsage));
  }
  RunLoopUntilIdle();
  EXPECT_TRUE(WatchersDisconnected(kMediaUsage));
}

TEST_F(UsageReporterImplTest, StateIsEmittedToAllWatchers) {
  auto watcher1 = Watch(fidl::Clone(kMediaUsage), kActivateCallback);
  auto watcher2 = Watch(fidl::Clone(kMediaUsage), kActivateCallback);

  policy_action_reporter_->ReportPolicyAction(fidl::Clone(kMediaUsage),
                                              fuchsia::media::Behavior::MUTE);

  RunLoopUntilIdle();
  EXPECT_TRUE(fidl::Equals(watcher1.impl()->last_usage(), kMediaUsage));
  EXPECT_TRUE(fidl::Equals(watcher1.impl()->last_usage_state(), kMutedState));
  EXPECT_TRUE(fidl::Equals(watcher2.impl()->last_usage(), kMediaUsage));
  EXPECT_TRUE(fidl::Equals(watcher2.impl()->last_usage_state(), kMutedState));
  EXPECT_TRUE(AcksComplete(kMediaUsage));
}

TEST_F(UsageReporterImplTest, StatesAreEmittedToAllWatchers) {
  auto watcher1 = Watch(fidl::Clone(kMediaUsage), kActivateCallback);
  auto watcher2 = Watch(fidl::Clone(kMediaUsage), kActivateCallback);

  policy_action_reporter_->ReportPolicyAction(fidl::Clone(kMediaUsage),
                                              fuchsia::media::Behavior::MUTE);
  policy_action_reporter_->ReportPolicyAction(fidl::Clone(kMediaUsage),
                                              fuchsia::media::Behavior::MUTE);

  RunLoopUntilIdle();
  EXPECT_TRUE(fidl::Equals(watcher1.impl()->last_usage(), kMediaUsage));
  EXPECT_TRUE(fidl::Equals(watcher1.impl()->last_usage_state(), kMutedState));
  EXPECT_TRUE(fidl::Equals(watcher2.impl()->last_usage(), kMediaUsage));
  EXPECT_TRUE(fidl::Equals(watcher2.impl()->last_usage_state(), kMutedState));
  EXPECT_TRUE(AcksComplete(kMediaUsage));
}

TEST_F(UsageReporterImplTest, WatchersThatDontReplyAreDisconnected) {
  auto watcher1 = Watch(fidl::Clone(kMediaUsage), kDeactivateCallback);
  auto watcher2 = Watch(fidl::Clone(kMediaUsage), kDeactivateCallback);

  // Report up to kMaxStates and allow watchers to ack if enabled.
  for (int i = 0; i < kMaxStates; ++i) {
    policy_action_reporter_->ReportPolicyAction(fidl::Clone(kMediaUsage),
                                                fuchsia::media::Behavior::MUTE);
  }
  RunLoopUntilIdle();
  EXPECT_FALSE(WatchersDisconnected(kMediaUsage));

  // Report additional state to reach kMaxStates and cause disconnect of un-acking watchers.
  policy_action_reporter_->ReportPolicyAction(fidl::Clone(kMediaUsage),
                                              fuchsia::media::Behavior::MUTE);
  RunLoopUntilIdle();
  EXPECT_TRUE(WatchersDisconnected(kMediaUsage));
}

TEST_F(UsageReporterImplTest, WatchersReceiveCachedState) {
  // The watcher should receive the current state on connection.
  auto watcher1 = Watch(fidl::Clone(kMediaUsage), kActivateCallback);
  RunLoopUntilIdle();
  EXPECT_TRUE(fidl::Equals(watcher1.impl()->last_usage(), kMediaUsage));
  EXPECT_TRUE(fidl::Equals(watcher1.impl()->last_usage_state(), kUnadjustedState));

  policy_action_reporter_->ReportPolicyAction(fidl::Clone(kMediaUsage),
                                              fuchsia::media::Behavior::MUTE);

  // The new watcher should receive the current state when it connects, and that state should be
  // updated by the policy action.
  auto watcher2 = Watch(fidl::Clone(kMediaUsage), kActivateCallback);
  RunLoopUntilIdle();
  EXPECT_TRUE(fidl::Equals(watcher2.impl()->last_usage(), kMediaUsage));
  EXPECT_TRUE(fidl::Equals(watcher2.impl()->last_usage_state(), kMutedState));
  EXPECT_TRUE(AcksComplete(kMediaUsage));
}

}  // namespace media::audio

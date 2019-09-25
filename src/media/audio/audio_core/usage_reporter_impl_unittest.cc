// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/usage_reporter_impl.h"

#include <lib/fidl/cpp/binding.h>
#include <lib/gtest/test_loop_fixture.h>

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/usage_settings.h"

namespace media::audio {
namespace {

const auto kMediaUsage = UsageFrom(fuchsia::media::AudioRenderUsage::MEDIA);
const auto kMutedState = fuchsia::media::UsageState::WithMuted({});
const auto kUnadjustedState = fuchsia::media::UsageState::WithUnadjusted({});

class FakeUsageWatcher : public fuchsia::media::UsageWatcher {
 public:
  const fuchsia::media::Usage& last_usage() const { return last_usage_; }

  const fuchsia::media::UsageState& last_usage_state() const { return last_usage_state_; }

 private:
  void OnStateChanged(fuchsia::media::Usage usage, fuchsia::media::UsageState usage_state,
                      OnStateChangedCallback callback) override {
    last_usage_ = std::move(usage);
    last_usage_state_ = std::move(usage_state);
    callback();
  }

  fuchsia::media::Usage last_usage_;
  fuchsia::media::UsageState last_usage_state_;
};

class UsageReporterImplTest : public gtest::TestLoopFixture {
 protected:
  fidl::Binding<fuchsia::media::UsageWatcher, std::unique_ptr<FakeUsageWatcher>> Watch(
      fuchsia::media::Usage usage) {
    fidl::InterfaceHandle<fuchsia::media::UsageWatcher> state_watcher_handle;
    auto request = state_watcher_handle.NewRequest();
    usage_reporter_->Watch(std::move(usage), std::move(state_watcher_handle));
    return fidl::Binding(std::make_unique<FakeUsageWatcher>(), std::move(request));
  }

  UsageReporterImpl usage_reporter_impl_;
  AudioAdmin::PolicyActionReporter* policy_action_reporter_ = &usage_reporter_impl_;
  fuchsia::media::UsageReporter* usage_reporter_ = &usage_reporter_impl_;
};

TEST_F(UsageReporterImplTest, StatesAreEmittedToWatchers) {
  auto watcher = Watch(fidl::Clone(kMediaUsage));

  policy_action_reporter_->ReportPolicyAction(fidl::Clone(kMediaUsage),
                                              fuchsia::media::Behavior::MUTE);

  RunLoopUntilIdle();
  EXPECT_TRUE(fidl::Equals(watcher.impl()->last_usage(), kMediaUsage));
  EXPECT_TRUE(fidl::Equals(watcher.impl()->last_usage_state(), kMutedState));
}

TEST_F(UsageReporterImplTest, StatesAreEmittedToAllWatchers) {
  auto watcher1 = Watch(fidl::Clone(kMediaUsage));
  auto watcher2 = Watch(fidl::Clone(kMediaUsage));

  policy_action_reporter_->ReportPolicyAction(fidl::Clone(kMediaUsage),
                                              fuchsia::media::Behavior::MUTE);

  RunLoopUntilIdle();
  EXPECT_TRUE(fidl::Equals(watcher1.impl()->last_usage(), kMediaUsage));
  EXPECT_TRUE(fidl::Equals(watcher1.impl()->last_usage_state(), kMutedState));
  EXPECT_TRUE(fidl::Equals(watcher2.impl()->last_usage(), kMediaUsage));
  EXPECT_TRUE(fidl::Equals(watcher2.impl()->last_usage_state(), kMutedState));
}

TEST_F(UsageReporterImplTest, WatchersReceiveCachedState) {
  // The watcher should receive the current state on connection.
  auto watcher1 = Watch(fidl::Clone(kMediaUsage));
  RunLoopUntilIdle();
  EXPECT_TRUE(fidl::Equals(watcher1.impl()->last_usage(), kMediaUsage));
  EXPECT_TRUE(fidl::Equals(watcher1.impl()->last_usage_state(), kUnadjustedState));

  policy_action_reporter_->ReportPolicyAction(fidl::Clone(kMediaUsage),
                                              fuchsia::media::Behavior::MUTE);

  // The new watcher should receive the current state when it connects, and that state should be
  // updated by the policy action.
  auto watcher2 = Watch(fidl::Clone(kMediaUsage));
  RunLoopUntilIdle();
  EXPECT_TRUE(fidl::Equals(watcher2.impl()->last_usage(), kMediaUsage));
  EXPECT_TRUE(fidl::Equals(watcher2.impl()->last_usage_state(), kMutedState));
}

}  // namespace
}  // namespace media::audio

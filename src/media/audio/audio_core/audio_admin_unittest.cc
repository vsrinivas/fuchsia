// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_admin.h"

#include <lib/gtest/test_loop_fixture.h>

#include <unordered_map>

#include "src/media/audio/lib/test/null_audio_capturer.h"
#include "src/media/audio/lib/test/null_audio_renderer.h"

namespace media::audio {
namespace {
// Note we purposely use some strange values here to ensure we're not falling back to any default
// or hard-coded logic for values.
static constexpr float kMuteGain = -3.0f;
static constexpr float kDuckGain = -2.0f;
static constexpr float kNoneGain = -1.0f;

static constexpr AudioAdmin::BehaviorGain kTestBehaviorGain{
    .none_gain_db = kNoneGain,
    .duck_gain_db = kDuckGain,
    .mute_gain_db = kMuteGain,
};

class MockPolicyActionReporter : public AudioAdmin::PolicyActionReporter {
 public:
  MockPolicyActionReporter(
      fit::function<void(fuchsia::media::Usage usage, fuchsia::media::Behavior policy_action)>
          receiver)
      : receiver_(std::move(receiver)) {}

  void ReportPolicyAction(fuchsia::media::Usage usage,
                          fuchsia::media::Behavior policy_action) override {
    receiver_(std::move(usage), policy_action);
  }

 private:
  fit::function<void(fuchsia::media::Usage usage, fuchsia::media::Behavior policy_action)>
      receiver_;
};

class MockActivityDispatcher : public AudioAdmin::ActivityDispatcher {
 public:
  void OnRenderActivityChanged(std::bitset<fuchsia::media::RENDER_USAGE_COUNT> activity) override {
    last_dispatched_render_activity_ = activity;
  }
  void OnCaptureActivityChanged(
      std::bitset<fuchsia::media::CAPTURE_USAGE_COUNT> activity) override {
    last_dispatched_capture_activity_ = activity;
  }

  // Access last activity dispatched.
  std::bitset<fuchsia::media::RENDER_USAGE_COUNT> GetLastRenderActivity() {
    return last_dispatched_render_activity_;
  }
  std::bitset<fuchsia::media::CAPTURE_USAGE_COUNT> GetLastCaptureActivity() {
    return last_dispatched_capture_activity_;
  }

 private:
  std::bitset<fuchsia::media::RENDER_USAGE_COUNT> last_dispatched_render_activity_;
  std::bitset<fuchsia::media::CAPTURE_USAGE_COUNT> last_dispatched_capture_activity_;
};

class AudioAdminTest : public gtest::TestLoopFixture {};

TEST_F(AudioAdminTest, TwoRenderersWithNoInteractions) {
  MockPolicyActionReporter policy_action_reporter([](auto _usage, auto _policy_action) {});
  MockActivityDispatcher mock_activity_dispatcher;
  StreamVolumeManager stream_volume_manager(dispatcher());
  AudioAdmin admin(kTestBehaviorGain, &stream_volume_manager, &policy_action_reporter,
                   &mock_activity_dispatcher, dispatcher());
  test::NullAudioRenderer r1, r2;

  // Set an inintial stream volume.
  const float kStreamGain = 1.0;
  stream_volume_manager.SetUsageGain(
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA), kStreamGain);
  stream_volume_manager.SetUsageGain(
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION),
      kStreamGain);

  // Start playing a MEDIA stream and check for 'no gain adjustment'.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::MEDIA, true, &r1);
  RunLoopUntilIdle();
  EXPECT_EQ(kStreamGain + kNoneGain,
            stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
                fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA)));

  // Now play a COMMUNICATIONS stream and also check for 'no gain adjustment'.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::COMMUNICATION, true, &r2);
  RunLoopUntilIdle();
  EXPECT_EQ(kStreamGain + kNoneGain,
            stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
                fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA)));
  EXPECT_EQ(
      kStreamGain + kNoneGain,
      stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
          fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION)));
}

TEST_F(AudioAdminTest, TwoRenderersWithDuck) {
  StreamVolumeManager stream_volume_manager(dispatcher());
  MockPolicyActionReporter policy_action_reporter([](auto _usage, auto _policy_action) {});
  MockActivityDispatcher mock_activity_dispatcher;
  AudioAdmin admin(kTestBehaviorGain, &stream_volume_manager, &policy_action_reporter,
                   &mock_activity_dispatcher, dispatcher());
  test::NullAudioRenderer r1, r2;

  // Media should duck when comms is active.
  admin.SetInteraction(
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION),
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA),
      fuchsia::media::Behavior::DUCK);

  // Set an inintial stream volume.
  const float kStreamGain = 1.0;
  stream_volume_manager.SetUsageGain(
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA), kStreamGain);
  stream_volume_manager.SetUsageGain(
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION),
      kStreamGain);

  // create media active stream.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::MEDIA, true, &r1);
  RunLoopUntilIdle();
  EXPECT_EQ(kStreamGain + kNoneGain,
            stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
                fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA)));

  // communications renderer becomes active; media should duck.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::COMMUNICATION, true, &r2);
  RunLoopUntilIdle();
  EXPECT_EQ(kStreamGain + kDuckGain,
            stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
                fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA)));
  EXPECT_EQ(
      kStreamGain + kNoneGain,
      stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
          fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION)));

  // comms becomes inactive; ducking should stop.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::COMMUNICATION, false, &r2);
  RunLoopUntilIdle();
  EXPECT_EQ(kStreamGain + kNoneGain,
            stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
                fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA)));
  EXPECT_EQ(
      kStreamGain + kNoneGain,
      stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
          fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION)));
}

TEST_F(AudioAdminTest, CapturerDucksRenderer) {
  StreamVolumeManager stream_volume_manager(dispatcher());
  MockPolicyActionReporter policy_action_reporter([](auto _usage, auto _policy_action) {});
  MockActivityDispatcher mock_activity_dispatcher;
  AudioAdmin admin(kTestBehaviorGain, &stream_volume_manager, &policy_action_reporter,
                   &mock_activity_dispatcher, dispatcher());
  test::NullAudioRenderer r1;
  test::NullAudioCapturer c1;

  // Set an inintial stream volume.
  const float kStreamGain = 1.0;
  stream_volume_manager.SetUsageGain(
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA), kStreamGain);
  stream_volume_manager.SetUsageGain(
      fuchsia::media::Usage::WithCaptureUsage(fuchsia::media::AudioCaptureUsage::COMMUNICATION),
      kStreamGain);

  // Media should duck when comms is active.
  admin.SetInteraction(
      fuchsia::media::Usage::WithCaptureUsage(fuchsia::media::AudioCaptureUsage::COMMUNICATION),
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA),
      fuchsia::media::Behavior::DUCK);

  // Create active media stream.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::MEDIA, true, &r1);
  RunLoopUntilIdle();
  EXPECT_EQ(kStreamGain + kNoneGain,
            stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
                fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA)));

  // Create active comms capturer; media output should duck.
  admin.UpdateCapturerState(fuchsia::media::AudioCaptureUsage::COMMUNICATION, true, &c1);
  RunLoopUntilIdle();
  EXPECT_EQ(kStreamGain + kDuckGain,
            stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
                fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA)));
  EXPECT_EQ(kStreamGain + kNoneGain,
            stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
                fuchsia::media::Usage::WithCaptureUsage(
                    fuchsia::media::AudioCaptureUsage::COMMUNICATION)));

  // Comms becomes inactive; ducking should stop.
  admin.UpdateCapturerState(fuchsia::media::AudioCaptureUsage::COMMUNICATION, false, &c1);
  RunLoopUntilIdle();
  EXPECT_EQ(kStreamGain + kNoneGain,
            stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
                fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA)));
  EXPECT_EQ(kStreamGain + kNoneGain,
            stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
                fuchsia::media::Usage::WithCaptureUsage(
                    fuchsia::media::AudioCaptureUsage::COMMUNICATION)));
}

TEST_F(AudioAdminTest, RendererDucksCapturer) {
  StreamVolumeManager stream_volume_manager(dispatcher());
  MockPolicyActionReporter policy_action_reporter([](auto _usage, auto _policy_action) {});
  MockActivityDispatcher mock_activity_dispatcher;
  AudioAdmin admin(kTestBehaviorGain, &stream_volume_manager, &policy_action_reporter,
                   &mock_activity_dispatcher, dispatcher());
  test::NullAudioRenderer r1;
  test::NullAudioCapturer c1;

  const float kStreamGain = 1.0;
  stream_volume_manager.SetUsageGain(
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION),
      kStreamGain);
  stream_volume_manager.SetUsageGain(
      fuchsia::media::Usage::WithCaptureUsage(fuchsia::media::AudioCaptureUsage::FOREGROUND),
      kStreamGain);

  // Foreground capturer should duck when communication renderers are active.
  admin.SetInteraction(
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION),
      fuchsia::media::Usage::WithCaptureUsage(fuchsia::media::AudioCaptureUsage::FOREGROUND),
      fuchsia::media::Behavior::DUCK);

  // Create active capturer stream.
  admin.UpdateCapturerState(fuchsia::media::AudioCaptureUsage::FOREGROUND, true, &c1);
  RunLoopUntilIdle();
  EXPECT_EQ(
      kStreamGain + kNoneGain,
      stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
          fuchsia::media::Usage::WithCaptureUsage(fuchsia::media::AudioCaptureUsage::FOREGROUND)));

  // Create active comms renderer; foreground capturer should duck.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::COMMUNICATION, true, &r1);
  RunLoopUntilIdle();
  EXPECT_EQ(
      kStreamGain + kDuckGain,
      stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
          fuchsia::media::Usage::WithCaptureUsage(fuchsia::media::AudioCaptureUsage::FOREGROUND)));
  EXPECT_EQ(
      kStreamGain + kNoneGain,
      stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
          fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION)));

  // Comms becomes inactive; ducking should stop.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::COMMUNICATION, false, &r1);
  RunLoopUntilIdle();
  EXPECT_EQ(
      kStreamGain + kNoneGain,
      stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
          fuchsia::media::Usage::WithCaptureUsage(fuchsia::media::AudioCaptureUsage::FOREGROUND)));
  EXPECT_EQ(
      kStreamGain + kNoneGain,
      stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
          fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION)));
}

TEST_F(AudioAdminTest, PolicyActionsReported) {
  auto test_policy_action = [this](auto expected_action) {
    const auto target_usage =
        fuchsia::media::Usage::WithCaptureUsage(fuchsia::media::AudioCaptureUsage::FOREGROUND);
    fuchsia::media::Behavior policy_action_taken;
    // Record any actions taken on our target_usage (AudioCaptureUsage::FOREGROUND)
    MockPolicyActionReporter policy_action_reporter(
        [&policy_action_taken, &target_usage](auto usage, auto action) {
          if (fidl::Equals(usage, target_usage)) {
            policy_action_taken = action;
          }
        });

    StreamVolumeManager stream_volume_manager(dispatcher());
    MockActivityDispatcher mock_activity_dispatcher;
    AudioAdmin admin(kTestBehaviorGain, &stream_volume_manager, &policy_action_reporter,
                     &mock_activity_dispatcher, dispatcher());
    test::NullAudioRenderer r1;
    test::NullAudioCapturer c1;

    const float kStreamGain = 1.0;
    stream_volume_manager.SetUsageGain(
        fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION),
        kStreamGain);
    stream_volume_manager.SetUsageGain(
        fuchsia::media::Usage::WithCaptureUsage(fuchsia::media::AudioCaptureUsage::FOREGROUND),
        kStreamGain);

    // Foreground capturer should duck when communication renderers are active.
    admin.SetInteraction(
        fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION),
        fuchsia::media::Usage::WithCaptureUsage(fuchsia::media::AudioCaptureUsage::FOREGROUND),
        expected_action);

    // Create active capturer stream.
    admin.UpdateCapturerState(fuchsia::media::AudioCaptureUsage::FOREGROUND, true, &c1);
    // Create active comms renderer; foreground capturer should receive policy action.
    admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::COMMUNICATION, true, &r1);
    RunLoopUntilIdle();
    EXPECT_EQ(policy_action_taken, expected_action);

    // Comms becomes inactive; action should stop.
    admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::COMMUNICATION, false, &r1);
    RunLoopUntilIdle();
    EXPECT_EQ(policy_action_taken, fuchsia::media::Behavior::NONE);
  };

  test_policy_action(fuchsia::media::Behavior::DUCK);
  test_policy_action(fuchsia::media::Behavior::MUTE);
}

TEST_F(AudioAdminTest, RenderActivityDispatched) {
  // Test that a change of usage given an initial activity is correctly dispatched.
  auto test_dispatch_action = [this](
                                  std::bitset<fuchsia::media::RENDER_USAGE_COUNT> initial_activity,
                                  fuchsia::media::AudioRenderUsage changed_usage) {
    StreamVolumeManager stream_volume_manager(dispatcher());
    MockPolicyActionReporter policy_action_reporter([](auto _usage, auto _policy_action) {});
    MockActivityDispatcher mock_activity_dispatcher;
    AudioAdmin admin(kTestBehaviorGain, &stream_volume_manager, &policy_action_reporter,
                     &mock_activity_dispatcher, dispatcher());

    // Trigger the initial activity by registering AudioRenderers.
    std::array<test::NullAudioRenderer, fuchsia::media::RENDER_USAGE_COUNT> rs;
    for (int i = 0; i < fuchsia::media::RENDER_USAGE_COUNT; i++) {
      if (initial_activity[i]) {
        admin.UpdateRendererState(static_cast<fuchsia::media::AudioRenderUsage>(i), true, &rs[i]);
      }
    }

    RunLoopUntilIdle();
    EXPECT_EQ(initial_activity, mock_activity_dispatcher.GetLastRenderActivity());

    int changed_usage_index = static_cast<int>(changed_usage);
    std::bitset<fuchsia::media::RENDER_USAGE_COUNT> final_activity = initial_activity;
    final_activity.flip(changed_usage_index);

    // Modify the initial activity to reflect the changed usage.
    admin.UpdateRendererState(changed_usage, final_activity[changed_usage_index],
                              &rs[changed_usage_index]);

    RunLoopUntilIdle();
    EXPECT_EQ(final_activity, mock_activity_dispatcher.GetLastRenderActivity());
  };

  // Check all of the possible state transitions from each possible activity.
  int possible_activities_count = std::pow(2, fuchsia::media::RENDER_USAGE_COUNT);
  for (int i = 0; i < possible_activities_count; i++) {
    for (int j = 0; j < fuchsia::media::RENDER_USAGE_COUNT; j++) {
      auto initial_activity = static_cast<std::bitset<fuchsia::media::RENDER_USAGE_COUNT>>(i);
      auto changed_usage = static_cast<fuchsia::media::AudioRenderUsage>(j);
      test_dispatch_action(initial_activity, changed_usage);
    }
  }
}

TEST_F(AudioAdminTest, CaptureActivityDispatched) {
  // Test that a change of usage given an initial activity is correctly dispatched.
  auto test_dispatch_action = [this](
                                  std::bitset<fuchsia::media::CAPTURE_USAGE_COUNT> initial_activity,
                                  fuchsia::media::AudioCaptureUsage changed_usage) {
    StreamVolumeManager stream_volume_manager(dispatcher());
    MockPolicyActionReporter policy_action_reporter([](auto _usage, auto _policy_action) {});
    MockActivityDispatcher mock_activity_dispatcher;
    AudioAdmin admin(kTestBehaviorGain, &stream_volume_manager, &policy_action_reporter,
                     &mock_activity_dispatcher, dispatcher());

    // Trigger the initial activity by registering AudioCapturers.
    std::array<test::NullAudioCapturer, fuchsia::media::CAPTURE_USAGE_COUNT> rs;
    for (int i = 0; i < fuchsia::media::CAPTURE_USAGE_COUNT; i++) {
      if (initial_activity[i]) {
        admin.UpdateCapturerState(static_cast<fuchsia::media::AudioCaptureUsage>(i), true, &rs[i]);
      }
    }

    RunLoopUntilIdle();
    EXPECT_EQ(initial_activity, mock_activity_dispatcher.GetLastCaptureActivity());

    int changed_usage_index = static_cast<int>(changed_usage);
    std::bitset<fuchsia::media::CAPTURE_USAGE_COUNT> final_activity = initial_activity;
    final_activity.flip(changed_usage_index);

    // Modify the initial activity to reflect the changed usage.
    admin.UpdateCapturerState(changed_usage, final_activity[changed_usage_index],
                              &rs[changed_usage_index]);

    RunLoopUntilIdle();
    EXPECT_EQ(final_activity, mock_activity_dispatcher.GetLastCaptureActivity());
  };

  // Check all of the possible state transitions from each possible activity.
  int possible_activities_count = std::pow(2, fuchsia::media::CAPTURE_USAGE_COUNT);
  for (int i = 0; i < possible_activities_count; i++) {
    for (int j = 0; j < fuchsia::media::CAPTURE_USAGE_COUNT; j++) {
      auto initial_activity = static_cast<std::bitset<fuchsia::media::CAPTURE_USAGE_COUNT>>(i);
      auto changed_usage = static_cast<fuchsia::media::AudioCaptureUsage>(j);
      test_dispatch_action(initial_activity, changed_usage);
    }
  }
}

// Test to verify that Mute overrides Duck, and both override None.
TEST_F(AudioAdminTest, PriorityActionsApplied) {
  StreamVolumeManager stream_volume_manager(dispatcher());
  MockPolicyActionReporter policy_action_reporter([](auto _usage, auto _policy_action) {});
  MockActivityDispatcher mock_activity_dispatcher;
  AudioAdmin admin(kTestBehaviorGain, &stream_volume_manager, &policy_action_reporter,
                   &mock_activity_dispatcher, dispatcher());
  test::NullAudioRenderer r1, r2, r3;
  test::NullAudioCapturer c1;

  // Interruption should duck when SystemAgent(render) is active.
  admin.SetInteraction(
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT),
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::INTERRUPTION),
      fuchsia::media::Behavior::DUCK);

  // Communication(render) should duck when SystemAgent(render) is active.
  admin.SetInteraction(
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT),
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION),
      fuchsia::media::Behavior::DUCK);

  // Communication(render) should mute when SystemAgent(capture) is active.
  admin.SetInteraction(
      fuchsia::media::Usage::WithCaptureUsage(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT),
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION),
      fuchsia::media::Behavior::MUTE);

  // Set an initial stream volume.
  const float kStreamGain = 1.0;
  stream_volume_manager.SetUsageGain(
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::INTERRUPTION),
      kStreamGain);
  stream_volume_manager.SetUsageGain(
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION),
      kStreamGain);

  // Create Interruption active stream.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::INTERRUPTION, true, &r1);
  RunLoopUntilIdle();
  EXPECT_EQ(
      kStreamGain + kNoneGain,
      stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
          fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::INTERRUPTION)));

  // Create Communication active stream.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::COMMUNICATION, true, &r2);
  RunLoopUntilIdle();
  EXPECT_EQ(
      kStreamGain + kNoneGain,
      stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
          fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION)));

  // SystemAgent capturer becomes active; Interruption should not change, Communication should mute.
  admin.UpdateCapturerState(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT, true, &c1);
  RunLoopUntilIdle();
  EXPECT_EQ(
      kStreamGain + kNoneGain,
      stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
          fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::INTERRUPTION)));
  EXPECT_EQ(
      kStreamGain + kMuteGain,
      stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
          fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION)));

  // SystemAgent renderer becomes active; Interruption should duck, Communication should remain
  // muted.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT, true, &r3);
  RunLoopUntilIdle();
  EXPECT_EQ(
      kStreamGain + kDuckGain,
      stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
          fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::INTERRUPTION)));
  EXPECT_EQ(
      kStreamGain + kMuteGain,
      stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
          fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION)));
}

// Test to verify that a muted active stream's policy is not applied.
TEST_F(AudioAdminTest, IgnoreMutedActiveStreamPolicy) {
  StreamVolumeManager stream_volume_manager(dispatcher());
  MockPolicyActionReporter policy_action_reporter([](auto _usage, auto _policy_action) {});
  MockActivityDispatcher mock_activity_dispatcher;
  AudioAdmin admin(kTestBehaviorGain, &stream_volume_manager, &policy_action_reporter,
                   &mock_activity_dispatcher, dispatcher());
  test::NullAudioRenderer r1, r2;
  test::NullAudioCapturer c1;

  // Media should duck when Communication(render) is active.
  admin.SetInteraction(
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION),
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA),
      fuchsia::media::Behavior::DUCK);

  // Communication(render) should mute when SystemAgent(capture) is active.
  admin.SetInteraction(
      fuchsia::media::Usage::WithCaptureUsage(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT),
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION),
      fuchsia::media::Behavior::MUTE);

  // Set an initial stream volume.
  const float kStreamGain = 1.0;
  stream_volume_manager.SetUsageGain(
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA), kStreamGain);
  stream_volume_manager.SetUsageGain(
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION),
      kStreamGain);

  // Create Media active stream.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::MEDIA, true, &r1);
  RunLoopUntilIdle();
  EXPECT_EQ(kStreamGain + kNoneGain,
            stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
                fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA)));

  // Communication renderer becomes active; Media should duck.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::COMMUNICATION, true, &r2);
  RunLoopUntilIdle();
  EXPECT_EQ(kStreamGain + kDuckGain,
            stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
                fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA)));

  // SystemAgent capturer becomes active; Communication should mute, so its policy should no longer
  // apply to Media.
  admin.UpdateCapturerState(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT, true, &c1);
  RunLoopUntilIdle();
  EXPECT_EQ(
      kStreamGain + kMuteGain,
      stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
          fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION)));
  EXPECT_EQ(kStreamGain + kNoneGain,
            stream_volume_manager.GetUsageGainSettings().GetAdjustedUsageGain(
                fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA)));
}

}  // namespace
}  // namespace media::audio

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

class AudioAdminTest : public gtest::TestLoopFixture {};

TEST_F(AudioAdminTest, TwoRenderersWithNoInteractions) {
  MockPolicyActionReporter policy_action_reporter([](auto _usage, auto _policy_action) {});
  StreamVolumeManager stream_volume_manager(dispatcher());
  AudioAdmin admin(kTestBehaviorGain, &stream_volume_manager, &policy_action_reporter,
                   dispatcher());
  test::NullAudioRenderer r1, r2;

  // Set an inintial stream volume.
  const float kStreamGain = 1.0;
  stream_volume_manager.SetUsageGain(
      fuchsia::media::Usage::WithRenderUsage(fidl::Clone(fuchsia::media::AudioRenderUsage::MEDIA)),
      kStreamGain);
  stream_volume_manager.SetUsageGain(fuchsia::media::Usage::WithRenderUsage(fidl::Clone(
                                         fuchsia::media::AudioRenderUsage::COMMUNICATION)),
                                     kStreamGain);

  // Start playing a MEDIA stream and check for 'no gain adjustment'.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::MEDIA, true, &r1);
  RunLoopUntilIdle();
  EXPECT_EQ(kStreamGain + kNoneGain, stream_volume_manager.GetUsageGainSettings().GetUsageGain(
                                         fuchsia::media::Usage::WithRenderUsage(fidl::Clone(
                                             fuchsia::media::AudioRenderUsage::MEDIA))));

  // Now play a COMMUNICATIONS stream and also check for 'no gain adjustment'.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::COMMUNICATION, true, &r2);
  RunLoopUntilIdle();
  EXPECT_EQ(kStreamGain + kNoneGain, stream_volume_manager.GetUsageGainSettings().GetUsageGain(
                                         fuchsia::media::Usage::WithRenderUsage(fidl::Clone(
                                             fuchsia::media::AudioRenderUsage::MEDIA))));
  EXPECT_EQ(kStreamGain + kNoneGain, stream_volume_manager.GetUsageGainSettings().GetUsageGain(
                                         fuchsia::media::Usage::WithRenderUsage(fidl::Clone(
                                             fuchsia::media::AudioRenderUsage::COMMUNICATION))));
}

TEST_F(AudioAdminTest, TwoRenderersWithDuck) {
  StreamVolumeManager stream_volume_manager(dispatcher());
  MockPolicyActionReporter policy_action_reporter([](auto _usage, auto _policy_action) {});
  AudioAdmin admin(kTestBehaviorGain, &stream_volume_manager, &policy_action_reporter,
                   dispatcher());
  test::NullAudioRenderer r1, r2;

  // Media should duck when comms is active.
  admin.SetInteraction(
      fuchsia::media::Usage::WithRenderUsage(
          fidl::Clone(fuchsia::media::AudioRenderUsage::COMMUNICATION)),
      fuchsia::media::Usage::WithRenderUsage(fidl::Clone(fuchsia::media::AudioRenderUsage::MEDIA)),
      fuchsia::media::Behavior::DUCK);

  // Set an inintial stream volume.
  const float kStreamGain = 1.0;
  stream_volume_manager.SetUsageGain(
      fuchsia::media::Usage::WithRenderUsage(fidl::Clone(fuchsia::media::AudioRenderUsage::MEDIA)),
      kStreamGain);
  stream_volume_manager.SetUsageGain(fuchsia::media::Usage::WithRenderUsage(fidl::Clone(
                                         fuchsia::media::AudioRenderUsage::COMMUNICATION)),
                                     kStreamGain);

  // create media active stream.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::MEDIA, true, &r1);
  RunLoopUntilIdle();
  EXPECT_EQ(kStreamGain + kNoneGain, stream_volume_manager.GetUsageGainSettings().GetUsageGain(
                                         fuchsia::media::Usage::WithRenderUsage(fidl::Clone(
                                             fuchsia::media::AudioRenderUsage::MEDIA))));

  // communications renderer becomes active; media should duck.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::COMMUNICATION, true, &r2);
  RunLoopUntilIdle();
  EXPECT_EQ(kStreamGain + kDuckGain, stream_volume_manager.GetUsageGainSettings().GetUsageGain(
                                         fuchsia::media::Usage::WithRenderUsage(fidl::Clone(
                                             fuchsia::media::AudioRenderUsage::MEDIA))));
  EXPECT_EQ(kStreamGain + kNoneGain, stream_volume_manager.GetUsageGainSettings().GetUsageGain(
                                         fuchsia::media::Usage::WithRenderUsage(fidl::Clone(
                                             fuchsia::media::AudioRenderUsage::COMMUNICATION))));

  // comms becomes inactive; ducking should stop.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::COMMUNICATION, false, &r2);
  RunLoopUntilIdle();
  EXPECT_EQ(kStreamGain + kNoneGain, stream_volume_manager.GetUsageGainSettings().GetUsageGain(
                                         fuchsia::media::Usage::WithRenderUsage(fidl::Clone(
                                             fuchsia::media::AudioRenderUsage::MEDIA))));
  EXPECT_EQ(kStreamGain + kNoneGain, stream_volume_manager.GetUsageGainSettings().GetUsageGain(
                                         fuchsia::media::Usage::WithRenderUsage(fidl::Clone(
                                             fuchsia::media::AudioRenderUsage::COMMUNICATION))));
}

TEST_F(AudioAdminTest, CapturerDucksRenderer) {
  StreamVolumeManager stream_volume_manager(dispatcher());
  MockPolicyActionReporter policy_action_reporter([](auto _usage, auto _policy_action) {});
  AudioAdmin admin(kTestBehaviorGain, &stream_volume_manager, &policy_action_reporter,
                   dispatcher());
  test::NullAudioRenderer r1;
  test::NullAudioCapturer c1;

  // Set an inintial stream volume.
  const float kStreamGain = 1.0;
  stream_volume_manager.SetUsageGain(
      fuchsia::media::Usage::WithRenderUsage(fidl::Clone(fuchsia::media::AudioRenderUsage::MEDIA)),
      kStreamGain);
  stream_volume_manager.SetUsageGain(fuchsia::media::Usage::WithCaptureUsage(fidl::Clone(
                                         fuchsia::media::AudioCaptureUsage::COMMUNICATION)),
                                     kStreamGain);

  // Media should duck when comms is active.
  admin.SetInteraction(
      fuchsia::media::Usage::WithCaptureUsage(
          fidl::Clone(fuchsia::media::AudioCaptureUsage::COMMUNICATION)),
      fuchsia::media::Usage::WithRenderUsage(fidl::Clone(fuchsia::media::AudioRenderUsage::MEDIA)),
      fuchsia::media::Behavior::DUCK);

  // Create active media stream.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::MEDIA, true, &r1);
  RunLoopUntilIdle();
  EXPECT_EQ(kStreamGain + kNoneGain, stream_volume_manager.GetUsageGainSettings().GetUsageGain(
                                         fuchsia::media::Usage::WithRenderUsage(fidl::Clone(
                                             fuchsia::media::AudioRenderUsage::MEDIA))));

  // Create active comms capturer; media output should duck.
  admin.UpdateCapturerState(fuchsia::media::AudioCaptureUsage::COMMUNICATION, true, &c1);
  RunLoopUntilIdle();
  EXPECT_EQ(kStreamGain + kDuckGain, stream_volume_manager.GetUsageGainSettings().GetUsageGain(
                                         fuchsia::media::Usage::WithRenderUsage(fidl::Clone(
                                             fuchsia::media::AudioRenderUsage::MEDIA))));
  EXPECT_EQ(kStreamGain + kNoneGain, stream_volume_manager.GetUsageGainSettings().GetUsageGain(
                                         fuchsia::media::Usage::WithCaptureUsage(fidl::Clone(
                                             fuchsia::media::AudioCaptureUsage::COMMUNICATION))));

  // Comms becomes inactive; ducking should stop.
  admin.UpdateCapturerState(fuchsia::media::AudioCaptureUsage::COMMUNICATION, false, &c1);
  RunLoopUntilIdle();
  EXPECT_EQ(kStreamGain + kNoneGain, stream_volume_manager.GetUsageGainSettings().GetUsageGain(
                                         fuchsia::media::Usage::WithRenderUsage(fidl::Clone(
                                             fuchsia::media::AudioRenderUsage::MEDIA))));
  EXPECT_EQ(kStreamGain + kNoneGain, stream_volume_manager.GetUsageGainSettings().GetUsageGain(
                                         fuchsia::media::Usage::WithCaptureUsage(fidl::Clone(
                                             fuchsia::media::AudioCaptureUsage::COMMUNICATION))));
}

TEST_F(AudioAdminTest, RendererDucksCapturer) {
  StreamVolumeManager stream_volume_manager(dispatcher());
  MockPolicyActionReporter policy_action_reporter([](auto _usage, auto _policy_action) {});
  AudioAdmin admin(kTestBehaviorGain, &stream_volume_manager, &policy_action_reporter,
                   dispatcher());
  test::NullAudioRenderer r1;
  test::NullAudioCapturer c1;

  const float kStreamGain = 1.0;
  stream_volume_manager.SetUsageGain(fuchsia::media::Usage::WithRenderUsage(fidl::Clone(
                                         fuchsia::media::AudioRenderUsage::COMMUNICATION)),
                                     kStreamGain);
  stream_volume_manager.SetUsageGain(fuchsia::media::Usage::WithCaptureUsage(fidl::Clone(
                                         fuchsia::media::AudioCaptureUsage::FOREGROUND)),
                                     kStreamGain);

  // Foreground capturer should duck when communication renderers are active.
  admin.SetInteraction(fuchsia::media::Usage::WithRenderUsage(
                           fidl::Clone(fuchsia::media::AudioRenderUsage::COMMUNICATION)),
                       fuchsia::media::Usage::WithCaptureUsage(
                           fidl::Clone(fuchsia::media::AudioCaptureUsage::FOREGROUND)),
                       fuchsia::media::Behavior::DUCK);

  // Create active capturer stream.
  admin.UpdateCapturerState(fuchsia::media::AudioCaptureUsage::FOREGROUND, true, &c1);
  RunLoopUntilIdle();
  EXPECT_EQ(kStreamGain + kNoneGain, stream_volume_manager.GetUsageGainSettings().GetUsageGain(
                                         fuchsia::media::Usage::WithCaptureUsage(fidl::Clone(
                                             fuchsia::media::AudioCaptureUsage::FOREGROUND))));

  // Create active comms renderer; foreground capturer should duck.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::COMMUNICATION, true, &r1);
  RunLoopUntilIdle();
  EXPECT_EQ(kStreamGain + kDuckGain, stream_volume_manager.GetUsageGainSettings().GetUsageGain(
                                         fuchsia::media::Usage::WithCaptureUsage(fidl::Clone(
                                             fuchsia::media::AudioCaptureUsage::FOREGROUND))));
  EXPECT_EQ(kStreamGain + kNoneGain, stream_volume_manager.GetUsageGainSettings().GetUsageGain(
                                         fuchsia::media::Usage::WithRenderUsage(fidl::Clone(
                                             fuchsia::media::AudioRenderUsage::COMMUNICATION))));

  // Comms becomes inactive; ducking should stop.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::COMMUNICATION, false, &r1);
  RunLoopUntilIdle();
  EXPECT_EQ(kStreamGain + kNoneGain, stream_volume_manager.GetUsageGainSettings().GetUsageGain(
                                         fuchsia::media::Usage::WithCaptureUsage(fidl::Clone(
                                             fuchsia::media::AudioCaptureUsage::FOREGROUND))));
  EXPECT_EQ(kStreamGain + kNoneGain, stream_volume_manager.GetUsageGainSettings().GetUsageGain(
                                         fuchsia::media::Usage::WithRenderUsage(fidl::Clone(
                                             fuchsia::media::AudioRenderUsage::COMMUNICATION))));
}

TEST_F(AudioAdminTest, PolicyActionsReported) {
  auto test_policy_action = [this](auto expected_action) {
    const auto target_usage = fuchsia::media::Usage::WithCaptureUsage(
        fidl::Clone(fuchsia::media::AudioCaptureUsage::FOREGROUND));
    fuchsia::media::Behavior policy_action_taken;
    // Record any actions taken on our target_usage (AudioCaptureUsage::FOREGROUND)
    MockPolicyActionReporter policy_action_reporter(
        [&policy_action_taken, &target_usage](auto usage, auto action) {
          if (fidl::Equals(usage, target_usage)) {
            policy_action_taken = action;
          }
        });

    StreamVolumeManager stream_volume_manager(dispatcher());
    AudioAdmin admin(kTestBehaviorGain, &stream_volume_manager, &policy_action_reporter,
                     dispatcher());
    test::NullAudioRenderer r1;
    test::NullAudioCapturer c1;

    const float kStreamGain = 1.0;
    stream_volume_manager.SetUsageGain(fuchsia::media::Usage::WithRenderUsage(fidl::Clone(
                                           fuchsia::media::AudioRenderUsage::COMMUNICATION)),
                                       kStreamGain);
    stream_volume_manager.SetUsageGain(fuchsia::media::Usage::WithCaptureUsage(fidl::Clone(
                                           fuchsia::media::AudioCaptureUsage::FOREGROUND)),
                                       kStreamGain);

    // Foreground capturer should duck when communication renderers are active.
    admin.SetInteraction(fuchsia::media::Usage::WithRenderUsage(
                             fidl::Clone(fuchsia::media::AudioRenderUsage::COMMUNICATION)),
                         fuchsia::media::Usage::WithCaptureUsage(
                             fidl::Clone(fuchsia::media::AudioCaptureUsage::FOREGROUND)),
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

}  // namespace
}  // namespace media::audio

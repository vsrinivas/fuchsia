// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_admin.h"

#include <lib/gtest/test_loop_fixture.h>

#include <unordered_map>

#include "src/media/audio/audio_core/audio_core_impl.h"
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
    .mute_gain_db = kMuteGain,
    .duck_gain_db = kDuckGain,
    .none_gain_db = kNoneGain,
};

// TODO(turnage): Use UsageGainSettings in this test; remove these functions and local storage for
//                usage gains.
fuchsia::media::Usage Usage(fuchsia::media::AudioRenderUsage u) {
  fuchsia::media::Usage usage;
  usage.set_render_usage(u);
  return usage;
}
fuchsia::media::Usage Usage(fuchsia::media::AudioCaptureUsage u) {
  fuchsia::media::Usage usage;
  usage.set_capture_usage(u);
  return usage;
}

// A |UsageGainAdjustment| that simply records all the requested adjustments and tracks the most
// recent gain for each usage.
class FakeUsageGainAdjustment : public UsageGainAdjustment {
 public:
  struct GainAdjustment {
    fuchsia::media::Usage usage;
    float gain_adjust;
  };

  std::vector<GainAdjustment> take_gain_adjustments() { return std::move(gain_adjustments_); }

  float GetUsageGainAdjustment(fuchsia::media::AudioRenderUsage usage) const {
    auto it = render_usage_gain_.find(usage);
    if (it == render_usage_gain_.end()) {
      return kNoneGain;
    }
    return it->second;
  }
  float GetUsageGainAdjustment(fuchsia::media::AudioCaptureUsage usage) const {
    auto it = capture_usage_gain_.find(usage);
    if (it == capture_usage_gain_.end()) {
      return kNoneGain;
    }
    return it->second;
  }

 private:
  // |UsageGainAdjustment|
  void SetRenderUsageGainAdjustment(fuchsia::media::AudioRenderUsage u, float gain_db) override {
    render_usage_gain_.insert_or_assign(u, gain_db);
    fuchsia::media::Usage usage;
    usage.set_render_usage(u);
    gain_adjustments_.emplace_back(GainAdjustment{std::move(usage), gain_db});
  }
  void SetCaptureUsageGainAdjustment(fuchsia::media::AudioCaptureUsage u, float gain_db) override {
    capture_usage_gain_.insert_or_assign(u, gain_db);
    fuchsia::media::Usage usage;
    usage.set_capture_usage(u);
    gain_adjustments_.emplace_back(GainAdjustment{std::move(usage), gain_db});
  }

  std::unordered_map<fuchsia::media::AudioRenderUsage, float> render_usage_gain_;
  std::unordered_map<fuchsia::media::AudioCaptureUsage, float> capture_usage_gain_;
  std::vector<GainAdjustment> gain_adjustments_;
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
  FakeUsageGainAdjustment g;
  MockPolicyActionReporter policy_action_reporter([](auto _usage, auto _policy_action) {});
  AudioAdmin admin(kTestBehaviorGain, &g, &policy_action_reporter);
  test::NullAudioRenderer r1, r2;

  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::MEDIA, true, &r1);
  // TODO: we should probably simplify this so we don't make 9 gain adjustments to keep the gain
  // for all usages at 0.
  EXPECT_EQ(9u, g.take_gain_adjustments().size());
  EXPECT_EQ(kNoneGain, g.GetUsageGainAdjustment(fuchsia::media::AudioRenderUsage::MEDIA));

  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::COMMUNICATION, true, &r2);
  EXPECT_EQ(9u, g.take_gain_adjustments().size());
  EXPECT_EQ(kNoneGain, g.GetUsageGainAdjustment(fuchsia::media::AudioRenderUsage::MEDIA));
  EXPECT_EQ(kNoneGain, g.GetUsageGainAdjustment(fuchsia::media::AudioRenderUsage::COMMUNICATION));
}

TEST_F(AudioAdminTest, TwoRenderersWithDuck) {
  FakeUsageGainAdjustment g;
  MockPolicyActionReporter policy_action_reporter([](auto _usage, auto _policy_action) {});
  AudioAdmin admin(kTestBehaviorGain, &g, &policy_action_reporter);
  test::NullAudioRenderer r1, r2;

  // Media should duck when comms is active.
  admin.SetInteraction(Usage(fuchsia::media::AudioRenderUsage::COMMUNICATION),
                       Usage(fuchsia::media::AudioRenderUsage::MEDIA),
                       fuchsia::media::Behavior::DUCK);

  // create media active stream.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::MEDIA, true, &r1);
  EXPECT_EQ(kNoneGain, g.GetUsageGainAdjustment(fuchsia::media::AudioRenderUsage::MEDIA));

  // communications renderer becomes active; media should duck.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::COMMUNICATION, true, &r2);
  EXPECT_EQ(kDuckGain, g.GetUsageGainAdjustment(fuchsia::media::AudioRenderUsage::MEDIA));
  EXPECT_EQ(kNoneGain, g.GetUsageGainAdjustment(fuchsia::media::AudioRenderUsage::COMMUNICATION));

  // comms becomes inactive; ducking should stop.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::COMMUNICATION, false, &r2);
  EXPECT_EQ(kNoneGain, g.GetUsageGainAdjustment(fuchsia::media::AudioRenderUsage::MEDIA));
  EXPECT_EQ(kNoneGain, g.GetUsageGainAdjustment(fuchsia::media::AudioRenderUsage::COMMUNICATION));
}

TEST_F(AudioAdminTest, CapturerDucksRenderer) {
  FakeUsageGainAdjustment g;
  MockPolicyActionReporter policy_action_reporter([](auto _usage, auto _policy_action) {});
  AudioAdmin admin(kTestBehaviorGain, &g, &policy_action_reporter);
  test::NullAudioRenderer r1;
  test::NullAudioCapturer c1;

  // Media should duck when comms is active.
  admin.SetInteraction(Usage(fuchsia::media::AudioCaptureUsage::COMMUNICATION),
                       Usage(fuchsia::media::AudioRenderUsage::MEDIA),
                       fuchsia::media::Behavior::DUCK);

  // Create active media stream.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::MEDIA, true, &r1);
  EXPECT_EQ(kNoneGain, g.GetUsageGainAdjustment(fuchsia::media::AudioRenderUsage::MEDIA));

  // Create active comms capturer; media output should duck.
  admin.UpdateCapturerState(fuchsia::media::AudioCaptureUsage::COMMUNICATION, true, &c1);
  EXPECT_EQ(kDuckGain, g.GetUsageGainAdjustment(fuchsia::media::AudioRenderUsage::MEDIA));
  EXPECT_EQ(kNoneGain, g.GetUsageGainAdjustment(fuchsia::media::AudioCaptureUsage::COMMUNICATION));

  // Comms becomes inactive; ducking should stop.
  admin.UpdateCapturerState(fuchsia::media::AudioCaptureUsage::COMMUNICATION, false, &c1);
  EXPECT_EQ(kNoneGain, g.GetUsageGainAdjustment(fuchsia::media::AudioRenderUsage::MEDIA));
  EXPECT_EQ(kNoneGain, g.GetUsageGainAdjustment(fuchsia::media::AudioCaptureUsage::COMMUNICATION));
}

TEST_F(AudioAdminTest, RendererDucksCapturer) {
  FakeUsageGainAdjustment g;
  MockPolicyActionReporter policy_action_reporter([](auto _usage, auto _policy_action) {});
  AudioAdmin admin(kTestBehaviorGain, &g, &policy_action_reporter);
  test::NullAudioRenderer r1;
  test::NullAudioCapturer c1;

  // Foreground capturer should duck when communication renderers are active.
  admin.SetInteraction(Usage(fuchsia::media::AudioRenderUsage::COMMUNICATION),
                       Usage(fuchsia::media::AudioCaptureUsage::FOREGROUND),
                       fuchsia::media::Behavior::DUCK);

  // Create active capturer stream.
  admin.UpdateCapturerState(fuchsia::media::AudioCaptureUsage::FOREGROUND, true, &c1);
  EXPECT_EQ(kNoneGain, g.GetUsageGainAdjustment(fuchsia::media::AudioCaptureUsage::FOREGROUND));

  // Create active comms renderer; foreground capturer should duck.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::COMMUNICATION, true, &r1);
  EXPECT_EQ(kDuckGain, g.GetUsageGainAdjustment(fuchsia::media::AudioCaptureUsage::FOREGROUND));
  EXPECT_EQ(kNoneGain, g.GetUsageGainAdjustment(fuchsia::media::AudioRenderUsage::COMMUNICATION));

  // Comms becomes inactive; ducking should stop.
  admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::COMMUNICATION, false, &r1);
  EXPECT_EQ(kNoneGain, g.GetUsageGainAdjustment(fuchsia::media::AudioCaptureUsage::FOREGROUND));
  EXPECT_EQ(kNoneGain, g.GetUsageGainAdjustment(fuchsia::media::AudioRenderUsage::COMMUNICATION));
}

TEST_F(AudioAdminTest, PolicyActionsReported) {
  auto test_policy_action = [](auto expected_action) {
    const auto target_usage = Usage(fuchsia::media::AudioCaptureUsage::FOREGROUND);
    fuchsia::media::Behavior policy_action_taken;
    // Record any actions taken on our target_usage (AudioCaptureUsage::FOREGROUND)
    MockPolicyActionReporter policy_action_reporter(
        [&policy_action_taken, &target_usage](auto usage, auto action) {
          if (fidl::Equals(usage, target_usage)) {
            policy_action_taken = action;
          }
        });

    FakeUsageGainAdjustment g;
    AudioAdmin admin(kTestBehaviorGain, &g, &policy_action_reporter);
    test::NullAudioRenderer r1;
    test::NullAudioCapturer c1;

    // Foreground capturer should receive policy action when communication renderers are active.
    admin.SetInteraction(Usage(fuchsia::media::AudioRenderUsage::COMMUNICATION),
                         Usage(fuchsia::media::AudioCaptureUsage::FOREGROUND), expected_action);

    // Create active capturer stream.
    admin.UpdateCapturerState(fuchsia::media::AudioCaptureUsage::FOREGROUND, true, &c1);

    // Create active comms renderer; foreground capturer should receive policy action.
    admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::COMMUNICATION, true, &r1);
    EXPECT_EQ(policy_action_taken, expected_action);

    // Comms becomes inactive; action should stop.
    admin.UpdateRendererState(fuchsia::media::AudioRenderUsage::COMMUNICATION, false, &r1);
    EXPECT_EQ(policy_action_taken, fuchsia::media::Behavior::NONE);
  };

  test_policy_action(fuchsia::media::Behavior::DUCK);
  test_policy_action(fuchsia::media::Behavior::MUTE);
}

}  // namespace
}  // namespace media::audio

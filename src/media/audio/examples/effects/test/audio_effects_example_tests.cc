// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>

#include <cmath>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/media/audio/examples/effects/delay_effect.h"
#include "src/media/audio/examples/effects/effect_base.h"
#include "src/media/audio/examples/effects/rechannel_effect.h"
#include "src/media/audio/examples/effects/swap_effect.h"
#include "src/media/audio/lib/effects_loader/effects_loader.h"
#include "src/media/audio/lib/effects_loader/effects_processor.h"

namespace media::audio_effects_example {

static constexpr const char* kDelayEffectConfig = "{\"delay_frames\": 0}";

//
// Tests EffectsLoader, which directly calls the shared library interface.
//
class EffectsLoaderTest : public testing::Test {
 protected:
  std::unique_ptr<audio::EffectsLoader> effects_loader_;

  void SetUp() override {
    ASSERT_EQ(ZX_OK,
              audio::EffectsLoader::CreateWithModule("audio_effects_example.so", &effects_loader_));
    ASSERT_TRUE(effects_loader_);
  }
};

//
// These child classes may not differentiate, but we use different classes for
// Delay/Rechannel/Swap in order to group related test results accordingly.
//
class DelayEffectTest : public EffectsLoaderTest {
 protected:
  void TestDelayBounds(uint32_t frame_rate, uint32_t channels, uint32_t delay_frames);
};
class RechannelEffectTest : public EffectsLoaderTest {};
class SwapEffectTest : public EffectsLoaderTest {};

// We test the delay effect with certain configuration values, making assumptions
// about how those values relate to the allowed range for this effect.
constexpr uint32_t kTestDelay1 = 1u;
constexpr uint32_t kTestDelay2 = 2u;
static_assert(DelayEffect::kMaxDelayFrames >= kTestDelay2, "Test value too high");
static_assert(DelayEffect::kMinDelayFrames <= kTestDelay1, "Test value too low");

// For the most part, the below tests use a specific channel_count.
constexpr uint16_t kTestChans = 2;

// When testing or using the delay effect, we make certain channel assumptions.
static_assert(DelayEffect::kNumChannelsIn == kTestChans ||
                  DelayEffect::kNumChannelsIn == FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
              "DelayEffect::kNumChannelsIn must match kTestChans");
static_assert(DelayEffect::kNumChannelsOut == kTestChans ||
                  DelayEffect::kNumChannelsOut == FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY ||
                  DelayEffect::kNumChannelsOut == FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN,
              "DelayEffect::kNumChannelsOut must match kTestChans");

// When testing or using rechannel effect, we make certain channel assumptions.
static_assert(RechannelEffect::kNumChannelsIn != 2 || RechannelEffect::kNumChannelsOut != 2,
              "RechannelEffect must not be stereo-in/-out");
static_assert(RechannelEffect::kNumChannelsIn != RechannelEffect::kNumChannelsOut &&
                  RechannelEffect::kNumChannelsOut != FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY &&
                  RechannelEffect::kNumChannelsOut != FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN,
              "RechannelEffect must not be in-place");

// When testing or using the swap effect, we make certain channel assumptions.
static_assert(SwapEffect::kNumChannelsIn == kTestChans ||
                  SwapEffect::kNumChannelsIn == FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
              "SwapEffect::kNumChannelsIn must match kTestChans");
static_assert(SwapEffect::kNumChannelsOut == kTestChans ||
                  SwapEffect::kNumChannelsOut == FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY ||
                  SwapEffect::kNumChannelsOut == FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN,
              "SwapEffect::kNumChannelsOut must match kTestChans");

// Tests the get_parameters ABI, and that the effect behaves as expected.
TEST_F(DelayEffectTest, GetParameters) {
  fuchsia_audio_effects_parameters effect_params;

  uint32_t frame_rate = 48000;
  media::audio::Effect effect = effects_loader_->CreateEffect(Effect::Delay, frame_rate, kTestChans,
                                                              kTestChans, kDelayEffectConfig);
  ASSERT_TRUE(effect);

  EXPECT_EQ(effect.GetParameters(&effect_params), ZX_OK);
  EXPECT_EQ(effect_params.frame_rate, frame_rate);
  EXPECT_EQ(effect_params.channels_in, kTestChans);
  EXPECT_EQ(effect_params.channels_out, kTestChans);
  EXPECT_TRUE(effect_params.signal_latency_frames == DelayEffect::kLatencyFrames);
  EXPECT_TRUE(effect_params.suggested_frames_per_buffer == DelayEffect::kLatencyFrames);

  // Verify null struct*
  EXPECT_NE(effect.GetParameters(nullptr), ZX_OK);
}

// Tests the get_parameters ABI, and that the effect behaves as expected.
TEST_F(RechannelEffectTest, GetParameters) {
  fuchsia_audio_effects_parameters effect_params;

  uint32_t frame_rate = 48000;
  media::audio::Effect effect =
      effects_loader_->CreateEffect(Effect::Rechannel, frame_rate, RechannelEffect::kNumChannelsIn,
                                    RechannelEffect::kNumChannelsOut, {});
  ASSERT_TRUE(effect);

  effect_params.frame_rate = 44100;  // should be overwritten
  EXPECT_EQ(effect.GetParameters(&effect_params), ZX_OK);
  EXPECT_EQ(effect_params.frame_rate, frame_rate);
  EXPECT_TRUE(effect_params.channels_in == RechannelEffect::kNumChannelsIn);
  EXPECT_TRUE(effect_params.channels_out == RechannelEffect::kNumChannelsOut);
  EXPECT_TRUE(effect_params.signal_latency_frames == RechannelEffect::kLatencyFrames);
  EXPECT_TRUE(effect_params.suggested_frames_per_buffer == RechannelEffect::kLatencyFrames);
}

// Tests the get_parameters ABI, and that the effect behaves as expected.
TEST_F(SwapEffectTest, GetParameters) {
  fuchsia_audio_effects_parameters effect_params;

  uint32_t frame_rate = 44100;
  media::audio::Effect effect =
      effects_loader_->CreateEffect(Effect::Swap, frame_rate, kTestChans, kTestChans, {});
  ASSERT_TRUE(effect);

  effect_params.frame_rate = 48000;  // should be overwritten
  EXPECT_EQ(effect.GetParameters(&effect_params), ZX_OK);
  EXPECT_EQ(effect_params.frame_rate, frame_rate);
  EXPECT_EQ(effect_params.channels_in, kTestChans);
  EXPECT_EQ(effect_params.channels_out, kTestChans);
  EXPECT_TRUE(effect_params.signal_latency_frames == SwapEffect::kLatencyFrames);
  EXPECT_TRUE(effect_params.suggested_frames_per_buffer == SwapEffect::kLatencyFrames);
}

TEST_F(SwapEffectTest, UpdateConfiguration) {
  media::audio::Effect effect =
      effects_loader_->CreateEffect(Effect::Swap, 48000, kTestChans, kTestChans, {});
  ASSERT_TRUE(effect);

  EXPECT_NE(effect.UpdateConfiguration({}), ZX_OK);
}

TEST_F(RechannelEffectTest, UpdateConfiguration) {
  media::audio::Effect effect =
      effects_loader_->CreateEffect(Effect::Rechannel, 48000, RechannelEffect::kNumChannelsIn,
                                    RechannelEffect::kNumChannelsOut, {});
  ASSERT_TRUE(effect);
  EXPECT_NE(effect.UpdateConfiguration({}), ZX_OK);
}

TEST_F(DelayEffectTest, UpdateConfiguration) {
  media::audio::Effect effect = effects_loader_->CreateEffect(Effect::Delay, 48000, kTestChans,
                                                              kTestChans, kDelayEffectConfig);
  ASSERT_TRUE(effect);

  // Validate min/max values are accepted.
  EXPECT_EQ(effect.UpdateConfiguration("{\"delay_frames\": 0}"), ZX_OK);
  EXPECT_EQ(effect.UpdateConfiguration(
                fxl::StringPrintf("{\"delay_frames\": %u}", DelayEffect::kMaxDelayFrames)),
            ZX_OK);

  // Some invalid configs
  EXPECT_NE(effect.UpdateConfiguration({}), ZX_OK);
  EXPECT_NE(effect.UpdateConfiguration("{}"), ZX_OK);
  EXPECT_NE(effect.UpdateConfiguration("{\"delay_frames\": -1}"), ZX_OK);
  EXPECT_NE(effect.UpdateConfiguration("{\"delay_frames\": \"foobar\"}"), ZX_OK);
  EXPECT_NE(effect.UpdateConfiguration("{\"delay_frames\": false}"), ZX_OK);
  EXPECT_NE(effect.UpdateConfiguration("{\"delay_frames\": {}}"), ZX_OK);
  EXPECT_NE(effect.UpdateConfiguration("{\"delay_frames\": []}"), ZX_OK);
  EXPECT_NE(effect.UpdateConfiguration(
                fxl::StringPrintf("{\"delay_frames\": %u}", DelayEffect::kMaxDelayFrames + 1)),
            ZX_OK);
  EXPECT_NE(effect.UpdateConfiguration("[]"), ZX_OK);
  EXPECT_NE(effect.UpdateConfiguration("This is not JSON"), ZX_OK);
  EXPECT_NE(effect.UpdateConfiguration("]["), ZX_OK);
  EXPECT_NE(effect.UpdateConfiguration("{\"delay_frames\": 0"), ZX_OK);
}

// Tests the process_inplace ABI, and that the effect behaves as expected.
TEST_F(DelayEffectTest, ProcessInPlace) {
  uint32_t num_samples = 12 * kTestChans;
  uint32_t delay_samples = 6 * kTestChans;
  float delay_buff_in_out[num_samples];
  float expect[num_samples];

  for (uint32_t i = 0; i < delay_samples; ++i) {
    delay_buff_in_out[i] = static_cast<float>(i + 1);
    expect[i] = 0.0f;
  }
  for (uint32_t i = delay_samples; i < num_samples; ++i) {
    delay_buff_in_out[i] = static_cast<float>(i + 1);
    expect[i] = delay_buff_in_out[i - delay_samples];
  }

  media::audio::Effect effect = effects_loader_->CreateEffect(Effect::Delay, 48000, kTestChans,
                                                              kTestChans, kDelayEffectConfig);
  ASSERT_TRUE(effect);

  ASSERT_EQ(effect.UpdateConfiguration("{\"delay_frames\": 6}"), ZX_OK);
  EXPECT_EQ(effect.ProcessInPlace(4, delay_buff_in_out), ZX_OK);
  EXPECT_EQ(effect.ProcessInPlace(4, delay_buff_in_out + (4 * kTestChans)), ZX_OK);
  EXPECT_EQ(effect.ProcessInPlace(4, delay_buff_in_out + (8 * kTestChans)), ZX_OK);

  for (uint32_t sample = 0; sample < num_samples; ++sample) {
    EXPECT_EQ(delay_buff_in_out[sample], expect[sample]) << sample;
  }
  EXPECT_EQ(effect.ProcessInPlace(0, delay_buff_in_out), ZX_OK);
}

// Tests cases in which we expect process_inplace to fail.
TEST_F(RechannelEffectTest, ProcessInPlace) {
  constexpr uint32_t kNumFrames = 1;
  float buff_in_out[kNumFrames * RechannelEffect::kNumChannelsIn] = {0};

  // Effects that change the channelization should not process in-place.
  media::audio::Effect effect =
      effects_loader_->CreateEffect(Effect::Rechannel, 48000, RechannelEffect::kNumChannelsIn,
                                    RechannelEffect::kNumChannelsOut, {});
  ASSERT_TRUE(effect);

  EXPECT_NE(effect.ProcessInPlace(kNumFrames, buff_in_out), ZX_OK);
}

// Tests the process_inplace ABI, and that the effect behaves as expected.
TEST_F(SwapEffectTest, ProcessInPlace) {
  constexpr uint32_t kNumFrames = 4;
  float swap_buff_in_out[kNumFrames * kTestChans] = {1.0f, -1.0f, 1.0f, -1.0f,
                                                     1.0f, -1.0f, 1.0f, -1.0f};

  media::audio::Effect effect =
      effects_loader_->CreateEffect(Effect::Swap, 48000, kTestChans, kTestChans, {});
  ASSERT_TRUE(effect);

  EXPECT_EQ(effect.ProcessInPlace(kNumFrames, swap_buff_in_out), ZX_OK);
  for (uint32_t sample_num = 0; sample_num < kNumFrames * kTestChans; ++sample_num) {
    EXPECT_EQ(swap_buff_in_out[sample_num], (sample_num % 2 ? 1.0f : -1.0f));
  }

  EXPECT_EQ(effect.ProcessInPlace(0, swap_buff_in_out), ZX_OK);

  // Calls with null buff_ptr should fail.
  EXPECT_NE(effect.ProcessInPlace(kNumFrames, nullptr), ZX_OK);
  EXPECT_NE(effect.ProcessInPlace(0, nullptr), ZX_OK);
}

// Tests cases in which we expect process to fail.
TEST_F(DelayEffectTest, Process) {
  constexpr uint32_t kNumFrames = 1;
  float audio_buff_in[kNumFrames * kTestChans] = {0.0f};
  float* audio_buff_out = nullptr;

  // These stereo-to-stereo effects should ONLY process in-place
  media::audio::Effect effect = effects_loader_->CreateEffect(Effect::Delay, 48000, kTestChans,
                                                              kTestChans, kDelayEffectConfig);
  ASSERT_TRUE(effect);
  EXPECT_NE(effect.Process(kNumFrames, audio_buff_in, &audio_buff_out), ZX_OK);
}

// Tests the process ABI, and that the effect behaves as expected.
TEST_F(RechannelEffectTest, Process) {
  constexpr uint32_t kNumFrames = 1;
  float audio_buff_in[kNumFrames * RechannelEffect::kNumChannelsIn] = {
      1.0f, -1.0f, 0.25f, -1.0f, 0.98765432f, -0.09876544f};
  float* audio_buff_out = nullptr;
  float expected[kNumFrames * RechannelEffect::kNumChannelsOut] = {0.799536645f, -0.340580851f};

  media::audio::Effect effect =
      effects_loader_->CreateEffect(Effect::Rechannel, 48000, RechannelEffect::kNumChannelsIn,
                                    RechannelEffect::kNumChannelsOut, {});
  ASSERT_TRUE(effect);

  EXPECT_EQ(effect.Process(kNumFrames, audio_buff_in, &audio_buff_out), ZX_OK);
  EXPECT_EQ(audio_buff_out[0], expected[0]) << std::setprecision(9) << audio_buff_out[0];
  EXPECT_EQ(audio_buff_out[1], expected[1]) << std::setprecision(9) << audio_buff_out[1];

  EXPECT_EQ(effect.Process(0, audio_buff_in, &audio_buff_out), ZX_OK);

  // Test null buffer_in, buffer_out
  EXPECT_NE(effect.Process(kNumFrames, nullptr, &audio_buff_out), ZX_OK);
  EXPECT_NE(effect.Process(kNumFrames, audio_buff_in, nullptr), ZX_OK);
  EXPECT_NE(effect.Process(0, nullptr, &audio_buff_out), ZX_OK);
  EXPECT_NE(effect.Process(0, audio_buff_in, nullptr), ZX_OK);
}

// Tests cases in which we expect process to fail.
TEST_F(SwapEffectTest, Process) {
  constexpr uint32_t kNumFrames = 1;
  float audio_buff_in[kNumFrames * kTestChans] = {0.0f};
  float* audio_buff_out = nullptr;

  // These stereo-to-stereo effects should ONLY process in-place
  media::audio::Effect effect =
      effects_loader_->CreateEffect(Effect::Swap, 48000, kTestChans, kTestChans, {});
  ASSERT_TRUE(effect);
  EXPECT_NE(effect.Process(kNumFrames, audio_buff_in, &audio_buff_out), ZX_OK);
}

// Tests the process_inplace ABI thru successive in-place calls.
TEST_F(DelayEffectTest, ProcessInPlace_Chain) {
  constexpr uint32_t kNumFrames = 6;

  std::vector<float> buff_in_out = {1.0f,  -0.1f, -0.2f, 2.0f,  0.3f,  -3.0f,
                                    -4.0f, 0.4f,  5.0f,  -0.5f, -0.6f, 6.0f};
  std::vector<float> expected = {0.0f,  0.0f, 0.0f, 0.0f,  0.0f,  0.0f,
                                 -0.1f, 1.0f, 2.0f, -0.2f, -3.0f, 0.3f};

  media::audio::Effect delay1 = effects_loader_->CreateEffect(Effect::Delay, 44100, kTestChans,
                                                              kTestChans, kDelayEffectConfig);
  media::audio::Effect swap =
      effects_loader_->CreateEffect(Effect::Swap, 44100, kTestChans, kTestChans, {});
  media::audio::Effect delay2 = effects_loader_->CreateEffect(Effect::Delay, 44100, kTestChans,
                                                              kTestChans, kDelayEffectConfig);

  ASSERT_TRUE(delay1);
  ASSERT_TRUE(swap);
  ASSERT_TRUE(delay2);

  ASSERT_EQ(delay1.UpdateConfiguration(fxl::StringPrintf("{\"delay_frames\": %u}", kTestDelay1)),
            ZX_OK);
  ASSERT_EQ(delay2.UpdateConfiguration(fxl::StringPrintf("{\"delay_frames\": %u}", kTestDelay2)),
            ZX_OK);

  EXPECT_EQ(delay1.ProcessInPlace(kNumFrames, buff_in_out.data()), ZX_OK);
  EXPECT_EQ(swap.ProcessInPlace(kNumFrames, buff_in_out.data()), ZX_OK);
  EXPECT_EQ(delay2.ProcessInPlace(kNumFrames, buff_in_out.data()), ZX_OK);

  EXPECT_THAT(buff_in_out, testing::ContainerEq(expected));

  EXPECT_EQ(delay2.ProcessInPlace(0, buff_in_out.data()), ZX_OK);
  EXPECT_EQ(swap.ProcessInPlace(0, buff_in_out.data()), ZX_OK);
  EXPECT_EQ(delay1.ProcessInPlace(0, buff_in_out.data()), ZX_OK);
}

// Tests the flush ABI, and effect discards state.
TEST_F(DelayEffectTest, Flush) {
  constexpr uint32_t kNumFrames = 1;
  float buff_in_out[kTestChans] = {1.0f, -1.0f};

  media::audio::Effect effect =
      effects_loader_->CreateEffect(Effect::Delay, 44100, kTestChans, kTestChans,
                                    fxl::StringPrintf("{\"delay_frames\": %u}", kTestDelay1));

  ASSERT_EQ(effect.ProcessInPlace(kNumFrames, buff_in_out), ZX_OK);
  ASSERT_EQ(buff_in_out[0], 0.0f);

  EXPECT_EQ(effect.Flush(), ZX_OK);

  // Validate that cached samples are flushed.
  EXPECT_EQ(effect.ProcessInPlace(kNumFrames, buff_in_out), ZX_OK);
  EXPECT_EQ(buff_in_out[0], 0.0f);
}

//
// We use this subfunction to test the outer limits allowed by ProcessInPlace.
void DelayEffectTest::TestDelayBounds(uint32_t frame_rate, uint32_t channels,
                                      uint32_t delay_frames) {
  uint32_t delay_samples = delay_frames * channels;
  uint32_t num_frames = frame_rate;
  uint32_t num_samples = num_frames * channels;

  std::vector<float> delay_buff_in_out(num_samples);
  std::vector<float> expect(num_samples);

  media::audio::Effect effect = effects_loader_->CreateEffect(Effect::Delay, frame_rate, channels,
                                                              channels, kDelayEffectConfig);
  ASSERT_TRUE(effect);

  ASSERT_EQ(effect.UpdateConfiguration(fxl::StringPrintf("{\"delay_frames\": %u}", delay_frames)),
            ZX_OK);

  for (uint32_t pass = 0; pass < 2; ++pass) {
    for (uint32_t i = 0; i < num_samples; ++i) {
      delay_buff_in_out[i] = static_cast<float>(i + pass * num_samples + 1);
      expect[i] = fmax(delay_buff_in_out[i] - delay_samples, 0.0f);
    }
    ASSERT_EQ(effect.ProcessInPlace(num_frames, delay_buff_in_out.data()), ZX_OK);

    EXPECT_THAT(delay_buff_in_out, testing::ContainerEq(expect));
  }
}

// Verifies DelayEffect at the outer allowed bounds (largest delays and buffers).
TEST_F(DelayEffectTest, ProcessInPlace_Bounds) {
  TestDelayBounds(192000, 2, DelayEffect::kMaxDelayFrames);
  TestDelayBounds(2000, FUCHSIA_AUDIO_EFFECTS_CHANNELS_MAX, DelayEffect::kMaxDelayFrames);
}

}  // namespace media::audio_effects_example

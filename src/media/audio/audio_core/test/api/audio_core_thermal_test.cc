// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#include <string>

#include "src/media/audio/audio_core/shared/mix_profile_config.h"
#include "src/media/audio/audio_core/testing/integration/hermetic_pipeline_test.h"
#include "src/media/audio/lib/analysis/generators.h"
#include "src/media/audio/lib/format/audio_buffer.h"
#include "src/media/audio/lib/test/comparators.h"

using ASF = fuchsia::media::AudioSampleFormat;

namespace media::audio::test {

class AudioCoreThermalTest : public HermeticPipelineTest {
 protected:
  static void SetUpTestSuite() {
    HermeticAudioTest::SetTestSuiteRealmOptions([] {
      return HermeticAudioRealm::Options{
          .audio_core_config_data = MakeAudioCoreConfig({
              .output_device_config = R"x(
                "device_id": "*",
                "supported_stream_types": [
                  "render:background",
                  "render:communications",
                  "render:interruption",
                  "render:media",
                  "render:system_agent"
                ],
                "pipeline": {
                  "name": "Single MixStage 48k",
                  "streams": [
                    "render:background",
                    "render:communications",
                    "render:interruption",
                    "render:media",
                    "render:system_agent"
                  ],
                  "effects": [
                    {
                      "lib": "audio-core-api-test-effects.so",
                      "effect": "doubler_filter",
                      "name": "doubler",
                      "config": { "enabled": true }
                    },
                    {
                      "lib": "audio-core-api-test-effects.so",
                      "effect": "inversion_filter",
                      "name": "inverter",
                      "config": { "enabled": false }
                    }
                  ],
                  "output_rate": 48000,
                  "output_channels": 1
                }
              )x",
              .thermal_config = R"x(
                {
                  "state_number": 0,
                  "effect_configs": {
                    "doubler":  { "enabled": true  },
                    "inverter": { "enabled": false }
                  }
                },
                {
                  "state_number": 1,
                  "effect_configs": {
                    "doubler":  { "enabled": false },
                    "inverter": { "enabled": false }
                  }
                },
                {
                  "state_number": 2,
                  "effect_configs": {
                    "doubler":  { "enabled": false },
                    "inverter": { "enabled": true  }
                  }
                },
                {
                  "state_number": 3,
                  "effect_configs": {
                    "doubler":  { "enabled": true },
                    "inverter": { "enabled": true  }
                  }
                }
              )x",
          }),
      };
    });
  }

  // This is equivalent to, but a simplification of, running HermeticStepTest with parameters:
  //  .test_name = "audio_core_float32_1chan_48k_thermal" + std::to_string(thermal_state),
  //  .input_format = Format::Create<ASF::FLOAT>(1, 48000).take_value(),
  //  .source_step_magnitude = 0.5f,
  //  .source_step_width_in_frames = 1,
  //  .path = RenderPath::Media,
  //  .pipeline = { .ramp_in_width = MixProfileConfig::kDefaultPeriod.to_nsecs()
  //                                 * 48000 * 2 / zx::sec(1).to_nsecs(),
  //                .stabilization_width = 0, .destabilization_width = 0, .decay_width = 0 },
  //  .thermal_state = thermal_state,
  //  .output_format = Format::Create<ASF::FLOAT>(1, 48000).take_value(),
  //  .expected_output_magnitude = 0.5f * gain_factor,
  void RunTestCase(uint32_t thermal_state, float gain_factor) {
    constexpr auto kStepWidth = 1;
    constexpr auto kFrameRate = 48000;
    const auto kFormat = Format::Create<ASF::FLOAT>(1, kFrameRate).take_value();
    const int64_t kStepPrePadding =
        kFrameRate * 2 * MixProfileConfig::kDefaultPeriod.to_nsecs() / zx::sec(1).to_nsecs();
    auto num_input_frames = kStepPrePadding + kStepWidth;
    auto num_output_frames =
        std::max(static_cast<int64_t>(AddSlackToOutputFrames(num_input_frames)), kFrameRate / 2L);

    auto device = CreateOutput(AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS, kFormat, num_output_frames);
    ASSERT_EQ(ConfigurePipelineForThermal(thermal_state), ZX_OK);
    auto renderer = CreateAudioRenderer(kFormat, num_input_frames);

    auto input_buffer = GenerateSilentAudio(kFormat, kStepPrePadding);
    auto signal = GenerateConstantAudio(kFormat, kStepWidth, 0.5f);
    input_buffer.Append(&signal);

    auto expected_buffer = AudioBufferSlice(&input_buffer).Clone();
    for (auto frame_num = kStepPrePadding; frame_num < expected_buffer.NumSamples(); ++frame_num) {
      expected_buffer.samples()[frame_num] *= gain_factor;
    }

    auto packets = renderer->AppendPackets({&input_buffer});
    renderer->PlaySynchronized(this, device, 0);
    renderer->WaitForPackets(this, packets);

    auto ring_buffer = device->SnapshotRingBuffer();

    if constexpr (!kEnableAllOverflowAndUnderflowChecksInRealtimeTests) {
      // In case of underflows, exit NOW (don't assess this buffer).
      // TODO(fxbug.dev/80003): Remove workarounds when underflow conditions are fixed.
      if (DeviceHasUnderflows(device)) {
        GTEST_SKIP() << "Skipping step magnitude checks due to underflows";
      }
    }

    CompareAudioBufferOptions opts;
    opts.test_label = "check pre-silence";
    CompareAudioBuffers(AudioBufferSlice(&ring_buffer, 0, kStepPrePadding),
                        AudioBufferSlice(&expected_buffer, 0, kStepPrePadding), opts);
    opts.test_label = "check data";
    CompareAudioBuffers(AudioBufferSlice(&ring_buffer, kStepPrePadding, num_input_frames),
                        AudioBufferSlice(&expected_buffer, kStepPrePadding, num_input_frames),
                        opts);
    opts.test_label = "check post-silence";
    CompareAudioBuffers(AudioBufferSlice(&ring_buffer, num_input_frames, num_output_frames),
                        AudioBufferSlice<ASF::FLOAT>(), opts);
  }
};

// At thermal state 0, we expect our amplitude to be doubled.
TEST_F(AudioCoreThermalTest, Thermal0) { RunTestCase(0, 2.0f); }

// At thermal state 1, no effects are enabled. We expect normal magnitude.
TEST_F(AudioCoreThermalTest, Thermal1) { RunTestCase(1, 1.0f); }

// At thermal state 2, "inverter" is enabled. We expect inverted magnitude.
TEST_F(AudioCoreThermalTest, Thermal2) { RunTestCase(2, -1.0f); }

// At thermal state 3, "doubler" and "inverter" are enabled. We expect doubled inverted magnitude.
TEST_F(AudioCoreThermalTest, Thermal3) { RunTestCase(3, -2.0f); }

}  // namespace media::audio::test

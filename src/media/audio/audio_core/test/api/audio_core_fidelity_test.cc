// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#include <fuchsia/media/audio/cpp/fidl.h>
#include <zircon/device/audio.h>

#include <algorithm>
#include <cmath>

#include "src/media/audio/audio_core/test/api/fidelity_results.h"
#include "src/media/audio/audio_core/testing/integration/hermetic_fidelity_test.h"
#include "src/media/audio/audio_core/v1/driver_output.h"
#include "src/media/audio/audio_core/v1/threading_model.h"
#include "src/media/audio/lib/analysis/generators.h"
#include "src/media/audio/lib/processing/coefficient_table.h"

using ASF = fuchsia::media::AudioSampleFormat;

namespace media::audio::test {

// Only a few test cases are enabled currently, to keep CQ run-time under 5 mins.
// TODO(fxbug.dev/89243): Enable disabled cases in a long-running test environment, once available.

// Pipeline width includes the required presentation delay, so even without effects this entails
// more than just SincSampler filter width.
//
// At the beginning of the output signal, these values represent:
//    ramp_in_width --  "read-ahead". how early the signal starts to ramp in.
//    stabilization_width --  "settle time" required after signal-start, before analysis.
//
// At the end of the output signal, these values represent:
//    destabilization_width --  any "unsettling" occurring BEFORE end of signal.
//    decay_width --  "ring-out" or decay time, after end of signal (not relevant for this class).

class AudioCoreFidelityTest : public HermeticFidelityTest {
 protected:
  static constexpr audio_stream_unique_id_t kOutputDeviceId =
      AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS;

  static constexpr size_t kFilterWidthFrames =
      media_audio::SincFilterCoefficientTable::kMaxFracSideLength >> Fixed::Format::FractionalBits;

  static HermeticPipelineTest::PipelineConstants pipeline_constants(int32_t source_rate,
                                                                    int32_t num_mix_stages = 1) {
    return {
        .ramp_in_width =
            LeadTimeFramesFromSourceRate(source_rate) + kFilterWidthFrames * num_mix_stages,
        .stabilization_width = kFilterWidthFrames * num_mix_stages,
        .destabilization_width = kFilterWidthFrames * num_mix_stages,
        .decay_width = kFilterWidthFrames * num_mix_stages,
    };
  }

 private:
  static size_t LeadTimeFramesFromSourceRate(int32_t source_rate) {
    return static_cast<size_t>(2l * source_rate * MixProfileConfig::kDefaultPeriod.to_nsecs() /
                               zx::sec(1).to_nsecs());
  }
};

class AudioCore48kFidelityTest : public AudioCoreFidelityTest {
 protected:
  static constexpr int32_t kDeviceFrameRate = 48000;
  static constexpr int32_t kDeviceChannels = 2;

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
                    "render:system_agent",
                    "capture:loopback"
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
                    "loopback": true,
                    "output_rate": 48000,
                    "output_channels": 2
                }
              )x",
          }),
      };
    });
  }
};

class AudioCore96kFidelityTest : public AudioCoreFidelityTest {
 protected:
  static constexpr int32_t kDeviceFrameRate = 96000;
  static constexpr int32_t kDeviceChannels = 2;

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
                    "render:system_agent",
                    "render:ultrasound",
                    "capture:loopback"
                ],
                "pipeline": {
                    "name": "Single MixStage 96k",
                    "streams": [
                        "render:background",
                        "render:communications",
                        "render:interruption",
                        "render:media",
                        "render:system_agent",
                        "render:ultrasound"
                    ],
                    "loopback": true,
                    "output_rate": 96000,
                    "output_channels": 2
                }
              )x",
          }),
      };
    });
  }
};

class AudioCore48k96kFidelityTest : public AudioCoreFidelityTest {
 protected:
  static constexpr int32_t kDeviceFrameRate = 96000;
  static constexpr int32_t kDeviceChannels = 2;

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
                    "render:system_agent",
                    "render:ultrasound",
                    "capture:loopback"
                ],
                "pipeline": {
                    "name": "Final MixStage 96k",
                    "inputs": [
                        {
                            "name": "Initial MixStage 48k",
                            "streams": [
                                "render:background",
                                "render:communications",
                                "render:interruption",
                                "render:media",
                                "render:system_agent"
                            ],
                            "loopback": true,
                            "output_rate": 48000,
                            "output_channels": 2
                        }
                    ],
                    "streams": [
                        "render:ultrasound"
                    ],
                    "output_rate": 96000,
                    "output_channels": 2
                }
              )x",
          }),
      };
    });
  }
};

class AudioCoreMaxGainTest : public AudioCoreFidelityTest {
 protected:
  static constexpr int32_t kDeviceFrameRate = 48000;
  static constexpr int32_t kDeviceChannels = 1;

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
                    "name": "Gain-limited MixStage 48k (max 0db)",
                    "max_gain_db": 0,
                    "streams": [
                        "render:background",
                        "render:communications",
                        "render:interruption",
                        "render:media",
                        "render:system_agent"
                    ],
                    "output_rate": 48000,
                    "output_channels": 1
                }
              )x",
          }),
      };
    });
  }
};

class AudioCoreMinGainTest : public AudioCoreFidelityTest {
 protected:
  static constexpr int32_t kDeviceFrameRate = 48000;
  static constexpr int32_t kDeviceChannels = 1;

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
                    "name": "Gain-limited MixStage 48k (min 0db)",
                    "min_gain_db": 0,
                    "streams": [
                        "render:background",
                        "render:communications",
                        "render:interruption",
                        "render:media",
                        "render:system_agent"
                    ],
                    "output_rate": 48000,
                    "output_channels": 1
                }
              )x",
          }),
      };
    });
  }
};

//
// Assess frequency response/sinad for source sample_formats. Test with single MixStage at 96k.
//
class AudioCoreSourceFormatFidelityTest : public AudioCore96kFidelityTest {};

// Best-case uint8 results are kUint8LimitsDb (unlike kFullScaleLimitsDb for other sample_formats)
// because of the amplitude delta between max (0x7F) and min (0x80).
//
// 96k unsigned-8 stereo stream to a 96k stereo mix stage (PointSampler)
// Given input in Left channel (0), validate freq response and sinad of output Left (chan 0).
TEST_F(AudioCoreSourceFormatFidelityTest, DISABLED_Uint8PassThru) {
  constexpr uint32_t kSourceRate = 96000;

  Run(HermeticFidelityTest::TestCase<ASF::UNSIGNED_8, ASF::FLOAT>{
      .test_name = "audio_core_uint8_2chan_96k_point_96k",

      .input_format = Format::Create<ASF::UNSIGNED_8>(2, kSourceRate).take_value(),
      .path = RenderPath::Media,
      .channels_to_play{0},
      .renderer_clock_mode = ClockMode::Default,

      .pipeline = pipeline_constants(kSourceRate),
      .single_frequency_to_test = 1000,

      .device_id = kOutputDeviceId,
      .output_format = Format::Create<ASF::FLOAT>(kDeviceChannels, kDeviceFrameRate).take_value(),
      .channels_to_measure{
          {
              0,
              FidelityResults::kUint8LimitsDb,
              FidelityResults::kUint8SinadLimitsDb,
          },
          {
              1,
              FidelityResults::kSilenceDb,
              FidelityResults::kFloat32SinadLimitsDb,
          },
      },
  });
}

// 96k signed-16 stereo stream to a 96k stereo mix stage (PointSampler)
TEST_F(AudioCoreSourceFormatFidelityTest, Int16PassThru) {
  constexpr uint32_t kSourceRate = 96000;

  Run(HermeticFidelityTest::TestCase<ASF::SIGNED_16, ASF::FLOAT>{
      .test_name = "audio_core_int16_2chan_96k_point_96k",

      .input_format = Format::Create<ASF::SIGNED_16>(2, kSourceRate).take_value(),
      .path = RenderPath::Media,
      .channels_to_play{1},
      .renderer_clock_mode = ClockMode::Default,

      .pipeline = pipeline_constants(kSourceRate),
      .single_frequency_to_test = 1000,

      .device_id = kOutputDeviceId,
      .output_format = Format::Create<ASF::FLOAT>(kDeviceChannels, kDeviceFrameRate).take_value(),
      .channels_to_measure{
          {
              0,
              FidelityResults::kSilenceDb,
              FidelityResults::kFloat32SinadLimitsDb,
          },
          {
              1,
              FidelityResults::kFullScaleLimitsDb,
              FidelityResults::kInt16SinadLimitsDb,
          },
      },
  });
}

// 96k signed-24 stereo stream to a 96k stereo mix stage (PointSampler)
TEST_F(AudioCoreSourceFormatFidelityTest, DISABLED_Int24PassThru) {
  constexpr uint32_t kSourceRate = 96000;

  Run(HermeticFidelityTest::TestCase<ASF::SIGNED_24_IN_32, ASF::FLOAT>{
      .test_name = "audio_core_int24_2chan_96k_point_96k",

      .input_format = Format::Create<ASF::SIGNED_24_IN_32>(2, kSourceRate).take_value(),
      .path = RenderPath::Media,
      .channels_to_play{0},
      .renderer_clock_mode = ClockMode::Default,

      .pipeline = pipeline_constants(kSourceRate),
      .single_frequency_to_test = 1000,

      .device_id = kOutputDeviceId,
      .output_format = Format::Create<ASF::FLOAT>(kDeviceChannels, kDeviceFrameRate).take_value(),
      .channels_to_measure{
          {
              0,
              FidelityResults::kFullScaleLimitsDb,
              FidelityResults::kInt24SinadLimitsDb,
          },
          {
              1,
              FidelityResults::kSilenceDb,
              FidelityResults::kFloat32SinadLimitsDb,
          },
      },
  });
}

// 96k float32 stereo stream to a 96k stereo mix stage (PointSampler)
TEST_F(AudioCoreSourceFormatFidelityTest, DISABLED_Float32PassThru) {
  constexpr uint32_t kSourceRate = 96000;

  Run(HermeticFidelityTest::TestCase<ASF::FLOAT, ASF::FLOAT>{
      .test_name = "audio_core_float32_2chan_96k_point_96k",

      .input_format = Format::Create<ASF::FLOAT>(2, kSourceRate).take_value(),
      .path = RenderPath::Media,
      .channels_to_play{1},
      .renderer_clock_mode = ClockMode::Default,

      .pipeline = pipeline_constants(kSourceRate),
      .single_frequency_to_test = 1000,

      .device_id = kOutputDeviceId,
      .output_format = Format::Create<ASF::FLOAT>(kDeviceChannels, kDeviceFrameRate).take_value(),
      .channels_to_measure{
          {
              0,
              FidelityResults::kSilenceDb,
              FidelityResults::kFloat32SinadLimitsDb,
          },
          {
              1,
              FidelityResults::kFullScaleLimitsDb,
              FidelityResults::kFloat32SinadLimitsDb,
          },
      },
  });
}

//
// Assess frequency response and sinad for non-float32 destination sample_formats
// TODO(fxbug.dev/86301): Output format fidelity cases -- int24, int16, uint8, (float) for both
//   full-scale and mute; all cases single-frequency, mono float32 96k source, mono 96k dest

//
// Assess single-mix-stage frequency response and sinad, across channelization changes
// TODO(fxbug.dev/86300): ChannelizationFidelity cases -- for both point and sinc samplers;
//   mono stream->stereo MixStage, mono MixStage->stereo MixStage, stereo stream->mono MixStage;
//   all cases single-frequency, float32 96k source, float32 96k dest

//
// Assess single-mix-stage frequency response and sinad, across gain changes
// TODO(fxbug.dev/86302): Gain accuracy (FR) and dynamic range (SiNAD) at -30dB, -60dB, -90dB.
//   all cases single-frequency, mono float32 96k source, mono float32 96k dest

//
// Assess single-mix-stage frequency response and sinad, without frame-rate conversion
//

// PointSampler MixStages are well-tested by the SourceFormat cases above.
//
// 48k float32 stereo stream to a 48k stereo mix stage, with custom ref clock.
// We should stay perfectly synchronized, so results should be identical to PassThru.
TEST_F(AudioCore48kFidelityTest, DISABLED_48kMicro48k) {
  constexpr uint32_t kSourceRate = 48000;

  Run(HermeticFidelityTest::TestCase<ASF::FLOAT, ASF::FLOAT>{
      .test_name = "audio_core_float32_2chan_48k_microsrc_48k",

      .input_format = Format::Create<ASF::FLOAT>(2, kSourceRate).take_value(),
      .path = RenderPath::Media,
      .channels_to_play{0},
      .renderer_clock_mode = ClockMode::Offset,

      .pipeline = pipeline_constants(kSourceRate),

      .device_id = kOutputDeviceId,
      .output_format = Format::Create<ASF::FLOAT>(kDeviceChannels, kDeviceFrameRate).take_value(),
      .channels_to_measure{
          {
              0,
              FidelityResults::kFullScaleLimitsDb,
              FidelityResults::kFloat32SinadLimitsDb,
          },
      },
  });
}

//
// Assess single-mix-stage frequency response and sinad, with frame-rate conversion to 48k
//

// 44.1k float32 stereo stream to 48k stereo mix stage (SincSampler)
TEST_F(AudioCore48kFidelityTest, DISABLED_44100To48k) {
  constexpr uint32_t kSourceRate = 44100;

  Run(HermeticFidelityTest::TestCase<ASF::FLOAT, ASF::FLOAT>{
      .test_name = "audio_core_float32_2chan_44100_48k",

      .input_format = Format::Create<ASF::FLOAT>(2, kSourceRate).take_value(),
      .path = RenderPath::Media,
      .channels_to_play{0},
      .renderer_clock_mode = ClockMode::Monotonic,

      .pipeline = pipeline_constants(kSourceRate),

      .device_id = kOutputDeviceId,
      .output_format = Format::Create<ASF::FLOAT>(kDeviceChannels, kDeviceFrameRate).take_value(),
      .channels_to_measure{
          {
              0,
              FidelityResults::k44100To48kLimitsDb,
              FidelityResults::k44100To48kSinadLimitsDb,
          },
      },
  });
}

// 44.1k float32 stereo stream (with custom ref clock) to 48k stereo mix stage (SincSampler).
// audio_core chases a custom clock at non-trivial conversion ratio, so SiNAD is slightly lower.
TEST_F(AudioCore48kFidelityTest, DISABLED_44100Micro48k) {
  constexpr uint32_t kSourceRate = 44100;

  Run(HermeticFidelityTest::TestCase<ASF::FLOAT, ASF::FLOAT>{
      .test_name = "audio_core_float32_2chan_44100_microsrc_48k",

      .input_format = Format::Create<ASF::FLOAT>(2, kSourceRate).take_value(),
      .path = RenderPath::Media,
      .channels_to_play{0},
      .renderer_clock_mode = ClockMode::Offset,

      .pipeline = pipeline_constants(kSourceRate),

      .device_id = kOutputDeviceId,
      .output_format = Format::Create<ASF::FLOAT>(kDeviceChannels, kDeviceFrameRate).take_value(),
      .channels_to_measure{
          {
              0,
              FidelityResults::k44100To48kLimitsDb,
              FidelityResults::k44100Micro48kSinadLimitsDb,
          },
      },
  });
}

// 96k float32 stereo stream (with custom ref clock) to a 48k stereo mix stage.
TEST_F(AudioCore48kFidelityTest, 96kMicro48k) {
  constexpr uint32_t kSourceRate = 96000;

  Run(HermeticFidelityTest::TestCase<ASF::FLOAT, ASF::FLOAT>{
      .test_name = "audio_core_float32_2chan_96k_microsrc_48k",

      .input_format = Format::Create<ASF::FLOAT>(2, kSourceRate).take_value(),
      .path = RenderPath::Media,
      .channels_to_play{0},
      .renderer_clock_mode = ClockMode::Offset,

      .pipeline = pipeline_constants(kSourceRate),

      .device_id = kOutputDeviceId,
      .output_format = Format::Create<ASF::FLOAT>(kDeviceChannels, kDeviceFrameRate).take_value(),
      .channels_to_measure{
          {
              0,
              FidelityResults::k96kMicro48kLimitsDb,
              FidelityResults::k96kMicro48kSinadLimitsDb,
          },
      },
  });
}

//
// Assess ultrasound pass-through (no format/channel/rate conversion)
//

// ultrasound (must be float32 and match mix stage [96k stereo]) stream to 96k stereo mix stage
TEST_F(AudioCore96kFidelityTest, DISABLED_Ultrasound) {
  Run(HermeticFidelityTest::TestCase<ASF::FLOAT, ASF::FLOAT>{
      .test_name = "audio_core_ultrasound_float32_2chan_96k",

      .input_format = Format::Create<ASF::FLOAT>(2, kDeviceFrameRate).take_value(),
      .path = RenderPath::Ultrasound,
      .channels_to_play{0},

      .pipeline = pipeline_constants(kDeviceFrameRate),

      .device_id = kOutputDeviceId,
      .output_format = Format::Create<ASF::FLOAT>(kDeviceChannels, kDeviceFrameRate).take_value(),
      .channels_to_measure{
          {
              0,
              FidelityResults::kFullScaleLimitsDb,
              FidelityResults::kFloat32SinadLimitsDb,
          },
          {
              1,
              FidelityResults::kSilenceDb,
              FidelityResults::kFloat32SinadLimitsDb,
          },
      },
  });
}

//
// Assess single-mix-stage frequency response and sinad, with frame-rate conversion to 96k
//

// 48k float32 stereo stream to 96k stereo mix stage
TEST_F(AudioCore96kFidelityTest, DISABLED_48kTo96k) {
  constexpr uint32_t kSourceRate = 48000;

  Run(HermeticFidelityTest::TestCase<ASF::FLOAT, ASF::FLOAT>{
      .test_name = "audio_core_float32_2chan_48k_96k",

      .input_format = Format::Create<ASF::FLOAT>(2, kSourceRate).take_value(),
      .path = RenderPath::Media,
      .channels_to_play{0},
      .renderer_clock_mode = ClockMode::Default,

      .pipeline = pipeline_constants(kSourceRate),

      .device_id = kOutputDeviceId,
      .output_format = Format::Create<ASF::FLOAT>(kDeviceChannels, kDeviceFrameRate).take_value(),
      .channels_to_measure{
          {
              0,
              FidelityResults::k48kTo96kLimitsDb,
              FidelityResults::k48kTo96kSinadLimitsDb,
          },
          {
              1,
              FidelityResults::kSilenceDb,
              FidelityResults::kFloat32SinadLimitsDb,
          },
      },
  });
}

//
// Assess two-mix-stage frequency response and sinad, to 48k then up to 96k
//

// 24k float32 stereo stream to 48k stereo mix stage, to 96k stereo mix stage
TEST_F(AudioCore48k96kFidelityTest, DISABLED_24kTo48kTo96k) {
  constexpr uint32_t kSourceRate = 24000;

  Run(HermeticFidelityTest::TestCase<ASF::FLOAT, ASF::FLOAT>{
      .test_name = "audio_core_float32_2chan_24k_48k_96k",

      .input_format = Format::Create<ASF::FLOAT>(2, kSourceRate).take_value(),
      .path = RenderPath::Media,
      .channels_to_play{0},
      .renderer_clock_mode = ClockMode::Monotonic,

      .pipeline = pipeline_constants(kSourceRate, 2),

      .device_id = kOutputDeviceId,
      .output_format = Format::Create<ASF::FLOAT>(kDeviceChannels, kDeviceFrameRate).take_value(),
      .channels_to_measure{
          {
              0,
              FidelityResults::k24kTo48kTo96kLimitsDb,
              FidelityResults::k24kTo48kTo96kSinadLimitsDb,
          },
          {
              1,
              FidelityResults::kSilenceDb,
              FidelityResults::kFloat32SinadLimitsDb,
          },
      },
  });
}

// 96k float32 stereo stream to 48k stereo mix stage, to 96k stereo mix stage
// Note: our low-pass frequency instructs HermeticFidelityTest to expect out-of-band rejection for
// frequencies above 24kHz, even though both source and output rates exceed 48kHz.
TEST_F(AudioCore48k96kFidelityTest, 96kTo48kTo96k) {
  constexpr uint32_t kSourceRate = 96000;

  Run(HermeticFidelityTest::TestCase<ASF::FLOAT, ASF::FLOAT>{
      .test_name = "audio_core_float32_2chan_96k_48k_96k",

      .input_format = Format::Create<ASF::FLOAT>(2, kSourceRate).take_value(),
      .path = RenderPath::Media,
      .channels_to_play{0},
      .renderer_clock_mode = ClockMode::Flexible,

      .pipeline = pipeline_constants(kSourceRate, 2),
      .low_pass_frequency = 24000,

      .device_id = kOutputDeviceId,
      .output_format = Format::Create<ASF::FLOAT>(kDeviceChannels, kDeviceFrameRate).take_value(),
      .channels_to_measure{
          {
              0,
              FidelityResults::k96kTo48kTo96kLimitsDb,
              FidelityResults::k96kTo48kTo96kSinadLimitsDb,
          },
          {
              1,
              FidelityResults::kSilenceDb,
              FidelityResults::kFloat32SinadLimitsDb,
          },
      },
  });
}

// 48k float32 mono stream to 48k mono mix stage that has a maximum gain of 0dB. If the max_gain
// limiting is not working, then our SiNAD measurement will fail.
TEST_F(AudioCoreMaxGainTest, MaxGainTest) {
  constexpr uint32_t kSourceRate = 48000;

  Run(HermeticFidelityTest::TestCase<ASF::FLOAT, ASF::FLOAT>{
      .test_name = "audio_core_max_gain_float32_1chan_48k",

      .input_format = Format::Create<ASF::FLOAT>(1, kSourceRate).take_value(),
      .path = RenderPath::Communications,
      .channels_to_play{0},
      .renderer_clock_mode = ClockMode::Flexible,
      .gain_db = +20.0f,  // This clips heavily (low SiNAD) without "max_gain=0" in static config.

      .pipeline = pipeline_constants(kSourceRate, 1),
      .single_frequency_to_test = 8000,

      .device_id = kOutputDeviceId,
      .output_format = Format::Create<ASF::FLOAT>(kDeviceChannels, kDeviceFrameRate).take_value(),
      .channels_to_measure{
          {
              0,
              FidelityResults::kFullScaleLimitsDb,
              FidelityResults::kFloat32SinadLimitsDb,
          },
      },
  });
}

// 48k float32 mono stream to 48k mono mix stage that has a minimum gain of 0dB. If the min_gain
// limiting is not working, then our Frequency Response measurement will fail.
TEST_F(AudioCoreMinGainTest, MinGainTest) {
  constexpr uint32_t kSourceRate = 48000;

  Run(HermeticFidelityTest::TestCase<ASF::FLOAT, ASF::FLOAT>{
      .test_name = "audio_core_min_gain_float32_1chan_48k",

      .input_format = Format::Create<ASF::FLOAT>(1, kSourceRate).take_value(),
      .path = RenderPath::Communications,
      .channels_to_play{0},
      .renderer_clock_mode = ClockMode::Flexible,
      .gain_db = -20.0f,  // Without "min_gain=0" in static config, FR is -20dB (not unity 0dB).

      .pipeline = pipeline_constants(kSourceRate, 1),
      .single_frequency_to_test = 8000,

      .device_id = kOutputDeviceId,
      .output_format = Format::Create<ASF::FLOAT>(kDeviceChannels, kDeviceFrameRate).take_value(),
      .channels_to_measure{
          {
              0,
              FidelityResults::kFullScaleLimitsDb,
              FidelityResults::kFloat32SinadLimitsDb,
          },
      },
  });
}

// 48k float32 mono stream to 48k mono mix stage that has a maximum gain of 0dB. Even with min_gain
// limiting, a gain of -160 dB should always lead to silence being produced.
TEST_F(AudioCoreMinGainTest, MinGainTestAtMutedGainDb) {
  constexpr uint32_t kSourceRate = 48000;

  Run(HermeticFidelityTest::TestCase<ASF::FLOAT, ASF::FLOAT>{
      .test_name = "audio_core_min_gain_float32_1chan_48k_minus_160db",

      .input_format = Format::Create<ASF::FLOAT>(1, kSourceRate).take_value(),
      .path = RenderPath::Communications,
      .channels_to_play{0},
      .renderer_clock_mode = ClockMode::Flexible,
      .gain_db = fuchsia::media::audio::MUTED_GAIN_DB,

      .pipeline = pipeline_constants(kSourceRate, 1),
      .single_frequency_to_test = 8000,

      .device_id = kOutputDeviceId,
      .output_format = Format::Create<ASF::FLOAT>(kDeviceChannels, kDeviceFrameRate).take_value(),
      .channels_to_measure{
          {
              0,
              FidelityResults::kSilenceDb,
              FidelityResults::kFloat32SinadLimitsDb,
          },
      },
  });
}

}  // namespace media::audio::test

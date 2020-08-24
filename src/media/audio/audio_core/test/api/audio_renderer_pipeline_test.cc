// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <fuchsia/media/tuning/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <set>
#include <vector>

#include "src/media/audio/audio_core/audio_device.h"
#include "src/media/audio/audio_core/audio_tuner_impl.h"
#include "src/media/audio/lib/analysis/analysis.h"
#include "src/media/audio/lib/analysis/generators.h"
#include "src/media/audio/lib/logging/logging.h"
#include "src/media/audio/lib/test/comparators.h"
#include "src/media/audio/lib/test/hermetic_audio_test.h"

using ASF = fuchsia::media::AudioSampleFormat;
using AudioRenderUsage = fuchsia::media::AudioRenderUsage;

namespace media::audio::test {

namespace {
constexpr size_t kNumPacketsInPayload = 50;
constexpr size_t kFrameRate = 48000;
constexpr size_t kPacketFrames = kFrameRate * RendererShimImpl::kPacketMs / 1000;
constexpr size_t kPayloadFrames = kPacketFrames * kNumPacketsInPayload;
// This is kPayloadFrames rounded down to the nearest power of 2.
static const size_t kPayloadFramesPow2 = std::pow(2, std::floor(std::log2(kPayloadFrames)));
// The length of gain ramp for each volume change.
// Must match the constant in audio_core.
constexpr zx::duration kVolumeRampDuration = zx::msec(5);
}  // namespace

template <ASF SampleType>
class AudioRendererPipelineTest : public HermeticAudioTest {
 protected:
  AudioRendererPipelineTest() : format_(Format::Create<SampleType>(2, kFrameRate).value()) {}

  void SetUp() {
    HermeticAudioTest::SetUp();
    // None of our tests should underflow.
    FailUponOverflowsOrUnderflows();
    // The output can store exactly 1s of audio data.
    output_ = CreateOutput({{0xff, 0x00}}, format_, 48000);
    renderer_ = CreateAudioRenderer(format_, kPayloadFrames);
  }

  // All pipeline tests send batches of packets. This method returns the suggested size for
  // each batch. We want each batch to be large enough such that the output driver needs to
  // wake multiple times to mix the batch -- this ensures we're testing the timing paths in
  // the driver. We don't have direct access to the driver's timers, however, we know that
  // the driver must wake up at least once every MinLeadTime. Therefore, we return enough
  // packets to exceed one MinLeadTime.
  size_t NumPacketsPerBatch() {
    auto min_lead_time = renderer_->GetMinLeadTime();
    FX_CHECK(min_lead_time.get() > 0);
    // In exceptional cases, min_lead_time might be smaller than one packet.
    // Ensure we have at least a handful of packets.
    auto n =
        std::max(5lu, static_cast<size_t>(min_lead_time / zx::msec(RendererShimImpl::kPacketMs)));
    FX_CHECK(n < kNumPacketsInPayload);
    return n;
  }

  const TypedFormat<SampleType> format_;
  VirtualOutput<SampleType>* output_ = nullptr;
  AudioRendererShim<SampleType>* renderer_ = nullptr;
};

using AudioRendererPipelineTestInt16 = AudioRendererPipelineTest<ASF::SIGNED_16>;
using AudioRendererPipelineTestFloat = AudioRendererPipelineTest<ASF::FLOAT>;

// Validate that timestamped packets play through renderer to ring buffer as expected.
TEST_F(AudioRendererPipelineTestInt16, RenderWithPts) {
  const auto num_packets = NumPacketsPerBatch();
  const auto num_frames = num_packets * kPacketFrames;

  auto input_buffer = GenerateSequentialAudio<ASF::SIGNED_16>(format_, num_frames);
  auto packets = renderer_->AppendPackets({&input_buffer});
  renderer_->PlaySynchronized(this, output_, 0);
  renderer_->WaitForPackets(this, packets);
  auto ring_buffer = output_->SnapshotRingBuffer();

  // The ring buffer should match the input buffer for the first num_packets.
  // The remaining bytes should be zeros.
  CompareAudioBufferOptions opts;
  opts.num_frames_per_packet = kPacketFrames;
  opts.test_label = "check data";
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, 0, num_frames),
                      AudioBufferSlice(&input_buffer, 0, num_frames), opts);
  opts.test_label = "check silence";
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, num_frames, output_->frame_count()),
                      AudioBufferSlice<ASF::SIGNED_16>(), opts);
}

// If we issue DiscardAllPackets during Playback, PTS should not change.
TEST_F(AudioRendererPipelineTestInt16, DiscardDuringPlayback) {
  auto min_lead_time = renderer_->GetMinLeadTime();
  // Add extra packets to allow for scheduling delay to reduce flakes in debug mode. See fxb/52410.
  constexpr auto kSchedulingDelayInPackets = 10;
  const auto min_lead_time_in_packets =
      (min_lead_time / zx::msec(RendererShimImpl::kPacketMs)) + kSchedulingDelayInPackets;

  // This test writes to the ring buffer as follows:
  //
  // 1. The first step starts writing num_packets to the front of the ring buffer, but
  //    interrupts and discards after two packets have been written. Because of races,
  //    it's possible that more than two packets will have been written at the moment
  //    the remaining packets are discarded.
  //
  //     +---+---+ ...           +
  //     | P | P | maybe empty   |
  //     +---+---+ ...           +
  //
  //     ^..... num_packets .....^
  //
  // 2. The second step writes another num_packets, starting at least min_lead_time after
  //    the second packet:
  //
  //     +---+---+ ...           +---+ ...               +
  //     | P | P | maybe empty   | P | ...               |
  //     +---+---+ ...           +---+ ...               +
  //
  //             ^ min_lead_time ^
  //             +kSchedulingDelay
  //
  //     ^..... num_packets .....^..... num_packets .....^
  //
  // Note that 1 PTS == 1 frame.
  // To further simplify, all of the above sizes are integer numbers of packets.
  const int64_t first_packet = 0;
  const int64_t restart_packet = 2 + min_lead_time_in_packets;
  const int64_t first_pts = first_packet * kPacketFrames;
  const int64_t restart_pts = restart_packet * kPacketFrames;
  const auto num_packets = NumPacketsPerBatch();
  const size_t num_frames = num_packets * kPacketFrames;

  // Load the renderer with lots of packets, but interrupt after two of them.
  auto first_input = GenerateSequentialAudio<ASF::SIGNED_16>(format_, num_frames);
  auto first_packets = renderer_->AppendPackets({&first_input}, first_pts);
  renderer_->PlaySynchronized(this, output_, 0);
  renderer_->WaitForPackets(this, {first_packets[0], first_packets[1]});

  renderer_->renderer()->DiscardAllPackets(AddCallback(
      "DiscardAllPackets", []() { AUDIO_LOG(DEBUG) << "DiscardAllPackets #1 complete"; }));
  ExpectCallback();

  // The entire first two packets must have been written. Subsequent packets may have been partially
  // written, depending on exactly when the DiscardAllPackets command is received. The remaining
  // bytes should be zeros.
  auto ring_buffer = output_->SnapshotRingBuffer();
  CompareAudioBufferOptions opts;
  opts.num_frames_per_packet = kPacketFrames;
  opts.test_label = "first_input, first packet";
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, 0, 2 * kPacketFrames),
                      AudioBufferSlice(&first_input, 0, 2 * kPacketFrames), opts);
  opts.test_label = "first_input, third packet onwards";
  opts.partial = true;
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, 2 * kPacketFrames, output_->frame_count()),
                      AudioBufferSlice(&first_input, 2 * kPacketFrames, output_->frame_count()),
                      opts);

  opts.partial = false;
  renderer_->ClearPayload();

  // After interrupting the stream without stopping, now play another sequence of packets starting
  // at least "min_lead_time" after the last audio frame previously written to the ring buffer.
  // Between Left|Right, initial data values were odd|even; these are even|odd, for quick contrast
  // when visually inspecting the buffer.
  const int16_t restart_data_value = 0x4000;
  auto second_input =
      GenerateSequentialAudio<ASF::SIGNED_16>(format_, num_frames, restart_data_value);
  auto second_packets = renderer_->AppendPackets({&second_input}, restart_pts);
  renderer_->WaitForPackets(this, second_packets);

  // The ring buffer should contain first_input for 10ms (one packet), then partially-written data
  // followed by zeros until restart_pts, then second_input (num_packets), then the remaining bytes
  // should be zeros.
  ring_buffer = output_->SnapshotRingBuffer();

  opts.test_label = "first packet, after the second write";
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, 0, 2 * kPacketFrames),
                      AudioBufferSlice(&first_input, 0, 2 * kPacketFrames), opts);

  opts.test_label = "space between the first packet and second_input";
  opts.partial = true;
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, 2 * kPacketFrames, restart_pts),
                      AudioBufferSlice(&first_input, 2 * kPacketFrames, restart_pts), opts);

  opts.test_label = "second_input";
  opts.partial = false;
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, restart_pts, restart_pts + num_frames),
                      AudioBufferSlice(&second_input, 0, num_frames), opts);

  opts.test_label = "silence after second_input";
  CompareAudioBuffers(
      AudioBufferSlice(&ring_buffer, restart_pts + num_frames, output_->frame_count()),
      AudioBufferSlice<ASF::SIGNED_16>(), opts);
}

// During playback, gain changes should be ramped.
TEST_F(AudioRendererPipelineTestInt16, RampOnGainChanges) {
  fuchsia::media::audio::VolumeControlPtr volume;
  audio_core_->BindUsageVolumeControl(
      fuchsia::media::Usage::WithRenderUsage(AudioRenderUsage::MEDIA), volume.NewRequest());
  volume->SetVolume(0.5);

  auto num_packets = kNumPacketsInPayload;
  auto num_frames = num_packets * kPacketFrames;

  const int16_t kSampleFullVolume = 0x0200;
  const int16_t kSampleHalfVolume = 0x0010;

  auto input_buffer = GenerateConstantAudio<ASF::SIGNED_16>(format_, num_frames, kSampleFullVolume);
  auto packets = renderer_->AppendPackets({&input_buffer});
  auto start_time = renderer_->PlaySynchronized(this, output_, 0);

  // Wait until a few packets are rendered, then raise the volume to 1.0.
  auto start_delay = zx::time(start_time) - zx::clock::get_monotonic();
  RunLoopWithTimeout(start_delay + zx::msec((num_packets / 2) * RendererShimImpl::kPacketMs));
  volume->SetVolume(1.0);

  // Now wait for all packets to be rendered.
  renderer_->WaitForPackets(this, packets);
  auto ring_buffer = output_->SnapshotRingBuffer();

  // The output should contain a sequence at half volume, followed by a ramp,
  // followed by a sequence at full volume. Verify that the length of the ramp
  // matches the expected ramp duration.
  size_t start = ring_buffer.NumFrames() - 1;
  for (;; start--) {
    if (ring_buffer.SampleAt(start, 0) == kSampleHalfVolume) {
      break;
    }
    if (start == 0) {
      ADD_FAILURE() << "could not find half volume sample 0x" << std::hex << kSampleHalfVolume;
      ring_buffer.Display(0, 3 * kPacketFrames);
      return;
    }
  }
  size_t end = start + 1;
  for (;; end++) {
    if (ring_buffer.SampleAt(end, 0) == kSampleFullVolume) {
      break;
    }
    if (end == ring_buffer.NumFrames() - 1) {
      ADD_FAILURE() << "could not find full volume sample 0x" << std::hex << kSampleFullVolume
                    << " after frame " << std::dec << start;
      ring_buffer.Display(start, kPacketFrames);
      return;
    }
  }

  // The exact length can be off by a fractional frame due to rounding.
  const auto ns_per_frame = format_.frames_per_ns().Inverse();
  const auto dt = zx::nsec(ns_per_frame.Scale(end - start));
  const auto tol = zx::nsec(ns_per_frame.Scale(1));
  EXPECT_NEAR(kVolumeRampDuration.get(), dt.get(), tol.get())
      << "ramp has length " << (end - start) << " frames, from frame " << start << " to " << end;
}

// During playback, gain changes should not introduce high-frequency distortion.
TEST_F(AudioRendererPipelineTestFloat, NoDistortionOnGainChanges) {
  fuchsia::media::audio::VolumeControlPtr volume;
  audio_core_->BindUsageVolumeControl(
      fuchsia::media::Usage::WithRenderUsage(AudioRenderUsage::MEDIA), volume.NewRequest());
  volume->SetVolume(0.5);

  size_t num_frames = kPayloadFramesPow2;

  // At 48kHz, this is 5.33ms per sinusoidal period. This is chosen intentionally to
  // (a) not align with volume updates, which happen every 10ms, and (b) include a
  // power-of-2 number of frames, to simplify the FFT comparison.
  const size_t kFramesPerPeriod = 256;
  const size_t freq = num_frames / kFramesPerPeriod;
  auto input_buffer = GenerateCosineAudio(format_, num_frames, freq);
  auto packets = renderer_->AppendPackets({&input_buffer});
  auto start_time = renderer_->PlaySynchronized(this, output_, 0);

  // Wait until the first packet will be rendered, then make a few gain toggles.
  RunLoopWithTimeout(zx::time(start_time) - zx::clock::get_monotonic());
  for (size_t k = 0; k < num_frames / kPacketFrames; k++) {
    volume->SetVolume((k % 2) == 0 ? 1.0 : 0.5);
    RunLoopWithTimeout(zx::msec(RendererShimImpl::kPacketMs));
  }

  // Now wait for all packets to be rendered.
  renderer_->WaitForPackets(this, packets);
  auto ring_buffer = output_->SnapshotRingBuffer();
  auto output_buffer = AudioBufferSlice(&ring_buffer, 0, input_buffer.NumFrames()).GetChannel(0);

  // If we properly ramp gain changes, there should not be very much high-frequency noise.
  // For the purpose of this test, we'll define "high-frequency" to be anything at least 4
  // octaves above the base frequency.
  //
  // The precise amount of noise depends on exactly when the gain toggles are applied,
  // which is not deterministic. The noise signature also depends on the length and shape
  // of the gain ramp -- any intentional ramping change may break this test.
  //
  // As of early Aug 2020, typical noise_ratio values are:
  // * 0.05-0.07 without ramping
  // * 0.001-0.015 with ramping
  std::unordered_set<size_t> highfreqs;
  for (size_t f = freq << 4; f < output_buffer.NumFrames() / 2; f++) {
    highfreqs.insert(f);
  }
  auto result = MeasureAudioFreqs(AudioBufferSlice(&output_buffer), highfreqs);
  auto noise_ratio = result.total_magn_signal / result.total_magn_other;
  EXPECT_LT(noise_ratio, 0.02) << "\ntotal_magn_highfreq_noise = " << result.total_magn_signal
                               << "\ntotal_magn_other = " << result.total_magn_other;
}

class AudioRendererPipelineUnderflowTest : public HermeticAudioTest {
 protected:
  static void SetUpTestSuite() {
    HermeticAudioTest::SetUpTestSuiteWithOptions(HermeticAudioEnvironment::Options{
        .audio_core_base_url = "fuchsia-pkg://fuchsia.com/audio-core-with-test-effects",
        .audio_core_config_data_path = "/pkg/data/audio-core-config-with-sleeper-filter",
    });
  }

  AudioRendererPipelineUnderflowTest()
      : format_(Format::Create<ASF::SIGNED_16>(2, kFrameRate).value()) {}

  void SetUp() override {
    HermeticAudioTest::SetUp();
    // The output can store exactly 1s of audio data.
    output_ = CreateOutput({{0xff, 0x00}}, format_, 48000);
    renderer_ = CreateAudioRenderer(format_, kPayloadFrames);
  }

  const TypedFormat<ASF::SIGNED_16> format_;
  VirtualOutput<ASF::SIGNED_16>* output_ = nullptr;
  AudioRendererShim<ASF::SIGNED_16>* renderer_ = nullptr;
};

// Validate that a slow effects pipeline registers an underflow.
TEST_F(AudioRendererPipelineUnderflowTest, HasUnderflow) {
  // Inject one packet and wait for it to be rendered.
  auto input_buffer = GenerateSequentialAudio<ASF::SIGNED_16>(format_, kPacketFrames);
  auto packets = renderer_->AppendPackets({&input_buffer});
  renderer_->PlaySynchronized(this, output_, 0);
  renderer_->WaitForPackets(this, packets);

  // Wait an extra 20ms to account for the sleeper filter's delay.
  RunLoopWithTimeout(zx::msec(20));

  // Expect an underflow.
  output_->expected_inspect_properties().children["pipeline underflows"].ExpectUintNonzero("count");
}

class AudioRendererPipelineEffectsTest : public AudioRendererPipelineTestInt16 {
 protected:
  // Matches the value in audio_core_config_with_inversion_filter.json
  static constexpr const char* kInverterEffectName = "inverter";

  static void SetUpTestSuite() {
    HermeticAudioTest::SetUpTestSuiteWithOptions(HermeticAudioEnvironment::Options{
        .audio_core_base_url = "fuchsia-pkg://fuchsia.com/audio-core-with-test-effects",
        .audio_core_config_data_path = "/pkg/data/audio-core-config-with-inversion-filter",
    });
  }

  void SetUp() override {
    AudioRendererPipelineTestInt16::SetUp();
    environment()->ConnectToService(effects_controller_.NewRequest());
  }

  void RunInversionFilter(AudioBuffer<ASF::SIGNED_16>* audio_buffer_ptr) {
    auto& samples = audio_buffer_ptr->samples();
    for (size_t sample = 0; sample < samples.size(); sample++) {
      samples[sample] = -samples[sample];
    }
  }

  fuchsia::media::audio::EffectsControllerSyncPtr effects_controller_;
};

// Validate that the effects package is loaded and that it processes the input.
TEST_F(AudioRendererPipelineEffectsTest, RenderWithEffects) {
  const auto num_packets = NumPacketsPerBatch();
  const auto num_frames = num_packets * kPacketFrames;

  auto input_buffer = GenerateSequentialAudio<ASF::SIGNED_16>(format_, num_frames);
  auto packets = renderer_->AppendPackets({&input_buffer});
  renderer_->PlaySynchronized(this, output_, 0);
  renderer_->WaitForPackets(this, packets);
  auto ring_buffer = output_->SnapshotRingBuffer();

  // Simulate running the effect on the input buffer.
  RunInversionFilter(&input_buffer);

  // The ring buffer should match the transformed input buffer for the first num_packets.
  // The remaining bytes should be zeros.
  CompareAudioBufferOptions opts;
  opts.num_frames_per_packet = kPacketFrames;
  opts.test_label = "check data";
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, 0, num_frames),
                      AudioBufferSlice(&input_buffer, 0, num_frames), opts);
  opts.test_label = "check silence";
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, num_frames, output_->frame_count()),
                      AudioBufferSlice<ASF::SIGNED_16>(), opts);
}

TEST_F(AudioRendererPipelineEffectsTest, EffectsControllerEffectDoesNotExist) {
  fuchsia::media::audio::EffectsController_UpdateEffect_Result result;
  zx_status_t status = effects_controller_->UpdateEffect("invalid_effect_name", "disable", &result);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_TRUE(result.is_err());
  EXPECT_EQ(result.err(), fuchsia::media::audio::UpdateEffectError::NOT_FOUND);
}

TEST_F(AudioRendererPipelineEffectsTest, EffectsControllerInvalidConfig) {
  fuchsia::media::audio::EffectsController_UpdateEffect_Result result;
  zx_status_t status =
      effects_controller_->UpdateEffect(kInverterEffectName, "invalid config string", &result);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_TRUE(result.is_err());
  EXPECT_EQ(result.err(), fuchsia::media::audio::UpdateEffectError::INVALID_CONFIG);
}

// Similar to RenderWithEffects, except we send a message to the effect to ask it to disable
// processing.
TEST_F(AudioRendererPipelineEffectsTest, EffectsControllerUpdateEffect) {
  // Disable the inverter; frames should be unmodified.
  fuchsia::media::audio::EffectsController_UpdateEffect_Result result;
  zx_status_t status = effects_controller_->UpdateEffect(kInverterEffectName, "disable", &result);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_TRUE(result.is_response());

  const auto num_packets = NumPacketsPerBatch();
  const auto num_frames = num_packets * kPacketFrames;

  auto input_buffer = GenerateSequentialAudio<ASF::SIGNED_16>(format_, num_frames);
  auto packets = renderer_->AppendPackets({&input_buffer});
  renderer_->PlaySynchronized(this, output_, 0);
  renderer_->WaitForPackets(this, packets);
  auto ring_buffer = output_->SnapshotRingBuffer();

  // The ring buffer should match the input buffer for the first num_packets. The remaining bytes
  // should be zeros.
  CompareAudioBufferOptions opts;
  opts.num_frames_per_packet = kPacketFrames;
  opts.test_label = "check data";
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, 0, num_frames),
                      AudioBufferSlice(&input_buffer, 0, num_frames), opts);
  opts.test_label = "check silence";
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, num_frames, output_->frame_count()),
                      AudioBufferSlice<ASF::SIGNED_16>(), opts);
}

class AudioRendererPipelineTuningTest : public AudioRendererPipelineTestInt16 {
 protected:
  // Matches the value in audio_core_config_with_inversion_filter.json
  static constexpr const char* kInverterEffectName = "inverter";

  static void SetUpTestSuite() {
    HermeticAudioTest::SetUpTestSuiteWithOptions(HermeticAudioEnvironment::Options{
        .audio_core_base_url = "fuchsia-pkg://fuchsia.com/audio-core-with-test-effects",
        .audio_core_config_data_path = "/pkg/data/audio-core-config-with-inversion-filter",
    });
  }

  void SetUp() override {
    AudioRendererPipelineTestInt16::SetUp();
    environment()->ConnectToService(audio_tuner_.NewRequest());
  }

  void RunInversionFilter(AudioBuffer<ASF::SIGNED_16>* audio_buffer_ptr) {
    auto& samples = audio_buffer_ptr->samples();
    for (size_t sample = 0; sample < samples.size(); sample++) {
      samples[sample] = -samples[sample];
    }
  }

  fuchsia::media::tuning::AudioTunerPtr audio_tuner_;
};

// Verify the correct output is received before and after update of the OutputPipeline.
//
// AudioCore is launched with a default profile containing an inversion_filter effect; a renderer
// plays a packet, and the output is verified as inverted. Then, the AudioTuner service is used to
// update the OutputPipeline with a PipelineConfig containing a disabled inversion_filter effect. A
// second packet is played, and the output is verified as having no effects applied.
TEST_F(AudioRendererPipelineTuningTest, CorrectStreamOutputUponUpdatedPipeline) {
  // Setup packet details.
  auto num_packets = 1;
  auto num_frames = num_packets * kPacketFrames;

  // Initiate stream with first packets and send through default OutputPipeline, which has an
  // inversion_filter effect enabled.
  auto first_buffer = GenerateSequentialAudio<ASF::SIGNED_16>(format_, num_frames);
  auto first_packets = renderer_->AppendPackets({&first_buffer});
  renderer_->PlaySynchronized(this, output_, 0);
  renderer_->WaitForPackets(this, first_packets);
  auto ring_buffer = output_->SnapshotRingBuffer();

  // Prepare first buffer for comparison to expected ring buffer.
  RunInversionFilter(&first_buffer);

  CompareAudioBufferOptions opts;
  opts.num_frames_per_packet = kPacketFrames;
  opts.test_label = "default config, first packet";
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, 0, num_frames),
                      AudioBufferSlice(&first_buffer), opts);

  // Clear payload to avoid overlap of values from old OutputPipeline ringout with values from new
  // OutputPipeline.
  renderer_->ClearPayload();

  // Setup new output pipeline details.
  auto device_id = AudioDevice::UniqueIdToString({{0xff, 0x00}});
  PipelineConfig::MixGroup root{.name = "linearize",
                                .input_streams =
                                    {
                                        RenderUsage::MEDIA,
                                        RenderUsage::SYSTEM_AGENT,
                                        RenderUsage::INTERRUPTION,
                                        RenderUsage::COMMUNICATION,
                                    },
                                .effects = {
                                    {
                                        .lib_name = "inversion_filter.so",
                                        .effect_name = "inversion_filter",
                                        .instance_name = "inverter",
                                        .effect_config = "disable",
                                    },
                                }};
  auto pipeline_config = PipelineConfig(root);
  auto volume_curve = VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume);
  auto device_profile_with_inversion_effect =
      ToAudioDeviceTuningProfile(pipeline_config, volume_curve);

  // Update PipelineConfig through AudioTuner service.
  audio_tuner_->SetAudioDeviceProfile(
      device_id, std::move(device_profile_with_inversion_effect),
      AddCallback("SetAudioDeviceProfile", [](zx_status_t status) { EXPECT_EQ(status, ZX_OK); }));

  ExpectCallback();

  // Send second set of packets through new OutputPipeline (with inversion effect disabled); play
  // packets at least "min_lead_time" after the last audio frame previously written to the ring
  // buffer.
  auto min_lead_time = renderer_->GetMinLeadTime();
  // Add extra packets to allow for scheduling delay to reduce flakes in debug mode. See fxb/52410.
  constexpr auto kSchedulingDelayInPackets = 10;
  const auto min_lead_time_in_packets =
      (min_lead_time / zx::msec(RendererShimImpl::kPacketMs)) + kSchedulingDelayInPackets;
  const int64_t restart_packet = 2 + min_lead_time_in_packets;
  const int64_t restart_pts = restart_packet * kPacketFrames;

  auto second_buffer = GenerateSequentialAudio<ASF::SIGNED_16>(format_, num_frames);
  auto second_packets = renderer_->AppendPackets({&second_buffer}, restart_pts);
  renderer_->WaitForPackets(this, second_packets);
  ring_buffer = output_->SnapshotRingBuffer();

  // Verify the remaining packets have gone through the updated OutputPipeline and thus been
  // unmodified, due to the inversion_filter being disabled in the new configuration.
  opts.num_frames_per_packet = kPacketFrames;
  opts.test_label = "updated config, remaining packets";
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, restart_pts, restart_pts + num_frames),
                      AudioBufferSlice(&second_buffer), opts);
}

// Verify the correct output is received after update of the specified effect config.
//
// AudioCore is launched with a default profile containing an inversion_filter effect. The
// AudioTuner service is used to update a specified effect instance's effect configuration, which
// disables the inversion_filter effect present in the default profile. A packet is played, and the
// output is verified as having the inversion_filter effect disabled (no effects applied).
TEST_F(AudioRendererPipelineTuningTest, AudioTunerUpdateEffect) {
  // Disable the inverter; frames should be unmodified.
  auto device_id = AudioDevice::UniqueIdToString({{0xff, 0x00}});
  fuchsia::media::tuning::AudioEffectConfig updated_effect;
  updated_effect.set_instance_name(kInverterEffectName);
  updated_effect.set_configuration("disable");
  audio_tuner_->SetAudioEffectConfig(
      device_id, std::move(updated_effect),
      AddCallback("SetAudioEffectConfig", [](zx_status_t status) { EXPECT_EQ(status, ZX_OK); }));

  ExpectCallback();

  auto min_lead_time = renderer_->GetMinLeadTime();
  auto num_packets = min_lead_time / zx::msec(RendererShimImpl::kPacketMs);
  auto num_frames = num_packets * kPacketFrames;

  auto input_buffer = GenerateSequentialAudio<ASF::SIGNED_16>(format_, num_frames);
  auto packets = renderer_->AppendPackets({&input_buffer});
  renderer_->PlaySynchronized(this, output_, 0);
  renderer_->WaitForPackets(this, packets);
  auto ring_buffer = output_->SnapshotRingBuffer();

  // The ring buffer should match the input buffer for the first num_packets. The remaining bytes
  // should be zeros.
  CompareAudioBufferOptions opts;
  opts.num_frames_per_packet = kPacketFrames;
  opts.test_label = "check data";
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, 0, num_frames),
                      AudioBufferSlice(&input_buffer, 0, num_frames), opts);
  opts.test_label = "check silence";
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, num_frames, output_->frame_count()),
                      AudioBufferSlice<ASF::SIGNED_16>(), opts);
}

// /// Overall, need to add tests to validate various Renderer pipeline aspects
// TODO(mpuryear): validate the combinations of NO_TIMESTAMP (Play ref_time,
//     Play media_time, packet PTS)
// TODO(mpuryear): validate gain and ramping
// TODO(mpuryear): validate frame-rate, and fractional position
// TODO(mpuryear): validate channelization (future)
// TODO(mpuryear): validate sample format
// TODO(mpuryear): validate timing/sequence/latency of all callbacks
// TODO(mpuryear): validate various permutations of PtsUnits. Ref clocks?
// TODO(mpuryear): handle EndOfStream?
// TODO(mpuryear): test >1 payload buffer
// TODO(mpuryear): test late packets (no timestamps), gap-then-signal at driver.
//     Should include various permutations of MinLeadTime, ContinuityThreshold
// TODO(mpuryear): test packets with timestamps already played -- expect
//     truncated-signal at driver
// TODO(mpuryear): test packets with timestamps too late -- expect Renderer
//     gap-then-truncated-signal at driver
// TODO(mpuryear): test that no data is lost when Renderer Play-Pause-Play

}  // namespace media::audio::test

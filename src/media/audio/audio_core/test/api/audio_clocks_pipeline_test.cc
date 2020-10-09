// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/syscalls/clock.h>
#include <zircon/types.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <vector>

#include <fbl/algorithm.h>
#include <gmock/gmock.h>

#include "src/media/audio/audio_core/mixer/gain.h"
#include "src/media/audio/audio_core/mixer/sinc_sampler.h"
#include "src/media/audio/lib/analysis/analysis.h"
#include "src/media/audio/lib/analysis/generators.h"
#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/clock/utils.h"
#include "src/media/audio/lib/logging/logging.h"
#include "src/media/audio/lib/test/comparators.h"
#include "src/media/audio/lib/test/hermetic_audio_test.h"

using ASF = fuchsia::media::AudioSampleFormat;

namespace media::audio::test {

class MicroSrcPipelineTest : public HermeticAudioTest {
 protected:
  static constexpr size_t kFrameRate = 96000;
  static constexpr size_t kPayloadFrames = 2 * kFrameRate;  // 2s
  static constexpr size_t kPacketFrames = kFrameRate / 1000 * RendererShimImpl::kPacketMs;

  // Expected MicroSRC convergence time, in frames: about 15 mix periods at 10ms per period.
  static constexpr size_t kConvergenceFrames = 15 * 960;

  MicroSrcPipelineTest() : format_(Format::Create<ASF::FLOAT>(1, kFrameRate).value()) {}

  void TearDown() {
    ExpectNoOverflowsOrUnderflows();
    HermeticAudioTest::TearDown();
  }

  void Init(int64_t slew_ppm) {
    zx::clock ref_clock = ::media::audio::clock::AdjustableCloneOfMonotonic();

    zx::clock::update_args args;
    args.reset().set_rate_adjust(slew_ppm);
    auto status = ref_clock.update(args);
    FX_CHECK(status == ZX_OK) << status;

    // Now that the clock is adjusted, remove ZX_RIGHT_WRITE so audio_core cannot adjust the clock.
    // This forces audio_core to use MicroSRC instead of the Flexible clock scheme.
    ref_clock = audio::clock::DuplicateClock(ref_clock).take_value();

    // Buffer up to 2s of data.
    output_ = CreateOutput({{0xff, 0x00}}, format_, kPayloadFrames);
    renderer_ = CreateAudioRenderer(format_, kPayloadFrames,
                                    fuchsia::media::AudioRenderUsage::MEDIA, std::move(ref_clock));
  }

  AudioBuffer<ASF::FLOAT> Impulse() {
    AudioBuffer<ASF::FLOAT> out(format_, 1);
    out.samples()[0] = 0.5;
    return out;
  }

  AudioBuffer<ASF::FLOAT> FillBuffer(size_t frames) {
    AudioBuffer<ASF::FLOAT> out(format_, frames);
    for (size_t s = 0; s < out.NumSamples(); s++) {
      out.samples()[s] = 0.5;
    }
    return out;
  }

  std::set<size_t> NumFramesOutput(int64_t clock_slew_ppm, size_t num_frames_input) {
    std::set<size_t> out;
    double out_over_in = 1e6 / (1e6 + static_cast<double>(clock_slew_ppm));
    double num_frames_output = out_over_in * static_cast<double>(num_frames_input);
    out.insert(static_cast<size_t>(floor(num_frames_output)));
    out.insert(static_cast<size_t>(ceil(num_frames_output)));
    return out;
  }

  // Maximum ring in/out frames. We're not using any effects for this test, so ring in/out comes
  // from the SincSampler. Note, this is just one side of ring in/out.
  size_t RingInOut() {
    auto mixer = mixer::SincSampler::Select(format_.stream_type(), format_.stream_type());
    return mixer->neg_filter_width().Ceiling();
  }

  // Offset of the first audio sample. This should be greater than RingInOut so that any ring in
  // frames appear near the start of the output ring buffer, following a short bout of silence.
  size_t OffsetFrames() {
    constexpr size_t kFramesOfSilence = 20;
    return RingInOut() + kFramesOfSilence;
  }

  // Return the index of the peak sample, relative to the first frame in the slice.
  size_t FindPeak(AudioBufferSlice<ASF::FLOAT> slice) {
    FX_CHECK(slice.format().channels() == 1);
    float peak_val = 0;
    size_t peak_idx = 0;
    for (size_t f = 1; f < slice.NumFrames(); f++) {
      if (auto s = std::abs(slice.SampleAt(f, 0)); s > peak_val) {
        peak_val = s;
        peak_idx = f;
      }
    }
    return peak_idx;
  }

  // Send two impulses separated by num_frames_input, using a reference clock with the given slew.
  // The output should contain two impulses separated by NumFramesOutput.
  //
  // This test validates that time is correctly translated between the two clocks.
  void RunImpulseTest(int64_t clock_slew_ppm, size_t num_frames_input) {
    Init(clock_slew_ppm);

    auto impulse = Impulse();

    // Since this is a precise timing test, the clocks need to converge before we start.
    auto offset_frames = std::max(OffsetFrames(), kConvergenceFrames);
    FX_CHECK(num_frames_input + offset_frames < kPayloadFrames);

    // Play two impulses num_frames_input apart.
    auto first_input = renderer_->AppendPackets({&impulse}, offset_frames);
    auto second_input = renderer_->AppendPackets({&impulse}, offset_frames + num_frames_input);
    renderer_->PlaySynchronized(this, output_, 0);
    renderer_->WaitForPackets(this, first_input);
    renderer_->WaitForPackets(this, second_input);
    auto ring_buffer = output_->SnapshotRingBuffer();

    // There should be exactly two impulses.
    auto midpoint = offset_frames + num_frames_input / 2;
    auto first_peak = FindPeak(AudioBufferSlice(&ring_buffer, 0, midpoint));
    auto second_peak = FindPeak(AudioBufferSlice(&ring_buffer, midpoint, ring_buffer.NumFrames()));
    second_peak += midpoint;

    // The distance between the two impulses should be num_frames_output.
    auto num_frames_output = NumFramesOutput(clock_slew_ppm, num_frames_input);
    EXPECT_THAT(num_frames_output,
                ::testing::Contains(static_cast<size_t>(second_peak - first_peak)))
        << "\nimpulse[0] peaked at frame " << first_peak << "\nimpulse[1] peaked at frame "
        << second_peak;
  }

  // Send a flat signal (step function) of size num_frames_input, using a reference clock with the
  // given slew. The output should contain an equivalent step function of size NumFramesOutput.
  //
  // Note, the exact values are not important. The primary goal of this test is to ensure the output
  // does not have any dropped frames. A buggy mixer might drop frames if there is a gap between mix
  // calls, especially when the output clock is running faster than the input clock.
  void RunStepFnTest(int64_t clock_slew_ppm, size_t num_frames_input) {
    FX_CHECK(num_frames_input + OffsetFrames() < kPayloadFrames);
    Init(clock_slew_ppm);

    auto input = FillBuffer(num_frames_input);
    auto packets = renderer_->AppendPackets({&input}, OffsetFrames());
    renderer_->PlaySynchronized(this, output_, 0);
    renderer_->WaitForPackets(this, packets);
    auto ring_buffer = output_->SnapshotRingBuffer();

    // NumFramesOutput returns two values when the expected value is fractional. Pick the first
    // (lowest) value, then increase ring in/out by one to account for the (possible) extra
    // fractional frame.
    size_t num_frames_output = *NumFramesOutput(clock_slew_ppm, num_frames_input).begin();
    size_t max_ring_in = RingInOut() + 1;
    size_t max_ring_out = RingInOut() + 1;
    auto expected_output = FillBuffer(num_frames_output);

    auto first_silence_end = OffsetFrames() - max_ring_in;
    auto data_start = OffsetFrames() + max_ring_out;
    auto data_end = OffsetFrames() + num_frames_output - max_ring_in;
    auto second_silence_start = OffsetFrames() + num_frames_output + max_ring_out;

    // The output should contain silence, followed by optional ring in, followed by data, followed
    // by optional ring out, followed again by silence. Ultimately we're testing that we emit the
    // correct number of output frames. Because it's difficult to know the exact size of ring in/out
    // without running the sampler, our test is necessarily imprecise. To illustrate:
    //
    //     max ring in                      max ring out
    //     V                                V     |
    //   +-----+--------------------------+-----+ V
    //         +--------------------------+--+-----+
    //             ^                       ^
    //             num_frames_input        num_frames_output - num_frames_input
    //
    // In this case, we expect more output frames than input frames. However, since the delta
    // is smaller than the maximum ring out, we cannot be sure if the extra frames are output
    // or ring out. This means we cannot check if the system operated correctly.
    //
    // To address this problem, the diff between intput and output frames must be greater than
    // the total number of ring in + ring out frames.
    //
    // Note, we're not attempting to precisely measure the duration of the output step function.
    // We are drawing conservative boundaries around the output step function, then verifying
    // that there are no dropped frames within those boundaries.
    FX_CHECK(std::abs(static_cast<ssize_t>(num_frames_input - num_frames_output)) >
             static_cast<ssize_t>(max_ring_in + max_ring_out));

    auto silence1 = AudioBufferSlice(&ring_buffer, 0, first_silence_end);
    auto data = AudioBufferSlice(&ring_buffer, data_start, data_end);
    auto silence2 = AudioBufferSlice(&ring_buffer, second_silence_start, output_->frame_count());

    ExpectAudioBufferOptions opts;
    opts.num_frames_per_packet = kPacketFrames;
    opts.test_label = fxl::StringPrintf("check silence (frames 0 to %lu)", first_silence_end);
    ExpectSilentAudioBuffer(silence1, opts);

    opts.test_label = fxl::StringPrintf("check data (frames %lu to %lu)", data_start, data_end);
    ExpectNonSilentAudioBuffer(data, opts);

    opts.test_label = fxl::StringPrintf("check silence (frames %lu to %lu)", second_silence_start,
                                        output_->frame_count());
    ExpectSilentAudioBuffer(silence2, opts);
  }

  // Send a sine wave using a reference clock with the given slew. The output should
  // contain a sine wave with a slewed frequency. Each full sinusoidal period contains
  // (num_frames_input / input_freq) frames.
  //
  // This test validates that frequencies are correctly translated in the output audio.
  void RunSineWaveTest(int64_t clock_slew_ppm, size_t num_frames_input, size_t input_freq) {
    FX_CHECK(fbl::is_pow2(num_frames_input));
    FX_CHECK(num_frames_input + OffsetFrames() < kPayloadFrames);
    Init(clock_slew_ppm);

    // If the input clock is fast, we'll complete before writing a full num_frames_input
    // into the ring buffer. To ensure we have at least num_frames_input in the output buffer,
    // repeat prefix of the input. Also ensure we don't overrun the output ring buffer.
    auto input = GenerateCosineAudio(format_, num_frames_input, input_freq);
    auto input_prefix = AudioBufferSlice(&input, 0, (kPayloadFrames - num_frames_input) / 2);
    auto packets = renderer_->AppendPackets({&input, input_prefix}, 0);
    renderer_->PlaySynchronized(this, output_, 0);
    renderer_->WaitForPackets(this, packets);

    auto ring_buffer = output_->SnapshotRingBuffer();
    auto output_buffer = AudioBufferSlice(&ring_buffer, 0, num_frames_input);

    // We are given an input sine wave has an integer number of periods within num_frames_input.
    // Since the output is slewed, the wave is stretched or shrunk such that it will no longer
    // have an integer number of periods. This leads to discontinuities at the edge, which
    // introduces noise into the FFT. To reduce that noise, we apply a windowing function.
    auto windowed_output = MultiplyByTukeyWindow(output_buffer, 0.2);

    // Compute the slewed frequency in the output.
    double out_over_in = 1e6 / (1e6 + static_cast<double>(clock_slew_ppm));
    double num_frames_output = out_over_in * static_cast<double>(num_frames_input);
    size_t output_freq =
        static_cast<double>(input_freq) * static_cast<double>(num_frames_input) / num_frames_output;

    // Since our mixer uses a PID to follow the input clock, it may be a little fast or
    // slow, resulting in a cluster of sound around the expected output frequency.
    auto result = MeasureAudioFreq(AudioBufferSlice(&windowed_output), output_freq);

    // Ensure the FFT has a peak centered on f.
    double peak_m = 0;
    size_t peak_f = 0;
    for (size_t f = 0; f < result.all_square_magnitudes.size(); f++) {
      if (auto m = sqrt(result.all_square_magnitudes[f]); m > peak_m) {
        peak_m = m;
        peak_f = f;
      }
    }
    EXPECT_EQ(peak_f, output_freq) << "magnitude at peak_f = " << peak_m;

    // Input peak magnitude is 1.0. This is distributed to the sides of the volcano,
    // but should remain high.
    EXPECT_GT(peak_m, 0.5);

    // Check the width of the peak "volcano". During this period, the FFT rapidly
    // ramps up from the noise floor to the peak, followed by a rapid decrease back
    // down to the noise floor. The noise floor is set to -50dB, somewhat arbitrarily.
    //
    // Ideally the "volcano" would be a single point. However, our sine waves don't
    // perfectly fit into a power-of-2 length signal, so we need to use a windowing
    // function on the signal. This gives the FFT a volcano shape.
    //
    // The range [peak_start, peak_end] is the smallest range surrounding output_freq such
    // that none of the frequences outside of that range have a gain above the noise floor.
    const double kNoiseFloor = Gain::DbToScale(-50);
    size_t peak_start = output_freq;
    for (ssize_t f = output_freq; f >= 0; f--) {
      auto m = sqrt(result.all_square_magnitudes[f]);
      if (m > kNoiseFloor) {
        peak_start = f;
      }
    }
    size_t peak_end = output_freq;
    for (size_t f = output_freq; f < result.all_square_magnitudes.size(); f++) {
      auto m = sqrt(result.all_square_magnitudes[f]);
      if (m > kNoiseFloor) {
        peak_end = f;
      }
    }

    // The volcano peak should span less than 0.1% of the total frequency window.
    // Given a sample rate of 96kHz, this is a window of +/- 48Hz around the signal.
    //
    // How this was derived: We computed the peak_width for pure sine wave at the
    // expected output frequencies, as shown below, then rounded up to add slack.
    //
    //   - For 19990 periods in 131072 samples (14641 Hz): ~18 bins
    //   - For 20010 periods in 131072 samples (14655 Hz): ~18 bins
    //
    constexpr size_t kMaxPeakWidth = 40;
    EXPECT_LT(peak_end - peak_start, kMaxPeakWidth) << "FFT peak width is too long";
  }

  const TypedFormat<ASF::FLOAT> format_;
  VirtualOutput<ASF::FLOAT>* output_ = nullptr;
  AudioRendererShim<ASF::FLOAT>* renderer_ = nullptr;
};

// The maximum clock skew is +/-1000PPM. These tests use a skew less than the
// maximum so the two sides have a chance to converge (at the maximum, the slow
// side can never fully catch up).
TEST_F(MicroSrcPipelineTest, Impulse_FastReferenceClock) { RunImpulseTest(500, kFrameRate); }

TEST_F(MicroSrcPipelineTest, Impulse_SlowReferenceClock) { RunImpulseTest(-500, kFrameRate); }

TEST_F(MicroSrcPipelineTest, StepFn_FastReferenceClock) { RunStepFnTest(500, kFrameRate); }

TEST_F(MicroSrcPipelineTest, StepFn_SlowReferenceClock) { RunStepFnTest(-500, kFrameRate); }

// For these SineWave tests, our input buffer can be 2s long at 96kHz.
// We use the largest power-of-2 that will fit.
//
// For the frequency, we use a high frequency that has room to be slewed
// (in particular, we don't use num_input_frames/2 because that may be
// slewed to a higher frequency, which we couldn't detect because it exceeds
// the Nyquist limit). We also choose a number that is a multiple of the
// slew so the target output frequency is a nice round number.
//
// The numbers below work out to a frequency of 20k / (131072/96kHz) = 14648kHz.
TEST_F(MicroSrcPipelineTest, SineWave_FastReferenceClock) { RunSineWaveTest(500, 131072, 20000); }

TEST_F(MicroSrcPipelineTest, SineWave_SlowReferenceClock) { RunSineWaveTest(-500, 131072, 20000); }

}  // namespace media::audio::test

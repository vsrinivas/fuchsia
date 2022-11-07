// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/stdcompat/bit.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/clock.h>
#include <zircon/types.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <vector>

#include <gmock/gmock.h>

#include "src/media/audio/audio_core/shared/mixer/sinc_sampler.h"
#include "src/media/audio/audio_core/testing/integration/hermetic_audio_test.h"
#include "src/media/audio/lib/analysis/analysis.h"
#include "src/media/audio/lib/analysis/generators.h"
#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/clock/utils.h"
#include "src/media/audio/lib/processing/gain.h"
#include "src/media/audio/lib/test/comparators.h"

using ASF = fuchsia::media::AudioSampleFormat;

namespace media::audio::test {

class ClockSyncPipelineTest : public HermeticAudioTest {
  struct Peak {
    int64_t index;
    float value;
  };

 protected:
  static constexpr int32_t kFrameRate = 96000;
  static constexpr int64_t kPayloadFrames = 2 * kFrameRate;         // 2sec ring buffer
  static constexpr int64_t kPacketFrames = kFrameRate * 10 / 1000;  // 10ms packets
  static_assert((kFrameRate * 10) % 1000 == 0);

  ClockSyncPipelineTest() : format_(Format::Create<ASF::FLOAT>(1, kFrameRate).value()) {}

  void TearDown() override {
    if constexpr (kEnableAllOverflowAndUnderflowChecksInRealtimeTests) {
      ExpectNoOverflowsOrUnderflows();
    }

    HermeticAudioTest::TearDown();
  }

  virtual void Init(int32_t clock_slew_ppm, int64_t num_frames_input) = 0;
  virtual int64_t ConvergenceFrames() const = 0;
  virtual double NumFramesOutput(int32_t clock_slew_ppm, int64_t num_frames_input) = 0;

  AudioBuffer<ASF::FLOAT> Impulse(float value = 0.5, int64_t pre_silence_frames = 0,
                                  int64_t post_silence_frames = 0) {
    AudioBuffer<ASF::FLOAT> out(format_, pre_silence_frames + 1 + post_silence_frames);
    out.samples()[pre_silence_frames] = value;
    return out;
  }

  AudioBuffer<ASF::FLOAT> SilentBuffer(int64_t frames) {
    return GenerateSilentAudio<ASF::FLOAT>(format_, frames);
  }

  AudioBuffer<ASF::FLOAT> FillBuffer(int64_t frames, float value = 0.5) {
    AudioBuffer<ASF::FLOAT> out(format_, frames);
    for (int64_t s = 0; s < out.NumSamples(); s++) {
      out.samples()[s] = value;
    }
    return out;
  }

  // For a signal change occurring at frame T, how far BEFORE that frame will the effects of that
  // change be reflected in the output. We use no effects; this comes from SincSampler only.
  int64_t PreRampFrames() {
    auto mixer = mixer::SincSampler::Select(format_.stream_type(), format_.stream_type());
    // Initial ramping requires that we play 10ms of silence before doing bit-for-bit comparisons.
    return std::max(static_cast<int32_t>(mixer->pos_filter_width().Ceiling()), kFrameRate / 100);
  }

  // For a signal change occurring at frame T, how far AFTER that frame will the output reflect some
  // effect of the previous signal. We use no effects; this comes from SincSampler only.
  int64_t PostRampFrames() {
    auto mixer = mixer::SincSampler::Select(format_.stream_type(), format_.stream_type());
    return mixer->neg_filter_width().Ceiling();
  }

  // Maximum number of frames needed for a transition between two adjacent signals. At the beginning
  // of this interval, the output begins to reflect the new signal; only at the end of this interval
  // is the full effect shown. During this interval, the output is a cross-fading mixture of the
  // preceding signal and the new signal. We use no effects; this comes from SincSampler only.
  // These are SOURCE frames, but rates are so near unity that we safely use them interchangeably.
  int64_t TotalRampFrames() { return PreRampFrames() + PostRampFrames(); }

  // Offset of the first audio sample. This should be greater than TotalRampFrames() so that there
  // is silence and then transitional frames at the start of the output, following by the signal.
  // These are SOURCE frames, but rates are so near unity that we safely use them interchangeably.
  int64_t OffsetFrames() {
    constexpr int64_t kFramesOfSilence = 1024;
    EXPECT_TRUE(kFramesOfSilence > TotalRampFrames())
        << "For effective testing, OffsetFrames must exceed TotalRampFrames()";

    return kFramesOfSilence;
  }

  // Capture the ring buffer and rotate it leftward by the given offset, so the output starts at [0]
  AudioBuffer<ASF::FLOAT> SnapshotRingBuffer(int64_t offset_before_output_start) {
    auto ring_buffer = output_->SnapshotRingBuffer();

    offset_before_output_start %= ring_buffer.NumFrames();

    auto shifted =
        AudioBufferSlice(&ring_buffer, offset_before_output_start, ring_buffer.NumFrames()).Clone();
    shifted.Append(AudioBufferSlice(&ring_buffer, 0, offset_before_output_start));

    return shifted;
  }

  // Return the index of the peak sample, relative to the first frame in the slice.
  Peak FindPeak(AudioBufferSlice<ASF::FLOAT> slice) {
    EXPECT_TRUE(slice.format().channels() == 1) << "Channels must match";
    EXPECT_TRUE(slice.NumFrames() >= 1) << "Slice must contain data";
    int64_t peak_idx = 0;
    float peak_val = slice.SampleAt(0, 0);
    for (int64_t frame = 1; frame < slice.NumFrames(); ++frame) {
      if (auto s = slice.SampleAt(frame, 0); std::abs(s) > std::abs(peak_val)) {
        peak_idx = frame;
        peak_val = s;
      }
    }
    return {.index = peak_idx, .value = peak_val};
  }

  // Verify that the clock for this renderer is running at the expected rate
  static void CheckClockRate(const zx::clock& clock, int32_t clock_slew_ppm) {
    auto ref_clock_result = clock::GetClockDetails(clock);
    ASSERT_TRUE(ref_clock_result.is_ok());

    auto numerator =
        static_cast<double>(ref_clock_result.value().mono_to_synthetic.rate.synthetic_ticks);
    auto denominator =
        static_cast<double>(ref_clock_result.value().mono_to_synthetic.rate.reference_ticks);
    double measured_slew_ppm = (numerator * (1e6 / denominator)) - 1'000'000.0;

    // Don't wait for a driver clock to fully settle (a minute or more); accept a tolerance
    constexpr double kSlewTolerance = 0.12;
    EXPECT_NEAR(measured_slew_ppm, static_cast<double>(clock_slew_ppm),
                fabs(clock_slew_ppm * kSlewTolerance));
  }

  // Send two impulses separated by frames_between_impulses, using a reference clock with the given
  // slew. The output should contain two impulses separated by NumFramesOutput.
  //
  // This test validates that time is correctly translated between the two clocks.
  // This test validates the following, with two 1-frame impulses during clock synchronization:
  // A, The impulses are peak-detected in the output, with expected magnitudes;
  // B. The impulse-to-impulse interval is the expected number of frames;
  // C. The renderer clock is running at the expected rate.
  // All measurements use tolerance ranges except where explicitly stated as exact.
  void RunImpulseTest(int32_t clock_slew_ppm, int64_t frames_between_impulses) {
    constexpr double kInputImpulseMagnitude = 1.0;
    constexpr double kOutputImpulseMagnitude = kInputImpulseMagnitude * 0.65;
    constexpr bool kDebugOutputImpulseValues = false;

    // These should be zero, once lookahead/decay times are properly accounted-for.
    const int64_t kPreSilenceFrames = PreRampFrames();
    const int64_t kPostSilenceFrames = PostRampFrames() * 2;

    Init(clock_slew_ppm, frames_between_impulses);

    // This is a precise timing test, so clocks must converge before we start. This can take
    // multiple trips around our ring buffer, so below when calculating the expected start of the
    // output signal, we must modulo it with the ring-buffer size.
    auto offset_before_input_start = std::max(OffsetFrames(), ConvergenceFrames());

    // We use single-frame impulses in the input signal
    auto impulse = Impulse(kInputImpulseMagnitude, kPreSilenceFrames, kPostSilenceFrames);

    // Play two impulses frames_between_impulses apart.
    auto first_input = renderer_->AppendSlice(impulse, kPacketFrames, offset_before_input_start);
    auto second_input = renderer_->AppendSlice(impulse, kPacketFrames,
                                               offset_before_input_start + frames_between_impulses);

    if constexpr (kDebugOutputImpulseValues) {
      auto snapshot = renderer_->payload().Snapshot<ASF::FLOAT>();
      snapshot.Display(0, 2 * impulse.NumFrames(), "Input signal:");
    }

    renderer_->PlaySynchronized(this, output_, 0);
    renderer_->WaitForPackets(this, first_input);
    renderer_->WaitForPackets(this, second_input);

    auto offset_before_output_start =
        static_cast<int64_t>(NumFramesOutput(clock_slew_ppm, offset_before_input_start));
    // Shift the output so that neither "peak detection" range crosses the ring buffer boundary.
    auto ring_buffer = SnapshotRingBuffer(offset_before_output_start);

    if constexpr (!kEnableAllOverflowAndUnderflowChecksInRealtimeTests) {
      // In case of underflows, exit NOW (don't assess this buffer).
      // TODO(fxbug.dev/80003): Remove workarounds when underflow conditions are fixed.
      if (DeviceHasUnderflows(output_)) {
        GTEST_SKIP() << "Skipping impulse checks due to underflows";
      }
    }

    // A. two impulses are detected in the bisected output ring buffer
    auto num_frames_output = NumFramesOutput(clock_slew_ppm, frames_between_impulses);
    auto midpoint = static_cast<int64_t>(num_frames_output) / 2;
    auto first_peak = FindPeak(AudioBufferSlice(&ring_buffer, 0, midpoint));
    auto second_peak = FindPeak(AudioBufferSlice(&ring_buffer, midpoint, ring_buffer.NumFrames()));

    if constexpr (kDebugOutputImpulseValues) {
      FX_LOGS(INFO) << "Found impulse peaks of [" << first_peak.index << "] " << first_peak.value
                    << " and [" << midpoint + second_peak.index << "] " << second_peak.value;
      auto first_start = first_peak.index - std::min(first_peak.index, PreRampFrames());
      ring_buffer.Display(first_start, first_start + TotalRampFrames(), "Front of output ring");
      auto second_start = midpoint + second_peak.index - PreRampFrames();
      ring_buffer.Display(second_start, second_start + TotalRampFrames(), "Back of output ring");
    }

    EXPECT_GE(first_peak.value, kOutputImpulseMagnitude);
    EXPECT_GE(second_peak.value, kOutputImpulseMagnitude);

    // B. The distance between the two impulses should be num_frames_output.
    auto peak_to_peak_frames = (midpoint + second_peak.index) - first_peak.index;
    EXPECT_NEAR(static_cast<double>(peak_to_peak_frames), num_frames_output, 1.0);

    // C. clock rate check
    CheckClockRate(renderer_->reference_clock(), clock_slew_ppm);
  }

  // Send a flat signal (step function) of size num_frames_input, using a reference clock with the
  // given slew. The output should contain an equivalent step function of size NumFramesOutput.
  //
  // Note, the exact values are not important. The primary goal of this test is to ensure the output
  // does not have any dropped frames. A buggy mixer might drop frames if there is a gap between mix
  // calls, specifically when the destination clock is running faster than the source clock.
  //
  // This test validates the following, rendering a step function during clock synchronization:
  // A. The output step signal starts at the expected frame;
  // B. The output step signal has the expected magnitude for its entirety (no dropouts);
  // C. The output step signal ends at the expected frame;
  // D. Subsequent output signal (after PostRampFrames) is precisely zero;
  // E. The renderer clock is running at the expected rate.
  // All measurements use tolerance ranges except where explicitly stated as exact.
  void RunStepTest(int32_t clock_slew_ppm, int64_t num_frames_input) {
    constexpr float kInputStepMagnitude = 0.95f;
    constexpr double kOutputRelativeError = 0.025;

    Init(clock_slew_ppm, num_frames_input);

    // This is a precise timing test, so clocks must converge before we start. This can take
    // multiple trips around our ring buffer, so below when calculating the expected start of the
    // output signal, we must modulo it with the ring-buffer size.
    auto offset_before_input_start = std::max(OffsetFrames(), ConvergenceFrames());
    auto initial_silence = SilentBuffer(offset_before_input_start);
    auto input = FillBuffer(num_frames_input, kInputStepMagnitude);

    auto silent_packets = renderer_->AppendSlice(initial_silence, kPacketFrames, 0);
    auto packets = renderer_->AppendSlice(input, kPacketFrames, silent_packets.back()->end_pts);
    renderer_->PlaySynchronized(this, output_, 0);
    renderer_->WaitForPackets(this, packets);

    // NumFramesOutput returns a double. It's OK to truncate this: we insert transition ranges for
    // filter TotalRampFrames, between the "must be silence" and "must be non-silence" ranges.
    auto offset_before_output_start = static_cast<int64_t>(
        NumFramesOutput(clock_slew_ppm, offset_before_input_start - PreRampFrames()));
    // We shift the output so that neither signal range nor silence range cross the ring's edge.
    auto ring_buffer = SnapshotRingBuffer(offset_before_output_start);

    if constexpr (!kEnableAllOverflowAndUnderflowChecksInRealtimeTests) {
      // In case of underflows, exit NOW (don't assess this buffer).
      // TODO(fxbug.dev/80003): Remove workarounds when underflow conditions are fixed.
      if (DeviceHasUnderflows(output_)) {
        GTEST_SKIP() << "Skipping data checks due to underflows";
      }
    }

    // The output should contain silence, followed by TotalRampFrames of transition, followed by
    // data, followed by TotalRampFrames of transition, followed again by silence. Ultimately we're
    // testing that we emit the correct number of output frames. Our test is necessarily imprecise,
    // despite our using an input signal that is crisp and maximally detectable, because we ignore
    // the sampler's ramp intervals when doing our "signal or silence" checks. To illustrate:
    //
    //  max PreRampFrames                    max PostRampFrames
    //      |      |                             |      |
    //      |      V      num_frames_input       V      |
    //       \ +-----+-------------------------+-----+  |
    //        \                                    .    |
    //         \                                   .    |
    //          |                                  .    |
    //          V      num_frames_output (longer)  .    V
    //         +-----+-----------------------------+-----+
    //               +-----+                 +-----+
    //                 ^   ^                 ^   ^
    //                 |   |                 |   |
    //  max PostRampFrames data_start  data_end  max PreRampFrames
    //
    //
    // In this case, we expect more output frames than input frames. However, since the delta
    // is smaller than the maximum PostRampFrames, we cannot be sure if the extra frames are output
    // or PostRampFrames. This means we cannot check if the system operated perfectly.
    //
    // To address this problem, the diff between input and output frames must be greater than the
    // TotalRampFrames. This is checked in Init().
    //
    // We do not enforce a precise output duration or an exact step magnitude. We draw conservative
    // boundaries around the output and verify that no dropped frames occur within the boundaries.
    //
    // We do not check data values during the TotalRampFrames transition, because sinc
    // filter coefficients have zero-crossings, thus zero data values might be correct during
    // transition (if the SRC ratio is 1:1, for example). In our shifted ring-buffer, this ramp
    // begins at frame 0 (we include PreRampFrames() of frames of output before the signal begins).
    auto num_frames_output =
        static_cast<int64_t>(NumFramesOutput(clock_slew_ppm, num_frames_input));
    auto data_start = TotalRampFrames();                  // signal reaches full strength
    auto data_end = num_frames_output - PreRampFrames();  // silence starts to ramp in
    auto silence_start = data_start + num_frames_output + PostRampFrames();  // silence fully ramped

    // A. output step starts at expected frame.
    // B. magnitude is within tolerance across the entire step range: no dropouts
    auto data = AudioBufferSlice(&ring_buffer, data_start, data_end);
    CompareAudioBufferOptions compare_opts;
    compare_opts.test_label = fxl::StringPrintf("check data (starting at %lu)", data_start);
    compare_opts.max_relative_error = kOutputRelativeError;
    auto expect = AudioBufferSlice(&input, 0, data_end - data_start);
    CompareAudioBuffers(data, expect, compare_opts);

    // C. output step ends at expected frame.
    // D. subsequent range is entirely silent
    auto silence = AudioBufferSlice(&ring_buffer, silence_start, ring_buffer.NumFrames());
    ExpectAudioBufferOptions expect_opts;
    expect_opts.test_label = fxl::StringPrintf("check silence (starting at %lu)", silence_start);
    ExpectSilentAudioBuffer(silence, expect_opts);

    // E. clock rate check
    CheckClockRate(renderer_->reference_clock(), clock_slew_ppm);
  }

  // Send a sine wave using a clock with given slew. The output should be a sine wave at slewed
  // frequency. Each sinusoidal period contains (num_frames_to_analyze / input_freq) frames.
  //
  // This test validates the following, rendering a sinusoid during clock synchronization:
  // A. The output signal's magnitude is essentially unattenuated (within tolerance);
  // B. The output signal's center frequency is shifted by exactly the expected amount;
  // C. No other frequencies exceed the noise floor threshold (with a few exceptions);
  // D. The above-noise-floor frequencies are clustered around the primary output frequency;
  // E. The width of that cluster (from leftmost to rightmost) is below a certain "peak width";
  // F. The renderer clock is running at the expected rate (within a certain tolerance).
  void RunSineTest(int32_t clock_slew_ppm, int64_t num_frames_to_analyze, int32_t input_freq) {
    constexpr double kInputSineMagnitude = 1.0;
    constexpr double kExpectedOutputSineMagnitude = 0.99;
    constexpr double kExpectedNoiseFloorDb = -72.0;
    constexpr int64_t kMaxPeakWidth = 2;
    constexpr bool kDebugOutputSineValues = false;

    ASSERT_TRUE(cpp20::has_single_bit(static_cast<uint64_t>(num_frames_to_analyze)))
        << "num_frames_to_analyze must be a power of 2";
    ASSERT_TRUE(num_frames_to_analyze < kPayloadFrames)
        << "num_frames_to_analyze must fit into the ring-buffer";
    Init(clock_slew_ppm, num_frames_to_analyze);

    // This is a precise frequency detection test, so clocks must converge before we start. This can
    // take multiple trips around our ring buffer, so below when calculating the start of the output
    // signal, we must modulo it with the ring-buffer size.
    auto offset_before_input_start = ConvergenceFrames();

    // For fast input clocks, "output frames written" is less than "input frames consumed".
    // To ensure we produce enough output frames for analysis, we repeat the first part of the input
    // (specifically, half of the remaining space in the ring buffer).
    // We can append this without a discontinuity, because the input signal's frequency guarantees
    // that it fits exactly into num_frames_to_analyze frames (thus it can be perfectly looped).
    auto actual_num_frames_input =
        num_frames_to_analyze + (kPayloadFrames - num_frames_to_analyze) / 2;
    auto initial_silence = GenerateSilentAudio(format_, offset_before_input_start);
    auto input =
        GenerateCosineAudio(format_, num_frames_to_analyze, input_freq, kInputSineMagnitude);
    auto input_repeated =
        AudioBufferSlice(&input, 0, actual_num_frames_input - num_frames_to_analyze);

    // Verify that this is enough output for our analysis (after removing TotalRampFrames)...
    ASSERT_TRUE(NumFramesOutput(clock_slew_ppm, actual_num_frames_input - TotalRampFrames()) >
                static_cast<double>(num_frames_to_analyze));
    // ... and that this additional output doesn't cause us to overrun the ring buffer.
    ASSERT_TRUE(NumFramesOutput(clock_slew_ppm, actual_num_frames_input) <
                static_cast<double>(kPayloadFrames));

    auto silent_packets = renderer_->AppendSlice(initial_silence, kPacketFrames, 0);
    auto packets1 = renderer_->AppendSlice(input, kPacketFrames, silent_packets.back()->end_pts);
    auto packets2 = renderer_->AppendSlice(input_repeated, kPacketFrames, packets1.back()->end_pts);
    renderer_->PlaySynchronized(this, output_, 0);
    renderer_->WaitForPackets(this, packets2);

    // offset_before_input_start is input frame where signal starts. Add PostRampFrames to get the
    // frame where any effect of preceding silence is completely gone. Translate to output frame.
    auto offset_before_output_start = static_cast<int64_t>(
        NumFramesOutput(clock_slew_ppm, offset_before_input_start + PostRampFrames()));

    // Shift the entire buffer (with wraparound) to produce a full-length signal starting at [0].
    auto ring_buffer = SnapshotRingBuffer(offset_before_output_start);

    if constexpr (!kEnableAllOverflowAndUnderflowChecksInRealtimeTests) {
      // In case of underflows, exit NOW (don't assess this buffer).
      // TODO(fxbug.dev/80003): Remove workarounds when underflow conditions are fixed.
      if (DeviceHasUnderflows(output_)) {
        GTEST_SKIP() << "Skipping data checks due to underflows";
      }
    }

    // Compute the slewed frequency in the output.
    int32_t output_freq = static_cast<int32_t>(
        static_cast<double>(input_freq) * static_cast<double>(num_frames_to_analyze) /
        NumFramesOutput(clock_slew_ppm, num_frames_to_analyze));

    // As the mixer tracks the input clock's position, it may be a little ahead or behind, resulting
    // in a cluster of detected frequencies, not just the single expected frequency. Measure this.
    auto result =
        MeasureAudioFreq(AudioBufferSlice(&ring_buffer, 0, num_frames_to_analyze), output_freq);

    // Ensure the FFT has a peak centered on freq.
    double peak_magnitude = 0;
    int32_t peak_freq = 0;
    for (int32_t freq = 0; static_cast<size_t>(freq) < result.all_square_magnitudes.size();
         ++freq) {
      if (auto magn = sqrt(result.all_square_magnitudes[freq]); magn > peak_magnitude) {
        peak_magnitude = magn;
        peak_freq = freq;
      }
    }

    if constexpr (kDebugOutputSineValues) {
      double left_max_magn = 0, right_max_magn = 0;
      for (int32_t freq = 0; freq < output_freq; ++freq) {
        auto magn = sqrt(result.all_square_magnitudes[freq]);
        left_max_magn = std::max(left_max_magn, magn);
      }
      for (int32_t freq = output_freq + 1;
           freq < static_cast<int32_t>(result.all_square_magnitudes.size()); ++freq) {
        auto magn = sqrt(result.all_square_magnitudes[freq]);
        right_max_magn = std::max(right_max_magn, magn);
      }

      printf("\nPeak frequency bin %d, magnitude %9.6f. left-max %12.9f; right-max %12.9f\n",
             peak_freq, peak_magnitude, left_max_magn, right_max_magn);
      for (int32_t freq = (peak_freq & ~0x07) - 64; freq < (peak_freq & ~0x07) + 64; ++freq) {
        if (freq % 8 == 0) {
          printf("\n[%d] ", freq);
        }
        printf("%9.6f ", sqrt(result.all_square_magnitudes[freq]));
      }
    }

    // A. Input peak magnitude is 1.0. This will leak out to side freqs, but should remain high.
    EXPECT_GE(peak_magnitude, kExpectedOutputSineMagnitude);
    // B. Output frequency is shifted by the expected amount.
    EXPECT_EQ(peak_freq, output_freq) << "magnitude at peak_freq = " << peak_magnitude;

    // C. We determine the minimal [peak_start, peak_end] range -- including our center output
    // frequency -- such that no frequencies outside it exceed our noise floor.
    // D. Our -75 dB noise floor is chosen somewhat arbitrary (12.5 bits of accurate signal).
    const double kNoiseFloor = media_audio::DbToScale(kExpectedNoiseFloorDb);
    int32_t peak_start = output_freq;
    double left_max_magn = 0;
    for (int32_t freq = output_freq - 1; freq >= 0; --freq) {
      auto magn = sqrt(result.all_square_magnitudes[freq]);
      left_max_magn = std::max(left_max_magn, magn);
      if (magn > kNoiseFloor) {
        peak_start = freq;
      }
    }
    int32_t peak_end = output_freq;
    double right_max_magn = 0;
    for (int32_t freq = output_freq + 1;
         static_cast<uint64_t>(freq) < result.all_square_magnitudes.size(); ++freq) {
      auto magn = sqrt(result.all_square_magnitudes[freq]);
      right_max_magn = std::max(right_max_magn, magn);
      if (magn > kNoiseFloor) {
        peak_end = freq;
      }
    }

    // E. The peak should be sharply identified, if synchronization is stable & accurate. We
    // expressly use a frequency matched to our power-of-2 length (thus require no windowing).
    // Our peak width should span a single bin; we round out to 2.
    bool peak_meets_requirements = (peak_end - peak_start <= kMaxPeakWidth);
    EXPECT_TRUE(peak_meets_requirements)
        << "At this noise floor, peak width is " << peak_end - peak_start
        << ". At this width, noise floor is " << std::setprecision(4)
        << std::log10(left_max_magn) * 20.0 << " dB / " << std::log10(right_max_magn) * 20
        << " dB (L/R)";

    // F. clock rate check
    CheckClockRate(renderer_->reference_clock(), clock_slew_ppm);
  }

  const TypedFormat<ASF::FLOAT> format_;
  VirtualOutput<ASF::FLOAT>* output_ = nullptr;
  AudioRendererShim<ASF::FLOAT>* renderer_ = nullptr;
};

class MicroSrcPipelineTest : public ClockSyncPipelineTest {
 public:
  // Expected MicroSRC convergence time, in frames: about 15 mix periods at 10ms per period.
  int64_t ConvergenceFrames() const override { return 15 * kPacketFrames; }

 protected:
  void Init(int32_t clock_slew_ppm, int64_t num_frames_input) override {
    zx::clock ref_clock = ::media::audio::clock::AdjustableCloneOfMonotonic();

    zx::clock::update_args args;
    args.reset().set_rate_adjust(clock_slew_ppm);
    ASSERT_TRUE(ref_clock.update(args) == ZX_OK) << "Clock rate_adjust failed";

    // Now that the clock is adjusted, remove ZX_RIGHT_WRITE before sending it (AudioCore never
    // adjusts client-submitted clocks anyway, but this makes it truly impossible).
    auto clock_result = audio::clock::DuplicateClock(ref_clock);
    ASSERT_TRUE(clock_result.is_ok());
    ref_clock = clock_result.take_value();

    // Buffer up to 2s of data.
    output_ = CreateOutput({{0xff, 0x00}}, format_, kPayloadFrames);
    renderer_ = CreateAudioRenderer(format_, kPayloadFrames,
                                    fuchsia::media::AudioRenderUsage::MEDIA, std::move(ref_clock));

    // Any initial offset, plus the signal, should fit entirely into the ring buffer
    auto offset_before_input_start = std::max(OffsetFrames(), ConvergenceFrames());
    ASSERT_TRUE(num_frames_input + offset_before_input_start < kPayloadFrames)
        << "input signal is too big for the ring buffer";
  }

  double NumFramesOutput(int32_t clock_slew_ppm, int64_t num_frames_input) override {
    return static_cast<double>(num_frames_input) *
           (1e6 / (1e6 + static_cast<double>(clock_slew_ppm)));
  }
};

class AdjustableClockPipelineTest : public ClockSyncPipelineTest {
 public:
  // Expected device clock convergence time in frames.
  int64_t ConvergenceFrames() const override { return 13 * kFrameRate; }

 protected:
  void Init(int32_t clock_slew_ppm, int64_t num_frames_input) override {
    // Specify the clock rate for the output device.
    constexpr int32_t kMonotonicDomain = 0;
    constexpr int32_t kNonMonotonicDomain = 1;
    VirtualDevice::ClockProperties clock_properties = {
        .domain = (clock_slew_ppm ? kNonMonotonicDomain : kMonotonicDomain),
        .initial_rate_adjustment_ppm = clock_slew_ppm,
    };

    // Buffer up to 2s of data.
    output_ =
        CreateOutput({{0xff, 0x00}}, format_, kPayloadFrames, std::nullopt, 0.0, clock_properties);

    // With this uninitialized clock, instruct AudioRenderer to use AudioCore's clock.
    renderer_ = CreateAudioRenderer(format_, kPayloadFrames,
                                    fuchsia::media::AudioRenderUsage::MEDIA, zx::clock());
  }

  double NumFramesOutput(int32_t clock_slew_ppm, int64_t num_frames_input) override {
    return static_cast<double>(num_frames_input);
  }
};

// Use these when debugging, to eliminate rate-adjustment. They aren't worth running otherwise.
//
// TEST_F(MicroSrcPipelineTest, ImpulseBaseline) { RunImpulseTest(0, kFrameRate); }
// TEST_F(MicroSrcPipelineTest, StepBaseline) { RunStepTest(0, kFrameRate); }
// TEST_F(MicroSrcPipelineTest, SineBaseline) { RunSineTest(0, 131072, 20000); }
// TEST_F(AdjustableClockPipelineTest, ImpulseBaseline) { RunImpulseTest(0, kFrameRate); }
// TEST_F(AdjustableClockPipelineTest, StepBaseline) { RunStepTest(0, kFrameRate); }
// TEST_F(AdjustableClockPipelineTest, SineBaseline) { RunSineTest(0, 131072, 20000); }

// The maximum clock adjustment is +/-1000 PPM. These tests use a skew less than that, so the two
// sides have a chance to converge (at the maximum, the slow side can never fully catch up).
// To be discernable from the TotalRampFrames interval, the skew must also be > 291 PPM.
// At 96k rate, to make the offset an exact integer, clock skew should be a multiple of 125.
TEST_F(MicroSrcPipelineTest, ImpulseUp500) { RunImpulseTest(500, kFrameRate); }
TEST_F(MicroSrcPipelineTest, ImpulseUp875) { RunImpulseTest(875, kFrameRate); }
TEST_F(MicroSrcPipelineTest, ImpulseDown500) { RunImpulseTest(-500, kFrameRate); }

TEST_F(AdjustableClockPipelineTest, ImpulseUp500) { RunImpulseTest(500, kFrameRate); }
TEST_F(AdjustableClockPipelineTest, ImpulseDown500) { RunImpulseTest(-500, kFrameRate); }

TEST_F(MicroSrcPipelineTest, StepUp500) { RunStepTest(500, kFrameRate); }
TEST_F(MicroSrcPipelineTest, StepDown500) { RunStepTest(-500, kFrameRate); }
TEST_F(MicroSrcPipelineTest, StepDown625) { RunStepTest(-625, kFrameRate); }

TEST_F(AdjustableClockPipelineTest, StepUp500) { RunStepTest(500, kFrameRate); }
TEST_F(AdjustableClockPipelineTest, StepDown500) { RunStepTest(-500, kFrameRate); }

// For best precision in measuring resultant signal frequency, input signal frequency should be
// high, but with room for upward slew without approaching the Nyquist limit(num_input_frames/2).
// To make expected result frequency a round number, input frequency is a multiple of slew_ppm.
//
// // Sine test input buffer length: the largest power-of-2 (frames) that fits into 2 sec @96kHz.
// The numbers below work out to a frequency of 20k / (131072/96kHz) = 14.648 kHz.
TEST_F(MicroSrcPipelineTest, SineUp500) { RunSineTest(500, 131072, 20000); }
TEST_F(MicroSrcPipelineTest, SineDown500) { RunSineTest(-500, 131072, 20000); }
TEST_F(MicroSrcPipelineTest, SineDown750) { RunSineTest(-750, 131072, 20000); }

TEST_F(AdjustableClockPipelineTest, SineUp500) { RunSineTest(500, 131072, 20000); }
TEST_F(AdjustableClockPipelineTest, SineDown500) { RunSineTest(-500, 131072, 20000); }

}  // namespace media::audio::test

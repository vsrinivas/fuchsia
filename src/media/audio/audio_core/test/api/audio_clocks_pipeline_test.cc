// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

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

class ClockSyncPipelineTest : public HermeticAudioTest {
  struct Peak {
    size_t index;
    float value;
  };

 protected:
  static constexpr size_t kFrameRate = 96000;
  static constexpr size_t kPayloadFrames = 2 * kFrameRate;  // 2sec ring buffer
  static constexpr size_t kPacketFrames = kFrameRate * RendererShimImpl::kPacketMs / 1000;

  ClockSyncPipelineTest() : format_(Format::Create<ASF::FLOAT>(1, kFrameRate).value()) {}

  void TearDown() {
    ExpectNoOverflowsOrUnderflows();
    HermeticAudioTest::TearDown();
  }

  virtual void Init(int32_t clock_slew_ppm, size_t num_frames_input) = 0;
  virtual size_t ConvergenceFrames() const = 0;
  virtual double NumFramesOutput(int32_t clock_slew_ppm, size_t num_frames_input) = 0;

  AudioBuffer<ASF::FLOAT> Impulse(float value = 0.5) {
    AudioBuffer<ASF::FLOAT> out(format_, 1);
    out.samples()[0] = value;
    return out;
  }

  AudioBuffer<ASF::FLOAT> FillBuffer(size_t frames, float value = 0.5) {
    AudioBuffer<ASF::FLOAT> out(format_, frames);
    for (size_t s = 0; s < out.NumSamples(); s++) {
      out.samples()[s] = value;
    }
    return out;
  }

  // Maximum ring-in frames. We use no effects; this comes from SincSampler only. This represents
  // how long BEFORE a signal's first frame that it can be reflected in the SincSampler's output.
  size_t RingIn() {
    auto mixer = mixer::SincSampler::Select(format_.stream_type(), format_.stream_type());
    return mixer->pos_filter_width().Ceiling();
  }

  // Maximum ring-out frames. We use no effects; this comes from SincSampler only. This represents
  // how long AFTER a signal's last frame that it is still reflected in the SincSampler's output.
  // These are SOURCE frames, but rates are so near unity that we safely use them interchangeably.
  size_t RingOut() {
    auto mixer = mixer::SincSampler::Select(format_.stream_type(), format_.stream_type());
    return mixer->neg_filter_width().Ceiling();
  }

  // Offset of the first audio sample. This should be greater than RingIn() so that there is silence
  // and then transitional frames at the start of the output, following by the signal.
  // These are SOURCE frames, but rates are so near unity that we safely use them interchangeably.
  size_t OffsetFrames() {
    constexpr size_t kFramesOfSilence = 20;
    EXPECT_TRUE(kFramesOfSilence > RingIn())
        << "For effective testing, OffsetFrames must exceed RingIn()";

    return kFramesOfSilence;
  }

  // Capture the ring buffer and rotate it leftward by the given offset, so the output starts at [0]
  AudioBuffer<ASF::FLOAT> SnapshotRingBuffer(size_t offset_before_output_start) {
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
    size_t peak_idx = 0;
    float peak_val = slice.SampleAt(0, 0);
    for (size_t frame = 1; frame < slice.NumFrames(); ++frame) {
      if (auto s = slice.SampleAt(frame, 0); std::abs(s) > std::abs(peak_val)) {
        peak_idx = frame;
        peak_val = s;
      }
    }
    return {peak_idx, peak_val};
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
  // A, The 2 impulses are peak-detected in the output;
  // B. The impulse-to-impulse interval is the expected number of frames;
  // C. The renderer clock is running at the expected rate (within a certain tolerance).
  void RunImpulseTest(int32_t clock_slew_ppm, size_t frames_between_impulses) {
    constexpr double kInputImpulseMagnitude = 1.0;
    constexpr bool kDebugOutputImpulseValues = false;

    Init(clock_slew_ppm, frames_between_impulses);

    // This is a precise timing test, so clocks must converge before we start. This can take
    // multiple trips around our ring buffer, so below when calculating the expected start of the
    // output signal, we must modulo it with the ring-buffer size.
    auto offset_before_input_start = std::max(OffsetFrames(), ConvergenceFrames());

    // We use single-frame impulses in the input signal
    auto impulse = Impulse(kInputImpulseMagnitude);

    // Play two impulses frames_between_impulses apart.
    auto first_input = renderer_->AppendPackets({&impulse}, offset_before_input_start);
    auto second_input =
        renderer_->AppendPackets({&impulse}, offset_before_input_start + frames_between_impulses);
    renderer_->PlaySynchronized(this, output_, 0);
    renderer_->WaitForPackets(this, first_input);
    renderer_->WaitForPackets(this, second_input);

    auto offset_before_output_start =
        static_cast<size_t>(NumFramesOutput(clock_slew_ppm, offset_before_input_start - RingIn()));
    // Shift the output so that neither "peak detection" range crosses the ring buffer boundary.
    auto ring_buffer = SnapshotRingBuffer(offset_before_output_start);

    // A. two impulses are detected in the bissected output ring buffer
    auto num_frames_output = NumFramesOutput(clock_slew_ppm, frames_between_impulses);
    auto midpoint = num_frames_output / 2;
    auto first_peak = FindPeak(AudioBufferSlice(&ring_buffer, 0, midpoint));
    auto second_peak = FindPeak(AudioBufferSlice(&ring_buffer, midpoint, ring_buffer.NumFrames()));
    auto peak_to_peak_frames = (midpoint + second_peak.index) - first_peak.index;

    if constexpr (kDebugOutputImpulseValues) {
      FX_LOGS(INFO) << "Found impulse peaks of " << first_peak.value << " and "
                    << second_peak.value;
      ring_buffer.Display((first_peak.index - 8 > first_peak.index) ? 0 : first_peak.index - 8,
                          first_peak.index + 8);
      ring_buffer.Display(midpoint + second_peak.index - 8, midpoint + second_peak.index + 8);
    }

    // B. The distance between the two impulses should be num_frames_output.
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
  // A. The output step signal starts at the expected frame (with ringin tolerance);
  // B. The output step signal is non-zero for its entirety (no dropouts);
  // C. The output step signal ends at the expected frame (with ringout tolerance);
  // D. Subsequent output signal is precisely zero (after ringout tolerance);
  // E. The renderer clock is running at the expected rate (within a certain tolerance).
  void RunStepTest(int32_t clock_slew_ppm, size_t num_frames_input) {
    constexpr double kInputStepMagnitude = 0.75;

    Init(clock_slew_ppm, num_frames_input);

    // This is a precise timing test, so clocks must converge before we start. This can take
    // multiple trips around our ring buffer, so below when calculating the expected start of the
    // output signal, we must modulo it with the ring-buffer size.
    auto offset_before_input_start = std::max(OffsetFrames(), ConvergenceFrames());
    auto input = FillBuffer(num_frames_input, kInputStepMagnitude);

    auto packets = renderer_->AppendPackets({&input}, offset_before_input_start);
    renderer_->PlaySynchronized(this, output_, 0);
    renderer_->WaitForPackets(this, packets);

    // NumFramesOutput returns a double. It's OK to truncate this: we insert transition ranges
    // for filter ring in/out, between the "must be silence" and "must be non-silence" ranges.
    auto offset_before_output_start =
        static_cast<size_t>(NumFramesOutput(clock_slew_ppm, offset_before_input_start - RingIn()));
    // We shift the output so that neither signal range nor silence range cross the ring's edge.
    auto ring_buffer = SnapshotRingBuffer(offset_before_output_start);

    // The output should contain silence, followed by optional ring in, followed by data, followed
    // by optional ring out, followed again by silence. Ultimately we're testing that we emit the
    // correct number of output frames. Our test is necessarily imprecise, despite our using an
    // input signal that is crisp and maximally detectable, because we ignore the sampler's ring
    // in/out intervals when doing our "signal or silence" checks. To illustrate:
    //
    //     max-ringin                           max-ringout
    //      |      |                             |      |
    //      |      V      num_frames_input       V      |
    //       \ +-----+-------------------------+-----+  |
    //        \                                    .    |
    //         \                                   .    |
    //          |                                  .    |
    //          V      num_frames_output (longer)  .    V
    //         +-----+-----------------------------+-----+
    //
    //
    // In this case, we expect more output frames than input frames. However, since the delta
    // is smaller than the maximum ring out, we cannot be sure if the extra frames are output
    // or ring out. This means we cannot check if the system operated correctly.
    //
    // To address this problem, the diff between input and output frames must be greater than the
    // total number of ring in + ring out frames. This is checked in Init().
    //
    // We do not enforce a precise output duration or step magnitude. We draw conservative
    // boundaries around the output and verify that no dropped frames occur within the boundaries.
    //
    // We do not ExpectNonSilentAudio during the RingIn+RingOut transition, because sinc filter
    // coefficients have zero-crossings, thus zero data values might be correct during transition
    // (if the SRC ratio is 1:1, for example).
    auto num_frames_output = static_cast<size_t>(NumFramesOutput(clock_slew_ppm, num_frames_input));

    auto data_start = RingIn() + RingOut();               // signal reaches full strength
    auto data_end = num_frames_output;                    // fadein starts for subsequent silence
    auto silence_start = data_start + num_frames_output;  // signal fadeout completes

    ExpectAudioBufferOptions opts;
    opts.num_frames_per_packet = kPacketFrames;

    // A. output step starts at expected frame.
    // B. expected step range is entirely non-silent: no dropouts
    auto data = AudioBufferSlice(&ring_buffer, data_start, data_end);
    opts.test_label = fxl::StringPrintf("check data (starting at %lu)", data_start);
    ExpectNonSilentAudioBuffer(data, opts);

    // C. output step ends at expected frame.
    // D. subsequent range is entirely silent
    auto silence = AudioBufferSlice(&ring_buffer, silence_start, ring_buffer.NumFrames());
    opts.test_label = fxl::StringPrintf("check silence (starting at %lu)", silence_start);
    ExpectSilentAudioBuffer(silence, opts);

    // E. clock rate check
    CheckClockRate(renderer_->reference_clock(), clock_slew_ppm);
  }

  // Send a sine wave using a clock with given slew. The output should be a sine wave at slewed
  // frequency. Each sinusoidal period contains (num_frames_to_analyze / input_freq) frames.
  //
  // This test validates the following, rendering a sinusoid during clock synchronization:
  // A. The output signal's magnitude is essentially unattenuated;
  // B. The output signal's center frequency is shifted by exactly the expected amount;
  // C. No other frequencies exceed the noise floor threshold (with a few exceptions);
  // D. Those above-noise-floor frequencies are clustered around the primary output frequency;
  // E. The width of that cluster (from leftmost to rightmost) is below a certain "peak width";
  // F. The renderer clock is running at the expected rate (within a certain tolerance).
  void RunSineTest(int32_t clock_slew_ppm, size_t num_frames_to_analyze, size_t input_freq) {
    constexpr double kInputSineMagnitude = 1.0;
    constexpr double kExpectedOutputSineMagnitude = 0.99;
    constexpr double kExpectedNoiseFloorDb = -75.0;
    constexpr size_t kMaxPeakWidth = 2;
    constexpr bool kDebugOutputSineValues = false;

    ASSERT_TRUE(fbl::is_pow2(num_frames_to_analyze))
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
    auto input =
        GenerateCosineAudio(format_, num_frames_to_analyze, input_freq, kInputSineMagnitude);
    auto input_prefix =
        AudioBufferSlice(&input, 0, actual_num_frames_input - num_frames_to_analyze);

    // Verify that this is enough output for our analysis (even after subtracting ring in/out) ...
    ASSERT_TRUE(NumFramesOutput(clock_slew_ppm, actual_num_frames_input - RingIn() - RingOut()) >
                num_frames_to_analyze);
    // ... and that this additional output doesn't cause us to overrun the ring buffer.
    ASSERT_TRUE(NumFramesOutput(clock_slew_ppm, actual_num_frames_input) < kPayloadFrames);

    auto packets = renderer_->AppendPackets({&input, input_prefix}, offset_before_input_start);
    renderer_->PlaySynchronized(this, output_, 0);
    renderer_->WaitForPackets(this, packets);

    // offset_before_input_start is input frame where signal starts. Add RingOut to get input frame
    // where any effect from preceding silence has completely "rung out". Translate to output frame.
    auto offset_before_output_start =
        static_cast<size_t>(NumFramesOutput(clock_slew_ppm, offset_before_input_start + RingOut()));

    // Shift the entire buffer (with wraparound) to produce a full-length signal starting at [0].
    auto ring_buffer = SnapshotRingBuffer(offset_before_output_start);

    // Compute the slewed frequency in the output.
    size_t output_freq = static_cast<double>(input_freq) *
                         static_cast<double>(num_frames_to_analyze) /
                         NumFramesOutput(clock_slew_ppm, num_frames_to_analyze);

    // As the mixer tracks the input clock's position, it may be a little ahead or behind, resulting
    // in a cluster of detected frequencies, not just the single expected frequency. Measure this.
    auto result =
        MeasureAudioFreq(AudioBufferSlice(&ring_buffer, 0, num_frames_to_analyze), output_freq);

    // Ensure the FFT has a peak centered on freq.
    double peak_magnitude = 0;
    size_t peak_freq = 0;
    for (size_t freq = 0; freq < result.all_square_magnitudes.size(); ++freq) {
      if (auto magn = sqrt(result.all_square_magnitudes[freq]); magn > peak_magnitude) {
        peak_magnitude = magn;
        peak_freq = freq;
      }
    }

    if constexpr (kDebugOutputSineValues) {
      double left_max_magn = 0, right_max_magn = 0;
      for (ssize_t freq = 0; freq < output_freq; ++freq) {
        auto magn = sqrt(result.all_square_magnitudes[freq]);
        left_max_magn = std::max(left_max_magn, magn);
      }
      for (size_t freq = output_freq + 1; freq < result.all_square_magnitudes.size(); ++freq) {
        auto magn = sqrt(result.all_square_magnitudes[freq]);
        right_max_magn = std::max(right_max_magn, magn);
      }

      printf("\nPeak frequency bin %zu, magnitude %9.6f. left-max %12.9f; right-max %12.9f\n",
             peak_freq, peak_magnitude, left_max_magn, right_max_magn);
      for (size_t freq = (peak_freq & ~0x07) - 64; freq < (peak_freq & ~0x07) + 64; ++freq) {
        if (freq % 8 == 0) {
          printf("\n[%zu] ", freq);
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
    const double kNoiseFloor = Gain::DbToScale(kExpectedNoiseFloorDb);
    size_t peak_start = output_freq;
    double left_max_magn = 0;
    for (ssize_t freq = output_freq - 1; freq >= 0; --freq) {
      auto magn = sqrt(result.all_square_magnitudes[freq]);
      left_max_magn = std::max(left_max_magn, magn);
      if (magn > kNoiseFloor) {
        peak_start = freq;
      }
    }
    size_t peak_end = output_freq;
    double right_max_magn = 0;
    for (size_t freq = output_freq + 1; freq < result.all_square_magnitudes.size(); ++freq) {
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
  size_t ConvergenceFrames() const override { return 15 * kPacketFrames; }

 protected:
  void Init(int32_t clock_slew_ppm, size_t num_frames_input) override {
    zx::clock ref_clock = ::media::audio::clock::AdjustableCloneOfMonotonic();

    zx::clock::update_args args;
    args.reset().set_rate_adjust(clock_slew_ppm);
    ASSERT_TRUE(ref_clock.update(args) == ZX_OK) << "Clock rate_adjust failed";

    // Now that the clock is adjusted, remove ZX_RIGHT_WRITE before sending it (AudioCore never
    // adjusts client-submitted clocks anyway, but this makes it truly impossible).
    ref_clock = audio::clock::DuplicateClock(ref_clock).take_value();

    // Buffer up to 2s of data.
    output_ = CreateOutput({{0xff, 0x00}}, format_, kPayloadFrames);
    renderer_ = CreateAudioRenderer(format_, kPayloadFrames,
                                    fuchsia::media::AudioRenderUsage::MEDIA, std::move(ref_clock));

    // Any initial offset, plus the signal, should fit entirely into the ring buffer
    auto offset_before_input_start = std::max(OffsetFrames(), ConvergenceFrames());
    ASSERT_TRUE(num_frames_input + offset_before_input_start < kPayloadFrames)
        << "input signal is too big for the ring buffer";

    // In Step testing, the change in step length should exceed total ramp time, to be detectable.
    size_t num_frames_output = NumFramesOutput(clock_slew_ppm, num_frames_input);
    ASSERT_TRUE(std::abs(static_cast<ssize_t>(num_frames_input - num_frames_output)) >
                static_cast<ssize_t>(RingOut() + RingIn()))
        << "Change in signal length is too small to be detectable";
  }

  double NumFramesOutput(int32_t clock_slew_ppm, size_t num_frames_input) override {
    return static_cast<double>(num_frames_input) *
           (1e6 / (1e6 + static_cast<double>(clock_slew_ppm)));
  }
};

class AdjustableClockPipelineTest : public ClockSyncPipelineTest {
 public:
  // Expected device clock convergence time in frames.
  size_t ConvergenceFrames() const override { return 13 * kFrameRate; }

 protected:
  void Init(int32_t clock_slew_ppm, size_t num_frames_input) override {
    // Specify the clock rate for the output device.
    constexpr int32_t kMonotonicDomain = 0;
    constexpr int32_t kNonMonotonicDomain = 1;
    DeviceClockProperties clock_properties = {
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

  double NumFramesOutput(int32_t clock_slew_ppm, size_t num_frames_input) override {
    return num_frames_input;
  }
};

// The maximum clock skew is +/-1000PPM. These tests use a skew less than the maximum, so the two
// sides have a chance to converge (at the maximum, the slow side can never fully catch up).
TEST_F(MicroSrcPipelineTest, ImpulseFastReferenceClock) { RunImpulseTest(500, kFrameRate); }
TEST_F(MicroSrcPipelineTest, ImpulseSlowReferenceClock) { RunImpulseTest(-500, kFrameRate); }

TEST_F(AdjustableClockPipelineTest, ImpulseFastReferenceClock) { RunImpulseTest(500, kFrameRate); }
TEST_F(AdjustableClockPipelineTest, ImpulseSlowReferenceClock) { RunImpulseTest(-500, kFrameRate); }

TEST_F(MicroSrcPipelineTest, StepFastReferenceClock) { RunStepTest(500, kFrameRate); }
TEST_F(MicroSrcPipelineTest, StepSlowReferenceClock) { RunStepTest(-500, kFrameRate); }

TEST_F(AdjustableClockPipelineTest, StepFastReferenceClock) { RunStepTest(500, kFrameRate); }
TEST_F(AdjustableClockPipelineTest, StepSlowReferenceClock) { RunStepTest(-500, kFrameRate); }

// For best precision in measuring resultant signal frequency, input signal frequency should be
// high, but with room for upward slew without approaching the Nyquist limit(num_input_frames/2).
// Input frequency is a multiple of slew, to make expected resultant frequency a round number.
//
// Sine test input buffer length: the largest power-of-2 (in frames) that fits into 2 secs @ 96kHz.
// The numbers below work out to a frequency of 20k / (131072/96kHz) = 14.648 kHz.
TEST_F(MicroSrcPipelineTest, SineFastReferenceClock) { RunSineTest(500, 131072, 20000); }
TEST_F(MicroSrcPipelineTest, SineSlowReferenceClock) { RunSineTest(-500, 131072, 20000); }

TEST_F(AdjustableClockPipelineTest, SineFastReferenceClock) { RunSineTest(500, 131072, 20000); }
TEST_F(AdjustableClockPipelineTest, SineSlowReferenceClock) { RunSineTest(-500, 131072, 20000); }

}  // namespace media::audio::test

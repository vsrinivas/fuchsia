// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_MIXER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_MIXER_H_

#include <fuchsia/media/cpp/fidl.h>

#include <memory>

#include "src/media/audio/audio_core/mixer/constants.h"
#include "src/media/audio/audio_core/mixer/gain.h"
#include "src/media/audio/lib/format/frames.h"
#include "src/media/audio/lib/timeline/timeline_function.h"

namespace media::audio {

class Mixer {
 public:
  static constexpr uint32_t FRAC_ONE = 1u << kPtsFractionalBits;
  static constexpr uint32_t FRAC_HALF = FRAC_ONE >> 1;
  static constexpr uint32_t FRAC_MASK = FRAC_ONE - 1u;

  struct Bookkeeping;

  // SourceInfo
  //
  // This struct represents the state of the mix operation from a source point-of-view, specifically
  // the details needed by MixStage and other related objects, but not the inner resampling mixer
  // itself. For that reason, it largely concerns itself with clocks and long-running position
  // values that are maintained for detection of clock error.
  //
  struct SourceInfo {
    // This method resets the long-running and per-Mix position counters and is called upon a
    // destination discontinuity. If a next_dest_frame value is provided, set next_frac_source_frame
    // based on the dest_frames_to_frac_source_frames transform. Pre-setting next_src_pos_modulo
    // here is only helpful for very high resolution scenarios (and is speculative at best); we base
    // it on rate_modulo and denominator, not the dest-to-source transform.
    void ResetPositions(Bookkeeping& bookkeeping) { ResetPositions(0, bookkeeping); }
    void ResetPositions(int64_t target_dest_frame, Bookkeeping& bookkeeping) {
      bookkeeping.Reset();

      next_dest_frame = target_dest_frame;
      next_frac_source_frame =
          Fixed::FromRaw(dest_frames_to_frac_source_frames.Apply(target_dest_frame));
      if (bookkeeping.denominator) {
        next_src_pos_modulo =
            (target_dest_frame * bookkeeping.rate_modulo) % bookkeeping.denominator;
      }
      frac_source_error = Fixed(0);
    }

    // Only called by custom code when debugging, so can remain at INFO severity.
    void DisplayPositions(std::string tag = "") {
      FX_LOGS(INFO) << "0x" << std::hex << this << std::dec << " " << tag << ": next_dst "
                    << next_dest_frame << ", next_frac_src " << next_frac_source_frame.raw_value()
                    << ", next_src_pos_mod " << next_src_pos_modulo << ", frac_src_err "
                    << frac_source_error.raw_value();
    }

    // From their current values, advance the long-running positions by a number of dest frames.
    // Advancing by a negative number of frames should be infrequent, but we do support it.
    void AdvanceRunningPositionsBy(int32_t dest_frames, Bookkeeping& bookkeeping) {
      next_dest_frame += dest_frames;
      int64_t source_frame_delta = static_cast<int64_t>(bookkeeping.step_size) * dest_frames;

      if (bookkeeping.denominator) {
        // mod next_src_pos_modulo up into range if advance was negative. This is more clear (avoids
        // the error-prone negative modulo!) at negligible CPU cost.
        int64_t src_pos_modulo_delta = static_cast<int64_t>(bookkeeping.rate_modulo) * dest_frames;
        while (src_pos_modulo_delta < 0) {
          --source_frame_delta;
          src_pos_modulo_delta += bookkeeping.denominator;
        }
        next_src_pos_modulo += src_pos_modulo_delta;

        // mod next_src_pos_modulo back down into range.
        if (next_src_pos_modulo >= bookkeeping.denominator) {
          source_frame_delta += (next_src_pos_modulo / bookkeeping.denominator);
          next_src_pos_modulo %= bookkeeping.denominator;
        }
      }
      next_frac_source_frame += Fixed::FromRaw(source_frame_delta);
    }

    // From current values, advance long-running positions to the specified absolute dest frame num.
    // Advancing in negative direction should be infrequent, but we do support it.
    void AdvanceRunningPositionsTo(int32_t dest_target_frame, Bookkeeping& bookkeeping) {
      AdvanceRunningPositionsBy(dest_target_frame - next_dest_frame, bookkeeping);
    }

    // This translates a source reference_clock value into a source subframe value.
    // The output values of this function are in 19.13 source subframes.
    TimelineFunction source_ref_clock_to_frac_source_frames;

    // This translates a CLOCK_MONOTONIC time to a source subframe, accounting for the source
    // reference clock. The output values of this function are in 19.13 source subframes.
    TimelineFunction clock_mono_to_frac_source_frames;

    // This translates a destination frame to a source subframe, accounting for both the source and
    // dest reference clocks. The output values of this function are in 19.13 source subframes.
    TimelineFunction dest_frames_to_frac_source_frames;

    // Per-job state, used by the MixStage around a loop of potentially multiple calls to Mix().
    // 32 bits is adequate: even at 192kHz the MixJob could be 6+ hours (is generally 10 ms).
    uint32_t frames_produced;

    // Maintained since the stream started, relative to dest or source reference clocks.
    //
    // This tracks the upcoming destination frame number, for this stream. This should match the
    // frame value passed to callers of Mix(), via ReadLock. If this is not the case, then there has
    // been a discontinuity in the destination stream and our running positions should be reset.
    int64_t next_dest_frame = 0;

    // This tracks the upcoming source fractional frame value for this stream. This value will be
    // incremented by the amount of source consumed by each Mix() call, an amount is determined by
    // step_size and rate_modulo/denominator. If next_dest_frame does not match the requested dest
    // frame value, this stream's running position is reset by recalculating next_frac_source_frame
    // from the dest_frames_to_frac_source_frames TimelineFunction.
    Fixed next_frac_source_frame{0};

    // This field is similar to src_pos_modulo and relates to the same rate_modulo and denominator.
    // It expresses the stream's long-running position modulo (whereas src_pos_modulo is per-Mix).
    uint64_t next_src_pos_modulo = 0;

    // This field represents the difference between next_frac_souce_frame (maintained on a relative
    // basis after each Mix() call), and the clock-derived absolute source position (calculated from
    // the dest_frames_to_frac_source_frames TimelineFunction). Upon a dest frame discontinuity,
    // next_frac_source_frame is reset to that clock-derived value, and this field is set to zero.
    // This field sets the direction and magnitude of any steps taken for clock reconciliation.
    Fixed frac_source_error{0};

    // This field is used to ensure that when a stream first starts, we establish the offset
    // between destination frame and source fractional frame using clock calculations. We want to
    // only do this _once_, because thereafter we use ongoing step_size to track whether we are
    // drifting out of sync, rather than use a clock calculation each time (which would essentially
    // "jam-sync" each mix buffer, possibly creating gaps or overlaps in the process).
    bool running_pos_established = false;
  };

  // Bookkeeping
  //
  // This struct represents the state of that mix operation from the source point-of-view. In a Mix,
  // the relationship between sources and destinations is many-to-one; this struct includes details
  // about its source stream, specifically those needed by the inner resampling mixer object.
  //
  // When calling Mix(), we communicate resampling details with three parameters found in the
  // Bookkeeping. To augment step_size, rate_modulo and denominator arguments capture any remaining
  // aspects that are not expressed by the 19.13 fixed-point step_size. Because frac_src_offset and
  // step_size both use the 19.13 format, they exhibit the same precision limitations. These rate
  // and position limitations are reiterated upon the start of each mix job.
  //
  // Just as we address *rate* with rate_modulo and denominator, likewise for *position* Bookkeeping
  // uses src_pos_modulo to track initial and ongoing modulo of src subframes. This work is only
  // partially complete; the remaining work (e.g., setting src_pos_modulo's initial value to
  // anything other than 0) is tracked with MTWN-128.
  //
  // With *rate*, the effect of inaccuracy accumulates over time, causing measurable distortion that
  // cripples larger mix jobs. For *position*, a change in mix job size affects distortion frequency
  // but not distortion amplitude. Having added this to Bookkeeping, any residual effect seems to
  // be below audible thresholds; for now we are deferring the remaining work.
  struct Bookkeeping {
    // This object maintains gain values contained in the mix path. This includes source gain and a
    // snapshot of destination gain (Gain objects correspond with source streams, so the definitive
    // value for destination gain is naturally owned elsewhere). In the future, this object may
    // include explicit Mute states for source and dest stages, a separately controlled Usage gain
    // stage, and/or the ability to ramp one or more of these gains over time. Gain accepts level in
    // dB, and provides gainscale as float multiplier.
    Gain gain;
    //
    // Related to gain, the Bookkeeping struct should contain: the rechannel matrix (eventually).

    static constexpr uint32_t kScaleArrLen = 960;
    std::unique_ptr<Gain::AScale[]> scale_arr = std::make_unique<Gain::AScale[]>(kScaleArrLen);

    // Bookkeeping should contain the rechannel matrix eventually. Mapping from one channel
    // configuration to another is essentially an MxN gain table that can be applied during Mix().

    // This 19.13 fixed-point value represents how much to increment our sampling position in the
    // source (src) stream, for each output (dest) frame produced.
    uint32_t step_size = Mixer::FRAC_ONE;

    // If step_size cannot perfectly express the mix's resampling ratio, this parameter (along with
    // subsequent denominator) expresses leftover precision. When non-zero, rate_modulo and
    // denominator express a fractional value of the step_size unit that src position should
    // advance, for each dest frame.
    uint64_t rate_modulo = 0;

    // If step_size cannot perfectly express the mix's resampling ratio, this parameter (along with
    // precedent rate_modulo) expresses leftover precision. When non-zero, rate_modulo and
    // denominator express a fractional value of the step_size unit that src position should
    // advance, for each dest frame.
    uint64_t denominator = 0;

    // If frac_src_offset cannot perfectly express the source's position, this parameter (along with
    // denominator) expresses any leftover precision. When present, src_pos_modulo and denominator
    // express a fractional value of the frac_src_offset unit to be used when src position advances.
    uint64_t src_pos_modulo = 0;

    // This method resets the local position accounting (including gain ramping), but not the
    // long-running positions. This is called upon a source discontinuity.
    void Reset() {
      src_pos_modulo = 0;
      gain.CompleteSourceRamp();
    }
  };

  virtual ~Mixer() = default;

  //
  // Resampler enum
  //
  // This enum lists Fuchsia's available resamplers. Callers of Mixer::Select optionally use this
  // to specify a resampler type. Default allows an algorithm to select a resampler based on the
  // ratio of incoming-to-outgoing rates (currently we use WindowedSinc for all ratios except 1:1).
  enum class Resampler {
    Default = 0,
    SampleAndHold,
    LinearInterpolation,
    WindowedSinc,
  };

  //
  // Select
  //
  // Select an appropriate mixer instance, based on an optionally-specified resampler type, or else
  // by the properties of source/destination formats.
  //
  // When calling Mixer::Select, resampler_type is optional. If a caller specifies a particular
  // resampler, Mixer::Select will either instantiate what was requested or return nullptr, even if
  // it otherwise could have successfully instantiated a different one. Setting this to non-Default
  // says "I know exactly what I need: I want you to fail rather than give me anything else."
  //
  // If resampler_type is absent or Default, this is determined by algorithm. For optimum system
  // performance across changing conditions, callers should use Default whenever possible.
  static std::unique_ptr<Mixer> Select(const fuchsia::media::AudioStreamType& src_format,
                                       const fuchsia::media::AudioStreamType& dest_format,
                                       Resampler resampler_type = Resampler::Default);

  //
  // Mix
  //
  // Perform a mixing operation from source buffer into destination buffer.
  //
  // @param dest
  // The pointer to the destination buffer, into which frames will be mixed.
  //
  // @param dest_frames
  // The total number of frames of audio which comprise the destination buffer.
  //
  // @param dest_offset
  // The pointer to the offset (in output frames) from start of dest buffer, at
  // which we should mix destination frames. Essentially this tells Mix how many
  // 'dest' frames to skip over, when determining where to place the first mixed
  // output frame. When Mix has finished, dest_offset is updated to indicate the
  // destination buffer offset of the next frame to be mixed.
  //
  // @param src
  // Pointer to source buffer, containing frames to be mixed to the dest buffer.
  //
  // @param frac_src_frames
  // Total number (in 19.13 fixed) of incoming subframes in the source buffer.
  //
  // @param frac_src_offset
  // A pointer to the offset (in fractional source frames) from start of src
  // buffer, at which the first source frame should be sampled. When Mix has
  // finished, frac_src_offset will be updated to indicate the offset of the
  // sampling position of the next frame to be sampled.
  //
  // @param accumulate
  // When true, Mix will accumulate into the destination buffer (sum the mix
  // results with existing values in the dest buffer). When false, Mix will
  // overwrite any existing destination buffer values with its mix output.
  //
  // @return True if the mixer is finished with this source data and will not
  // need it in the future. False if the mixer has not consumed the entire
  // source buffer and will need more of it in the future.
  //
  // TODO(mpuryear): Change parameter frac_src_frames to src_frames (change
  // subframes to int frames), as this was never intended to be fractional.
  //
  // TODO(37356): Make frac_src_frames and frac_src_offset typesafe
  virtual bool Mix(float* dest, uint32_t dest_frames, uint32_t* dest_offset, const void* src,
                   uint32_t frac_src_frames, int32_t* frac_src_offset, bool accumulate) = 0;
  //
  // Reset
  //
  // Reset the internal state of the mixer. Will be called every time there is
  // a discontinuity in the source stream. Mixer implementations should reset
  // anything related to their internal filter state.
  virtual void Reset() { bookkeeping().Reset(); }

  //
  // Filter widths
  //
  // The positive and negative widths of the filter for this mixer, expressed in
  // fractional (19.13 fixed) source subframe units. These widths convey which
  // source frames will be referenced by the filter, when producing output for a
  // specific instant in time. Positive filter width refers to how far forward
  // (positively) the filter looks, from the PTS in question; negative filter
  // width refers to how far backward (negatively) the filter looks, from that
  // same PTS. Specifically...
  //
  // Let:
  // P = pos_filter_width()
  // N = neg_filter_width()
  // S = An arbitrary point in time at which the source stream will be sampled.
  // X = The PTS of an source frame.
  //
  // If (X >= (S - N)) && (X <= (S + P))
  // Then source frame X is within the filter and contributes to mix operation.
  //
  // Conversely, source frame X contributes to the output samples S where
  //  (S >= X - P)  and  (S <= X + N)
  //
  inline Fixed pos_filter_width() const { return pos_filter_width_; }
  inline Fixed neg_filter_width() const { return neg_filter_width_; }

  SourceInfo& source_info() { return source_info_; }
  const SourceInfo& source_info() const { return source_info_; }

  Bookkeeping& bookkeeping() { return bookkeeping_; }
  const Bookkeeping& bookkeeping() const { return bookkeeping_; }

  // Eagerly precompute any needed data. If not called, that data should be lazily computed
  // on the first call to Mix().
  // TODO(45074): This is for tests only and can be removed once filter creation is eager.
  virtual void EagerlyPrepare() {}

 protected:
  Mixer(uint32_t pos_filter_width, uint32_t neg_filter_width);

 private:
  Fixed pos_filter_width_;
  Fixed neg_filter_width_;
  Bookkeeping bookkeeping_;

  // The subset of per-stream position accounting info not needed by the inner resampling mixer.
  // This is only located here temporarily; we will move this to the MixStage.
  SourceInfo source_info_;
};  // namespace media::audio

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_MIXER_H_

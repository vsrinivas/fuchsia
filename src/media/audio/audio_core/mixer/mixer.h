// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_MIXER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_MIXER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/trace/event.h>

#include <memory>

#include "src/media/audio/audio_core/mixer/constants.h"
#include "src/media/audio/audio_core/mixer/gain.h"
#include "src/media/audio/lib/format/constants.h"
#include "src/media/audio/lib/timeline/timeline_function.h"

namespace media::audio {

// Enable to emit trace events containing the Mixer position state.
constexpr bool kMixerPositionTraceEvents = false;

// The Mixer class provides format-conversion, rechannelization, rate-connversion, and gain/mute
// scaling. Each source in a multi-stream mix has its own Mixer instance. When Mixer::Mix() is
// called, it adds that source's contribution, by reading audio from its source, generating the
// appropriately processed result, and summing this output into a common destination buffer.
class Mixer {
 public:
  struct Bookkeeping;

  // SourceInfo
  //
  // This struct contains position-related info needed by MixStage, to correctly feed this Mixer.
  //
  // This includes source-specific clock transforms (source-ref-clock-to-source-frame,
  // clock-mono-to-source-frame and dest-frame-to-source-frame), long-running source/dest positions
  // (used for clock error detection/tuning) and per-job frame count (used to produce sufficient
  // output, across multiple Mix calls). SourceInfo is established when a Mixer is created. Clock-
  // related info is updated before calling Mix, to generate the Bookkeeping values it needs.
  // Position-related values are updated based on the return values from Mix.
  //
  // SourceInfo is not used by Mixer::Mix(). It could be moved to a per-stream facet of MixStage.
  //
  struct SourceInfo {
    // This method resets long-running and per-Mix position counters, called when a destination
    // discontinuity occurs. It sets next_dest_frame to the specified value and calculates
    // next_source_frame based on the dest_frames_to_frac_source_frames transform.
    void ResetPositions(int64_t target_dest_frame, Bookkeeping& bookkeeping) {
      if (kMixerPositionTraceEvents) {
        TRACE_DURATION("audio", __func__, "target_dest_frame", target_dest_frame);
      }
      next_dest_frame = target_dest_frame;
      next_source_frame =
          Fixed::FromRaw(dest_frames_to_frac_source_frames.Apply(target_dest_frame));
      bookkeeping.source_pos_modulo = 0;
      source_pos_error = zx::duration(0);
      initial_position_is_set = true;
    }

    // Used by custom code when debugging.
    std::string PositionsToString(std::string tag = "") {
      return tag + ": next_dest " + std::to_string(next_dest_frame) + ", next_source " +
             std::to_string(next_source_frame.raw_value()) + ", pos_err " +
             std::to_string(source_pos_error.get());
    }

   private:
    // From current values, advance the long-running positions by dest_frames.
    // "Advancing" negatively should be infrequent, but we support it.
    void AdvancePositionsBy(int64_t dest_frames, Bookkeeping& bookkeeping,
                            bool advance_source_pos_modulo) {
      int64_t frac_source_frame_delta = bookkeeping.step_size.raw_value() * dest_frames;
      if (kMixerPositionTraceEvents) {
        TRACE_DURATION("audio", __func__, "dest_frames", dest_frames, "advance_source_pos_modulo",
                       advance_source_pos_modulo, "frac_source_frame_delta",
                       frac_source_frame_delta);
      }

      if (bookkeeping.rate_modulo()) {
        // rate_mod and pos_mods can be as large as UINT64_MAX-1; use 128-bit to avoid overflow
        __int128_t denominator_128 = bookkeeping.denominator();
        __int128_t source_pos_modulo_128 =
            static_cast<__int128_t>(bookkeeping.rate_modulo()) * dest_frames;
        if (advance_source_pos_modulo) {
          source_pos_modulo_128 += bookkeeping.source_pos_modulo;
        }
        // else, assume (for now) that source_pos_modulo started at zero.

        // TODO(mpuryear): remove negative-position-advance support, when no longer needed
        // If advance was negative, mod the potentially-negative pos_modulo values up into range.
        // Negative modulo is error-prone and potentially-undefined-behavior; avoiding it is more
        // clear at little additional CPU cost (any negative position advance should be small).
        while (source_pos_modulo_128 < 0) {
          frac_source_frame_delta -= 1;
          source_pos_modulo_128 += denominator_128;
        }
        // TODO(mpuryear): remove negative-position-advance support once this is no longer needed

        // mod these back down into range.
        auto new_source_pos_modulo = static_cast<uint64_t>(source_pos_modulo_128 % denominator_128);
        if (advance_source_pos_modulo) {
          bookkeeping.source_pos_modulo = new_source_pos_modulo;
        } else {
          // source_pos_modulo has already been advanced; it is already at its eventual value.
          // new_source_pos_modulo is what source_pos_modulo WOULD have become, if it had started at
          // zero. Now advance source_pos_modulo_128 by the difference (which is what its initial
          // value must have been), just in case this causes frac_source_frame_delta to increment.
          source_pos_modulo_128 += bookkeeping.source_pos_modulo;
          source_pos_modulo_128 -= new_source_pos_modulo;
          if (bookkeeping.source_pos_modulo < new_source_pos_modulo) {
            source_pos_modulo_128 += denominator_128;
          }
        }
        frac_source_frame_delta += static_cast<int64_t>(source_pos_modulo_128 / denominator_128);
      }
      next_source_frame = Fixed::FromRaw(next_source_frame.raw_value() + frac_source_frame_delta);
      next_dest_frame += dest_frames;
      if (kMixerPositionTraceEvents) {
        TRACE_DURATION("audio", "AdvancePositionsBy End", "nest_source_frame",
                       next_source_frame.Integral().Floor(), "next_source_frame.frac",
                       next_source_frame.Fraction().raw_value(), "next_dest_frame", next_dest_frame,
                       "source_pos_modulo", bookkeeping.source_pos_modulo);
      }
    }

   public:
    void UpdateRunningPositionsBy(int64_t dest_frames, Bookkeeping& bookkeeping) {
      AdvancePositionsBy(dest_frames, bookkeeping, false);
    }

    void AdvanceAllPositionsBy(int64_t dest_frames, Bookkeeping& bookkeeping) {
      AdvancePositionsBy(dest_frames, bookkeeping, true);
    }

    // From current values, advance long-running positions to the specified absolute dest frame num.
    // "Advancing" negatively should be infrequent, but we support it.
    void AdvanceAllPositionsTo(int64_t dest_target_frame, Bookkeeping& bookkeeping) {
      int64_t dest_frames = dest_target_frame - next_dest_frame;
      AdvancePositionsBy(dest_frames, bookkeeping, true);
    }

    // This translates a source reference_clock value into a source subframe value.
    // The output values of this function are in source subframes (raw_value of the Fixed type).
    TimelineFunction source_ref_clock_to_frac_source_frames;

    // This translates CLOCK_MONOTONIC time to source subframe, accounting for the source reference
    // clock. The output values of this function are source subframes (raw_value of the Fixed type).
    TimelineFunction clock_mono_to_frac_source_frames;

    // This translates destination frame to source subframe, accounting for both source and dest
    // reference clocks. This function outputs source subframes (raw_value of the Fixed type).
    TimelineFunction dest_frames_to_frac_source_frames;

    // Per-job state, used by the MixStage around a loop of potentially multiple calls to Mix().
    int64_t frames_produced;

    // Maintained since the stream started, relative to dest or source reference clocks.
    //
    // This tracks the upcoming destination frame number, for this stream. This should match the
    // frame value passed to callers of Mix(), via ReadLock. If this is not the case, then there has
    // been a discontinuity in the destination stream and our running positions should be reset.
    int64_t next_dest_frame = 0;

    // This tracks the upcoming source fractional frame value for this stream. This value will be
    // incremented by the amount of source consumed by each Mix() call, an amount is determined by
    // step_size and rate_modulo/denominator. If next_dest_frame does not match the requested dest
    // frame value, this stream's running position is reset by recalculating next_source_frame
    // from the dest_frames_to_frac_source_frames TimelineFunction.
    Fixed next_source_frame{0};

    // This field represents the difference between next_frac_souce_frame (maintained on a relative
    // basis after each Mix() call), and the clock-derived absolute source position (calculated from
    // the dest_frames_to_frac_source_frames TimelineFunction). Upon a dest frame discontinuity,
    // next_source_frame is reset to that clock-derived value, and this field is set to zero.
    // This field sets the direction and magnitude of any steps taken for clock reconciliation.
    zx::duration source_pos_error{0};

    // This field is used to ensure that when a stream first starts, we establish the offset
    // between destination frame and source fractional frame using clock calculations. We want to
    // only do this _once_, because thereafter we use ongoing step_size to track whether we are
    // drifting out of sync, rather than use a clock calculation each time (which would essentially
    // "jam-sync" each mix buffer, possibly creating gaps or overlaps in the process).
    bool initial_position_is_set = false;
  };

  // Bookkeeping
  //
  // This struct contains all of (and nothing but) the state needed by the Mix() function.
  //
  // Bookkeeping contains per-stream info related to gain (and gain ramping) and rate-conversion.
  // Values are set by MixStage; the only parameter changed by Mix() is source_pos_modulo.
  //
  // When calling Mix(), we communicate rate-resampling details with three parameters found in the
  // Bookkeeping. Step_size is augmented by rate_modulo and denominator arguments that capture the
  // precision that cannot be expressed by the fixed-point step_size.
  //
  // Source_offset and step_size use the same fixed-point format, so they have identical precision
  // limitations. Source_pos_modulo, then, represents fractions of source subframe position.
  struct Bookkeeping {
    explicit Bookkeeping(Gain::Limits gain_limits = Gain::Limits{}) : gain(gain_limits) {}

    // This object maintains gain values in the mix path, including source gain and a snapshot of
    // destination gain (the definitive value for destination gain is owned elsewhere). Gain accepts
    // level in dB, and provides gainscale as float multiplier.
    Gain gain;

    static constexpr int64_t kScaleArrLen = 960;
    std::unique_ptr<Gain::AScale[]> scale_arr = std::make_unique<Gain::AScale[]>(kScaleArrLen);

    // Bookkeeping should contain the rechannel matrix eventually. Mapping from one channel
    // configuration to another is essentially an MxN gain table that can be applied during Mix().

    // This fixed-point value is a fractional "stride" for the source: how much to increment our
    // sampling position in the source stream, for each output (dest) frame produced.
    Fixed step_size = kOneFrame;

    // This parameter (along with denominator) expresses leftover rate precision that step_size
    // cannot express. When non-zero, rate_modulo and denominator express a fractional value of the
    // step_size unit that src position should advance, for each dest frame.
    uint64_t rate_modulo() const { return rate_modulo_; }

    // This parameter (along with rate_modulo and source_pos_modulo) expresses leftover rate and
    // position precision that step_size and source_offset (respectively) cannot express.
    uint64_t denominator() const { return denominator_; }

    // This parameter (along with denominator) expresses leftover position precision that Mix
    // parameter cannot express. When present, source_pos_modulo and denominator express a
    // fractional value of the source_offset unit, for additional precision on current position.
    // Note: this field is also referenced when updating long-running position fields in SourceInfo.
    // TODO(fxbug.dev/85108): Refactor Bookkeeping and SourceInfo.
    uint64_t source_pos_modulo = 0;

    void SetRateModuloAndDenominator(uint64_t rate_mod, uint64_t denom,
                                     SourceInfo* info = nullptr) {
      if (kMixerPositionTraceEvents) {
        TRACE_DURATION("audio", __func__, "rate_mod", rate_mod, "denom", denom);
      }
      FX_CHECK(denom > 0);
      FX_CHECK(rate_mod < denom);
      if (!rate_mod) {
        rate_modulo_ = 0;
        denominator_ = 1;
        source_pos_modulo = 0;
        return;
      }

      rate_modulo_ = rate_mod;
      if (denom != denominator_) {
        __uint128_t temp_source_pos_mod =
            (static_cast<__uint128_t>(source_pos_modulo) * denom) / denominator_;
        denominator_ = denom;
        source_pos_modulo = static_cast<uint64_t>(temp_source_pos_mod);
      }
    }

   private:
    uint64_t rate_modulo_ = 0;
    uint64_t denominator_ = 1;
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
  static std::unique_ptr<Mixer> Select(const fuchsia::media::AudioStreamType& source_format,
                                       const fuchsia::media::AudioStreamType& dest_format,
                                       Resampler resampler_type = Resampler::Default,
                                       Gain::Limits gain_limits = Gain::Limits{});

  //
  // Mix
  //
  // Perform a mixing operation from source buffer into destination buffer.
  //
  // @param dest_ptr
  // The pointer to the destination buffer, into which frames will be mixed.
  //
  // @param dest_frames
  // The total number of frames of audio which comprise the destination buffer.
  //
  // @param dest_offset_ptr
  // The pointer to the offset (in output frames) from start of dest buffer, at
  // which we should mix destination frames. Essentially this tells Mix how many
  // 'dest' frames to skip over, when determining where to place the first mixed
  // output frame. When Mix has finished, dest_offset is updated to indicate the
  // destination buffer offset of the next frame to be mixed.
  //
  // @param source_void_ptr
  // Pointer to source buffer, containing frames to be mixed to the dest buffer.
  //
  // @param source_frames
  // Total number of incoming frames in the source buffer.
  //
  // @param source_offset_ptr
  // A pointer to the offset from start of source buffer, at which the first source
  // frame should be sampled. When Mix has finished, source_offset will be updated
  // to indicate the offset of the sampling position of the next frame to be sampled.
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
  // Within Mix(), the following source/dest/rate constraints are enforced:
  //  * source_frames           must be at least 1
  //  * source_offset           must be at least -pos_filter_width
  //                            cannot exceed frac_source_frames
  //
  //  * dest_offset             cannot exceed dest_frames
  //
  //  * step_size               must exceed zero
  //  * rate_modulo             must be either zero or less than denominator
  //  * source_position_modulo  must be either zero or less than denominator
  //
  virtual bool Mix(float* dest_ptr, int64_t dest_frames, int64_t* dest_offset_ptr,
                   const void* source_void_ptr, int64_t source_frames, Fixed* source_offset_ptr,
                   bool accumulate) = 0;
  //
  // Reset
  //
  // Reset the internal state of the mixer. Will be called every time there is
  // a discontinuity in the source stream. Mixer implementations should reset
  // anything related to their internal filter state.
  virtual void Reset() {}

  //
  // Filter widths
  //
  // The positive and negative widths of the filter for this mixer, expressed in fixed-point
  // fractional source subframe units. These widths convey which source frames will be referenced by
  // the filter, when producing output for a specific instant in time. Positive filter width refers
  // to how far forward (positively) the filter looks, from the PTS in question; negative filter
  // width refers to how far backward (negatively) the filter looks, from that same PTS.
  //
  // Note that filter widths do NOT include the center PTS in question, so in that regard they are
  // not equivalent to the filter's length.
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
  // TODO(fxbug.dev/45074): This is for tests only and can be removed once filter creation is eager.
  virtual void EagerlyPrepare() {}

 protected:
  Mixer(Fixed pos_filter_width, Fixed neg_filter_width, Gain::Limits gain_limits);

 private:
  const Fixed pos_filter_width_;
  const Fixed neg_filter_width_;
  Bookkeeping bookkeeping_;

  // The subset of per-stream position accounting info not needed by the inner resampling mixer.
  // This is only located here temporarily; we will move this to the MixStage.
  SourceInfo source_info_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_MIXER_H_

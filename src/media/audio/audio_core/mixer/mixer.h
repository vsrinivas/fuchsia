// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_MIXER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_MIXER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <cmath>
#include <limits>
#include <memory>

#include <ffl/string.h>

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
    }

    // Used by custom code when debugging.
    std::string PositionsToString(std::string tag = "") {
      return tag + ": next_dest " + std::to_string(next_dest_frame) + ", next_source " +
             ffl::String(next_source_frame, ffl::String::DecRational).c_str() + ", pos_err " +
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

    // Translate a running source position (Fixed plus source position modulo | denominator) into
    // MONOTONIC nanoseconds, using the nanosec-to-Fixed TimelineFunction.
    //
    // To scale from reference units to subject units, TimelineFunction::Apply does this:
    //    (in_param - reference_offset) * subject_delta / reference_delta + subject_offset
    //
    // TimelineFunction clock_mono_to_frac_source_frames contains the correspondence we need (but in
    // inverse: subject is frac_source; reference is MONOTONIC nsecs). To more accurately calculate
    // MONOTONIC nsecs from frac_source (including modulo), we scale the function by denominator;
    // then we can include source_pos_modulo at full resolution and round when reducing to nsec.
    // So in the TimelineFunction::Apply equation above, we will use:
    //    in_param:         (next_source_frame.raw_value * denom + source_pos_modulo)
    //    reference_offset: (clock_mono_to_frac_source_frames.subject_time * denom)
    //    subject_delta and reference_delta: used as-is (factor denom out of both)
    //                      while remembering that the rate is inverted
    //    subject_offset:   (clock_mono_to_frac_source_frames.reference_time * denom)
    //
    // Because all the initial factors are 64-bit, our denom-scaled version must use int128.
    // Even then, we might overflow depending on parameters, so we scale back denom if needed.
    //
    // TODO(fxbug.dev/86743): Generalize this (remove the scale-down denominator optimization) and
    // extract the functionality into a 128-bit template specialization of audio/lib/timeline
    // TimelineRate and TimelineFunction.
    static zx::time MonotonicNsecFromRunningSource(const SourceInfo& info,
                                                   uint64_t initial_source_pos_modulo,
                                                   uint64_t denominator) {
      FX_DCHECK(initial_source_pos_modulo < denominator);

      __int128_t frac_src_from_offset =
          static_cast<__int128_t>(info.next_source_frame.raw_value()) -
          info.clock_mono_to_frac_source_frames.subject_time();

      // The calculation that would first overflow a int128 is the partial calculation:
      //    frac_src_offset * denominator * reference_delta
      // For our passed-in params, the maximal denominator that will NOT overflow is:
      //    int128::max() / abs(frac_src_from_offset) / reference_delta
      //
      // __int128_t doesn't have an abs() right now so we do it manually.
      //  We add one fractional frame to accommodate any pos_modulo contribution.
      __int128_t abs_frac_src_from_offset =
          (frac_src_from_offset < 0 ? -frac_src_from_offset : frac_src_from_offset) + 1;
      __int128_t max_denominator = std::numeric_limits<__int128_t>::max() /
                                   abs_frac_src_from_offset /
                                   info.clock_mono_to_frac_source_frames.reference_delta();

      __int128_t src_pos_mod_128 = static_cast<__int128_t>(initial_source_pos_modulo);
      __int128_t denom_128 = static_cast<__int128_t>(denominator);

      // A min denominator of 2 allows us to round to the nearest nsec, rather than floor.
      if (denom_128 == 1) {
        denom_128 = 2;
        // If denom is 1 then src_pos_mod_128 is 0: no point in doubling it
      } else {
        // If denominator is large enough to cause overflow, scale it down for this calculation
        // In the worst-case we may scale this down by more than 32 bits, but that's OK.
        while (denom_128 > max_denominator) {
          denom_128 >>= 1;
          src_pos_mod_128 >>= 1;
        }
        // While scaling down, don't let source_pos_modulo become equal to denominator.
        // Don't let 28|31 reduce to 7|7 -- make it 6|7 instead.
        src_pos_mod_128 = std::min(src_pos_mod_128, denom_128 - 1);
      }

      // First portion of our TimelineFunction::Apply
      __int128_t frac_src_modulo = frac_src_from_offset * denom_128 + src_pos_mod_128;

      // Middle portion, including rate factors
      __int128_t monotonic_modulo =
          frac_src_modulo * info.clock_mono_to_frac_source_frames.reference_delta();
      monotonic_modulo /= info.clock_mono_to_frac_source_frames.subject_delta();

      // Final portion, including adding in the monotonic offset
      __int128_t monotonic_offset_modulo =
          static_cast<__int128_t>(info.clock_mono_to_frac_source_frames.reference_time()) *
          denom_128;
      monotonic_modulo += monotonic_offset_modulo;

      // While reducing from nsec_modulo to nsec, we add denom_128/2 in order to round.
      __int128_t final_monotonic = (monotonic_modulo + denom_128 / 2) / denom_128;
      // final_monotonic is 128-bit so we can double-check that we haven't overflowed.
      // But we reduced denom_128 as needed to avoid all overflows.
      FX_DCHECK(final_monotonic <= std::numeric_limits<int64_t>::max() &&
                final_monotonic >= std::numeric_limits<int64_t>::min())
          << "0x" << std::hex << static_cast<uint64_t>(final_monotonic >> 64) << "'"
          << static_cast<uint64_t>(final_monotonic & std::numeric_limits<uint64_t>::max());

      return zx::time(static_cast<zx_time_t>(final_monotonic));
    }

    // This translates source reference_clock value (ns) into a source subframe value.
    // Output values of this function are source subframes (raw_value of the Fixed type).
    TimelineFunction source_ref_clock_to_frac_source_frames;

    // This field is used to ensure that when a stream timeline changes, we re-establish the offset
    // between destination frame and source fractional frame using clock calculations. If the
    // timeline hasn't changed, we use step_size calculations to track whether we are drifting.
    uint32_t source_ref_clock_to_frac_source_frames_generation = kInvalidGenerationId;

    // This translates CLOCK_MONOTONIC time to source subframe. Output values of this function are
    // source subframes (raw_value of the Fixed type).
    // This TLF entails the source rate as well as the source reference clock.
    TimelineFunction clock_mono_to_frac_source_frames;

    // This translates destination frame to source subframe. Output values of this function are
    // source subframes (raw_value of the Fixed type).
    // It represents the INTENDED dest-to-source relationship based on latest clock info.
    // The actual source position chases this timeline, via clock synchronization.
    // Thus, the TLF entails both source and dest rates and both source and dest reference clocks,
    // but NOT any additional micro-SRC being applied.
    TimelineFunction dest_frames_to_frac_source_frames;

    // Per-job state, used by the MixStage around a loop of potentially multiple calls to Mix().
    int64_t frames_produced;

    // These fields track our position in the destination and source streams. It may seem
    // sufficient to track next_dest_frame and use that to compute our source position:
    //
    //   next_source_frame =
    //       dest_frames_to_frac_source_frames.Apply(next_dest_frame)
    //
    // In practice, there are two reasons this is not sufficient:
    //
    //   1. Since next_source_frame typically increments by a fractional step size, it needs
    //      to be updated with more precision than supported by a Fixed alone. The full-precision
    //      next_source_frame is actually:
    //
    //          next_source_frame + bookkeeping.next_src_pos_modulo / bookkeeping.denominator
    //
    //      Where the full-precision step size is:
    //
    //          bookkeeping.step_size + bookkeeping.rate_modulo / bookkeeping.denominator
    //
    //   2. When reconciling clocks using micro SRC, next_source_frame may deviate from the ideal
    //      position (as determined by dest_frames_to_frac_source_frames) until the clocks are
    //      synchronized and source_pos_error = 0.
    //
    // We use the dest_frames_to_frac_source_frames transformation only at discontinuities in the
    // source stream.
    int64_t next_dest_frame = 0;
    Fixed next_source_frame{0};

    // This field represents the difference between next_source_frame (maintained on a relative
    // basis after each Mix() call), and the clock-derived absolute source position (calculated from
    // the dest_frames_to_frac_source_frames TimelineFunction). Upon a dest frame discontinuity,
    // next_source_frame is reset to that clock-derived value, and this field is set to zero. This
    // field sets the direction and magnitude of any steps taken for clock reconciliation.
    zx::duration source_pos_error{0};
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

    static Fixed DestLenToSourceLen(int64_t dest_frames, Fixed step_size, uint64_t rate_modulo,
                                    uint64_t denominator, uint64_t initial_source_pos_modulo) {
      // rate_modulo and denominator are arbitrarily large int64, so we must up-cast to 128-bit.
      __int128_t running_modulo =
          static_cast<__int128_t>(rate_modulo) * static_cast<__int128_t>(dest_frames) +
          static_cast<__int128_t>(initial_source_pos_modulo);
      // But rate_modulo and source_pos_modulo < denominator, so mod_contribution <= dest_frames
      __int128_t mod_contribution = running_modulo / static_cast<__int128_t>(denominator);
      FX_DCHECK(mod_contribution <= std::numeric_limits<int64_t>::max());

      // Max step_size is 192, which is 21 bits in Fixed (8.13). Also, mod_contribution cannot
      // exceed dest_frames, which means
      //     source_length_raw <= (step_size.raw_value() + 1) * dest_frames
      // Thus source_length_raw will overflow an int64 only if dest_frames >= 2^63/(192*2^13+1),
      // which is dest_frames > 5.86e12, which is dest_frames > 353 days @ 192khz.
      int64_t source_length_raw =
          step_size.raw_value() * dest_frames + static_cast<int64_t>(mod_contribution);

      return Fixed::FromRaw(source_length_raw);
    }

    // How many steps are needed to meet OR EXCEED the specified delta, using the specified
    // step_size, rate_modulo, denominator and initial position modulo.
    static int64_t SourceLenToDestLen(Fixed delta, Fixed step_size, uint64_t rate_modulo,
                                      uint64_t denominator, uint64_t initial_pos_modulo) {
      FX_DCHECK(delta >= Fixed(0));
      FX_DCHECK(step_size >= Fixed::FromRaw(1));
      FX_DCHECK(denominator > 0);
      FX_DCHECK(rate_modulo < denominator);
      FX_DCHECK(initial_pos_modulo < denominator);

      if (rate_modulo == 0) {
        // Ceiling discards any fractional remainder less than Fixed::FromRaw(1) because it floors
        // to Fixed::FromRaw(1) precision before rounding up.
        int64_t steps = delta.raw_value() / step_size.raw_value();
        if (delta > step_size * steps) {
          ++steps;
        }
        return steps;
      }

      // Both calculations fit into int128: delta.raw_value and step_size.raw_value are both int64,
      // and denom|rate_modulo|initial_pos_modulo are each uint64_t.  The largest possible step_size
      // and denominator still leave more than enough room for the max possible rate_mod, and the
      // largest possible step_size_rebased exceeds the largest possible delta_rebased.
      auto delta_rebased =
          static_cast<__int128_t>(delta.raw_value()) * denominator - initial_pos_modulo;
      auto step_size_rebased =
          static_cast<__int128_t>(step_size.raw_value()) * denominator + rate_modulo;

      // We know this DCHECK holds, because if we divide both top and bottom by denominator, then
      // top is int64_t::max or less, and bottom is 1 or more.
      FX_DCHECK(delta_rebased / step_size_rebased <= std::numeric_limits<int64_t>::max());

      auto steps = static_cast<int64_t>(delta_rebased / step_size_rebased);
      if (delta_rebased % step_size_rebased) {
        ++steps;
      }
      return steps;
    }

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

    // Update rate_modulo|denominator|source_pos_modulo as a trio.
    // The 'source_pos' param and retval are relevant only after the long-running source position
    // has been set. This is not the case during initial mixer setup, for example.
    Fixed SetRateModuloAndDenominator(uint64_t new_rate_mod, uint64_t new_denom,
                                      Fixed source_pos = Fixed(0)) {
      if (kMixerPositionTraceEvents) {
        TRACE_DURATION("audio", __func__, "new_rate_mod", new_rate_mod, "new_denom", new_denom);
      }
      FX_CHECK(new_denom > 0);
      FX_CHECK(new_rate_mod < new_denom);
      FX_CHECK(denominator_ > 0) << "denominator: " << denominator_;
      FX_CHECK(rate_modulo_ < denominator_)
          << "rate_modulo: " << rate_modulo_ << ", denominator: " << denominator_;
      FX_CHECK(source_pos_modulo < denominator_)
          << "source_pos_modulo: " << source_pos_modulo << ", denominator: " << denominator_;

      // Only rescale source_pos_modulo if denominator changes. Even then, don't change denominator
      // and source_pos_modulo if new rate is zero (even if they requested a different denominator).
      // That way we largely retain our running sub-frame fraction, across rate_mod/denom changes.
      if (new_denom != denominator_ && new_rate_mod) {
        // Ensure that new_source_pos_mod/new_denom == source_pos_modulo/denominator_, which means
        //   new_source_pos_mod = source_pos_modulo * new_denom / denominator_
        // For higher precision, round the result by adding "1/2".
        //   new_source_pos_mod = floor((source_pos_modulo * new_denom / denominator_) + 1/2)
        // Avoid float math and floor, and let int-division do the truncation for us:
        //   new_source_pos_mod = (source_pos_modulo * new_denom + denominator_/2) / denominator_.
        //
        // The max source_pos_modulo is UINT64_MAX-1. New and old denominators should never be
        // equal; but even if both are UINT64_MAX, the maximum (old_source_pos_mod * new_denom)
        // product is < (UINT128_MAX - UINT64_MAX). Even after adding UINT64_MAX/2 (for rounding),
        // new_source_pos_mod cannot overflow its uint128.
        //
        // source_pos_modulo is strictly < denominator_, so the scaled source_pos_mod < new_denom.
        // Our conceptual "+1/2" for rounding could only make new_source_pos_mod EQUAL to new_denom,
        // never exceed it. So our new source_pos_modulo cannot overflow its uint64.
        __uint128_t new_source_pos_mod = static_cast<__uint128_t>(source_pos_modulo) * new_denom;
        new_source_pos_mod += static_cast<__uint128_t>(denominator_ / 2);
        new_source_pos_mod /= static_cast<__uint128_t>(denominator_);

        if (static_cast<uint64_t>(new_source_pos_mod) == new_denom) {
          new_source_pos_mod = 0;
          source_pos += Fixed::FromRaw(1);
        }

        source_pos_modulo = static_cast<uint64_t>(new_source_pos_mod);
        denominator_ = new_denom;
      }
      rate_modulo_ = new_rate_mod;

      return source_pos;
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
  // When Mix has finished, frames before source_offset are no longer needed and can
  // be discarded.
  //
  // @param accumulate
  // When true, Mix will accumulate into the destination buffer (sum the mix
  // results with existing values in the dest buffer). When false, Mix will
  // overwrite any existing destination buffer values with its mix output.
  //
  // Within Mix(), the following source/dest/rate constraints are enforced:
  //  * source_frames           must be at least 1
  //  * source_offset           must be at least -pos_filter_width
  //                            cannot exceed source_frames
  //
  //  * dest_offset             cannot exceed dest_frames
  //
  //  * step_size               must exceed zero
  //  * rate_modulo             must be either zero or less than denominator
  //  * source_position_modulo  must be either zero or less than denominator
  //
  virtual void Mix(float* dest_ptr, int64_t dest_frames, int64_t* dest_offset_ptr,
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
  // For example, a pure "sample and hold" resampler might have a negative filter width of almost
  // one frame and a positive filter width of zero.
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

  // Eagerly precompute any needed data. If not called, that data should be lazily computed on the
  // first call to Mix().
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

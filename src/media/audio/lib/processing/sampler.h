// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_PROCESSING_SAMPLER_H_
#define SRC_MEDIA_AUDIO_LIB_PROCESSING_SAMPLER_H_

#include <lib/syslog/cpp/macros.h>

#include <cstdint>
#include <optional>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/lib/processing/gain.h"

namespace media_audio {

// Enable to emit trace events containing the position state.
inline constexpr bool kTracePositionEvents = false;

// Interface that takes an array of source samples in any format, and processes corresponding array
// of destination samples in normalized 32-bit float format with a specified gain scale applied.
//
// The source and destination samples can be in different frame rates, channel configurations or
// sample formats. The samples that are processed from the source format will be converted into the
// destination format accordingly during `Process` call with respect to the derived implementation.
//
// Each `Process` call assumes a contiguous stream of source and destination samples. The caller
// must ensure that the requested source and destination samples are aligned with respect to their
// audio format and timeline.
//
// Each sampler define their positive and negative lengths of the filter that are expressed in
// fixed-point fractional source subframe units. These lengths convey which source frames will be
// referenced by the filter, when producing corresponding destination frames for a specific instant
// in time.
//
// Positive filter length refers to how far forward (positively) the filter looks, from the PTS in
// question; while negative filter length refers to how far backward (negatively) the filter looks,
// from that same PTS. The center frame position is included in the length. For example, a pure
// "sample and hold" sampler might have a positive filter length of `Fixed::FromRaw(1)` and a
// negative filter length of `kOneFrame`:
//
//       center
//         VV
//   ***************
//     ^   ^^
//     +---++
//       n  p
//
// This class is not safe for concurrent use.
class Sampler {
 public:
  // Class that wraps all the state that is needed by the `Process` function.
  //
  // The primary state is the "stride", which describes how many fractional source frames we should
  // advance for each destination frame. Specifically, each destination frame is equivalent to
  // `step_size + rate_modulo / denominator` source frames, where `rate_modulo / denominator` is a
  // fractional subframe.
  //
  // If `rate_modulo == 0`, the source stride divides evenly into a destination frame and
  // `rate_modulo / denominator` can be ignored.
  class State {
   public:
    // Returns fractional step size for the source, i.e., "stride" for how much to increment the
    // sampling position in the source stream, for each destination frame produced.
    Fixed step_size() const { return step_size_; }
    void set_step_size(Fixed step_size) { step_size_ = step_size; }

    // Expresses (along with `denominator`) leftover rate precision that `step_size` cannot express,
    // which is a fractional value of the `step_size` unit that source position should advance, for
    // each destination frame.
    uint64_t rate_modulo() const { return rate_modulo_; }

    // Expresses (along with `rate_modulo` and `source_pos_modulo`) leftover rate and position
    // precision that `step_size` and `Source::frame_offset_ptr` (respectively) cannot express.
    uint64_t denominator() const { return denominator_; }

    // Expresses (along with `denominator`) leftover position precision that `Source` and `Dest`
    // parameters cannot express. When present, `source_pos_modulo` and `denominator` express a
    // fractional value of the `Source::frame_offset_ptr` unit, for additional precision on current
    // position.
    uint64_t source_pos_modulo() const { return source_pos_modulo_; }
    void set_source_pos_modulo(uint64_t source_pos_modulo) {
      source_pos_modulo_ = source_pos_modulo;
    }

    // Updates `rate_modulo`, `denominator` and `source_pos_modulo`.
    Fixed SetRateModuloAndDenominator(uint64_t rate_modulo, uint64_t denominator,
                                      Fixed source_pos_modulo = Fixed(0));

    // Returns corresponding destination length in frames for a given `source_length` in frames.
    int64_t DestFromSourceLength(Fixed source_length) const;

    // Returns corresponding source length in frames for a given `dest_length` in frames.
    Fixed SourceFromDestLength(int64_t dest_length) const;

   private:
    uint64_t rate_modulo_ = 0;
    uint64_t denominator_ = 1;
    Fixed step_size_ = kOneFrame;
    uint64_t source_pos_modulo_ = 0;
  };

  // Wraps source data.
  struct Source {
    // Pointer to the array of interleaved source samples in any sample format.
    const void* samples;
    // Pointer to the fractional offset from the start of source `samples` in frames, at which the
    // first source frame should be processed. This offset will be updated once `Process` is
    // finished in order to indicate the next frame offset to be processed in a future call.
    Fixed* frame_offset_ptr;
    // Number of source frames to be processed.
    int64_t frame_count;
  };

  // Wraps destination data.
  struct Dest {
    // Pointer to the array of interleaved destination samples in normalized 32-bit float format.
    float* samples;
    // Pointer to the integral offset from the start of destination `samples` in frames, at which
    // the first destination frame should be processed. This offset will be updated once `Process`
    // is finished in order to indicate the next frame offset to be processed in a future call.
    int64_t* frame_offset_ptr;
    // Number of destination frames to be processed.
    int64_t frame_count;
  };

  // Gain to be applied to the processed destination data.
  struct Gain {
    // Gain type.
    GainType type = GainType::kUnity;

    union {
      // Constant gain scale. This will be valid iff the gain `type != GainType::kRamping`.
      float scale = kUnityGainScale;
      // Pointer to the array of gain scale ramp, where each value represents the gain scale for
      // each destination frame. The length of this ramp must match the destination frame count.
      // This will be valid iff the gain `type == GainType::kRamping`.
      const float* scale_ramp;
    };
  };

  // Sampler type.
  enum class Type {
    kDefault = 0,
    kPointSampler = 1,
    kSincSampler = 2,
  };

  // Creates an appropriate `Sampler` for a given `source_format` and `dest_format`. If a sampler
  // `type` is specified explicitly (i.e. `type != Type::kDefault`), this will either return a
  // `Sampler` of that requested `type`, or `nullptr` if a `Sampler` with that `type` cannot be
  // created with the given configuration.
  static std::unique_ptr<Sampler> Create(const Format& source_format, const Format& dest_format,
                                         Type type = Type::kDefault);

  // Default destructor.
  virtual ~Sampler() = default;

  // Eagerly precomputes any needed data. If not called, that data will be lazily computed on the
  // first call to `Process`.
  // TODO(fxbug.dev/45074): This is for tests only and can be removed once filter creation is eager.
  virtual void EagerlyPrepare() = 0;

  // Processes `source` into `dest` with `gain`.
  virtual void Process(Source source, Dest dest, Gain gain, bool accumulate) = 0;

  // Returns sampler type.
  virtual Type type() const = 0;

  // Returns positive filter length in frames.
  Fixed pos_filter_length() const { return pos_filter_length_; }

  // Returns negative filter length in frames.
  Fixed neg_filter_length() const { return neg_filter_length_; }

  // Returns state.
  const State& state() const { return state_; }
  State& state() { return state_; }

 protected:
  Sampler(Fixed pos_filter_length, Fixed neg_filter_length)
      : pos_filter_length_(pos_filter_length), neg_filter_length_(neg_filter_length) {}

  // Ceils `frac_position` in frames.
  static constexpr int64_t Ceiling(int64_t frac_position) {
    return ((frac_position - 1) >> Fixed::Format::FractionalBits) + 1;
  }

  // Floors `frac_position` in frames.
  static constexpr int64_t Floor(int64_t frac_position) {
    return frac_position >> Fixed::Format::FractionalBits;
  }

 private:
  const Fixed pos_filter_length_;
  const Fixed neg_filter_length_;

  State state_;
};

// Mixes `source_sample` to `dest_sample_ptr` with a gain `scale` of `Type`.
template <GainType Type, bool Accumulate>
inline void MixSample(float source_sample, float* dest_sample_ptr, float scale) {
  if constexpr (Accumulate) {
    if constexpr (Type != GainType::kSilent) {
      *dest_sample_ptr += ApplyGain<Type>(source_sample, scale);
    }
  } else {
    *dest_sample_ptr = ApplyGain<Type>(source_sample, scale);
  }
}

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_LIB_PROCESSING_SAMPLER_H_

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_PROCESSING_SAMPLER_H_
#define SRC_MEDIA_AUDIO_LIB_PROCESSING_SAMPLER_H_

#include <cstdint>
#include <optional>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/processing/gain.h"

namespace media_audio {

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
    GainType type;

    union {
      // Constant gain scale. This will be valid iff the gain `type != GainType::kRamping`.
      float scale;
      // Pointer to the array of gain scale ramp, where each value represents the gain scale for
      // each destination frame. The length of this ramp must match the destination frame count.
      // This will be valid iff the gain `type == GainType::kRamping`.
      const float* scale_ramp;
    };
  };

  // Default destructor.
  virtual ~Sampler() = default;

  // Processes `source` into `dest` with `gain`.
  virtual void Process(Source source, Dest dest, Gain gain, bool accumulate) = 0;

  // Returns positive filter length in frames.
  Fixed pos_filter_length() const { return pos_filter_length_; }

  // Returns negative filter length in frames.
  Fixed neg_filter_length() const { return neg_filter_length_; }

 protected:
  Sampler(Fixed pos_filter_length, Fixed neg_filter_length)
      : pos_filter_length_(pos_filter_length), neg_filter_length_(neg_filter_length) {}

 private:
  const Fixed pos_filter_length_;
  const Fixed neg_filter_length_;
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

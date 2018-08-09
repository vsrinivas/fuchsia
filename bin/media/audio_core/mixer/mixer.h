// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_CORE_MIXER_MIXER_H_
#define GARNET_BIN_MEDIA_AUDIO_CORE_MIXER_MIXER_H_

#include <memory>

#include <fuchsia/media/cpp/fidl.h>

#include "garnet/bin/media/audio_core/constants.h"
#include "garnet/bin/media/audio_core/gain.h"

namespace media {
namespace audio {

class Mixer;
using MixerPtr = std::unique_ptr<Mixer>;

class Mixer {
 public:
  static constexpr uint32_t FRAC_ONE = 1u << kPtsFractionalBits;
  static constexpr uint32_t FRAC_MASK = FRAC_ONE - 1u;
  virtual ~Mixer();

  //
  // Resampler enum
  //
  // This enum lists Fuchsia's available resamplers. Callers of Mixer::Select
  // optionally use this enum to specify which resampler they require. Default
  // allows an existing algorithm to select a resampler based on the ratio of
  // incoming and outgoing sample rates.
  enum class Resampler {
    Default = 0,
    SampleAndHold,
    LinearInterpolation,
  };

  //
  // Select
  //
  // Select an appropriate mixer instance, based on an optionally-specified
  // resampler type, or else by the properties of source/destination formats.
  //
  // When calling Mixer::Select, resampler_type is optional. If caller specifies
  // a particular resampler, Mixer::Select will either instantiate exactly what
  // was requested, or return nullptr -- even if otherwise it could successfully
  // instantiate a different one. Setting this param to non-Default says "I know
  // exactly what I need: I want you to fail rather than give me anything else."
  //
  // If resampler_type is absent or indicates Default, the resampler type is
  // determined by algorithm (as has been the case before this CL).
  // For optimum system performance across changing conditions, callers should
  // take care when directly specifying a resampler type, if they do so at all.
  // The default should be allowed whenever possible.
  static MixerPtr Select(const fuchsia::media::AudioStreamType& src_format,
                         const fuchsia::media::AudioStreamType& dst_format,
                         Resampler resampler_type = Resampler::Default);

  //
  // Mix
  //
  // Perform a mixing operation from the source buffer into the destination
  // buffer.
  //
  // @param dst
  // The pointer to the destination buffer into which frames will be mixed.
  //
  // @param dst_frames
  // The total number of frames of audio which comprise the destination buffer.
  //
  // @param dst_offset
  // The pointer to the offset (in destination frames) at which we should start
  // to mix destination frames.  When Mix has finished, dst_offset will be
  // updated to indicate the offset into the destination buffer of the next
  // frame to be mixed.
  //
  // @param src
  // The pointer the the source buffer containing the frames to be mixed into
  // the destination buffer.
  //
  // @param frac_src_frames
  // The total number of fractional AudioOut frames contained by the source
  // buffer.
  //
  // @param frac_src_offset
  // A pointer to the offset (expressed in fractional AudioOut frames) at which
  // the first frame to be mixed with the destination buffer should be sampled.
  // When Mix has finished, frac_src_offset will be updated to indicate the
  // offset of the sampling position of the next frame to be mixed with the
  // output buffer.
  //
  // @param frac_step_size
  // How much to increment the fractional sampling position for each output
  // frame produced.
  //
  // @param amplitude_scale
  // The amplitude scaling factor to be applied when mixing.  This is expressed
  // as a 32-bit single-precision floating-point value.
  //
  // @param accumulate
  // When true, the mixer will accumulate into the destination buffer (read,
  // sum, clip, write-back).  When false, the mixer will simply replace the
  // destination buffer with its output.
  //
  // @param modulo
  // If frac_step_size cannot perfectly express the mix's resampling ratio,
  // this parameter (along with subsequent denominator) expresses any leftover
  // precision.  When present, modulo and denominator express a fractional value
  // of frac_step_size unit that should be advanced, for each destination frame.
  //
  // @param denominator
  // If frac_step_size cannot perfectly express the mix's resampling ratio, this
  // parameter (along with precedent modulo) expresses any leftover precision.
  // When present, modulo and denominator express a fractional value of
  // frac_step_size unit that should be advanced, for each destination frame.
  //
  // @return True if the mixer is finished with this source data and will not
  // need it in the future.  False if the mixer has not consumed the entire
  // source buffer and will need more of it in the future.
  //
  // TODO(mpuryear): Change frac_src_frames parameter to be (integer)
  // src_frames, as number of src_frames was never intended to be fractional.
  virtual bool Mix(float* dst, uint32_t dst_frames, uint32_t* dst_offset,
                   const void* src, uint32_t frac_src_frames,
                   int32_t* frac_src_offset, uint32_t frac_step_size,
                   Gain::AScale amplitude_scale, bool accumulate,
                   uint32_t modulo = 0, uint32_t denominator = 1) = 0;
  // When calling Mix(), we communicate the resampling rate with three
  // parameters. We augment frac_step_size with modulo and denominator
  // arguments that capture the remaining rate component that cannot be
  // expressed by a 19.13 fixed-point step_size. Note: frac_step_size and
  // frac_input_offset use the same format -- they have the same limitations
  // in what they can and cannot communicate. This begs two questions:
  //
  // Q1: For perfect position accuracy, don't we also need an in/out param
  // to specify initial/final subframe modulo, for fractional source offset?
  // A1: Yes, for optimum position accuracy (within quantization limits), we
  // SHOULD incorporate running subframe position_modulo in this way.
  //
  // For now, we are defering this work, tracking it with MTWN-128.
  //
  // Q2: Why did we solve this issue for rate but not for initial position?
  // A2: We solved this issue for *rate* because its effect accumulates over
  // time, causing clearly measurable distortion that becomes crippling with
  // larger jobs. For *position*, there is no accumulated magnification over
  // time -- in analyzing the distortion that this should cause, mix job
  // size would affect the distortion frequency but not amplitude. We expect
  // the effects to be below audible thresholds. Until the effects are
  // measurable and attributable to this jitter, we will defer this work.

  //
  // Reset
  //
  // Reset the internal state of the mixer.  Will be called every time there is
  // a discontinuity in the source stream.  Mixer implementations should reset
  // anything related to their internal filter state.
  virtual void Reset() {}

  //
  // Filter widths
  //
  // The positive and negative widths of the filter for this mixer, expressed in
  // fractional input AudioOut units.  These widths convey which input frames
  // will be referenced by the filter, when producing output for a specific
  // instant in time. Positive filter width refers to how far forward
  // (positively) the filter looks, from the PTS in question; negative filter
  // width refers to how far backward (negatively) the filter looks, from that
  // same PTS.  Specifically...
  //
  // Let:
  // P = pos_filter_width()
  // N = neg_filter_width()
  // S = An arbitrary point in time at which the input stream will be sampled.
  // X = The PTS of an input frame.
  //
  // If (X >= (S - N)) && (X <= (S + P))
  // Then input frame X is within the filter and contributes to mix operation.
  //
  // Conversely, input frame X contributes to the output samples S where
  //  (S >= X - P)  and  (S <= X + N)
  //
  inline uint32_t pos_filter_width() const { return pos_filter_width_; }
  inline uint32_t neg_filter_width() const { return neg_filter_width_; }

 protected:
  Mixer(uint32_t pos_filter_width, uint32_t neg_filter_width);

 private:
  uint32_t pos_filter_width_;
  uint32_t neg_filter_width_;
};

}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_CORE_MIXER_MIXER_H_

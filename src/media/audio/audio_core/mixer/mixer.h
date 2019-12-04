// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_MIXER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_MIXER_H_

#include <fuchsia/media/cpp/fidl.h>

#include <memory>

#include "lib/media/cpp/timeline_function.h"
#include "src/media/audio/audio_core/mixer/constants.h"
#include "src/media/audio/audio_core/mixer/frames.h"
#include "src/media/audio/audio_core/mixer/gain.h"

namespace media::audio {

class Mixer {
 public:
  static constexpr uint32_t FRAC_ONE = 1u << kPtsFractionalBits;
  static constexpr uint32_t FRAC_MASK = FRAC_ONE - 1u;

  // Bookkeeping
  //
  // This struct represents the state of that mix operation from the source point-of-view. In a Mix,
  // the relationship between sources and destinations is many-to-one, so this struct largely
  // includes details about its source stream, and how it relates to the destination.
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

    // This 19.13 fixed-point value represents how much to increment our sampling position in the
    // input (src) stream, for each output (dest) frame produced.
    uint32_t step_size = Mixer::FRAC_ONE;

    // If step_size cannot perfectly express the mix's resampling ratio, this parameter (along with
    // subsequent denominator) expresses leftover precision. When non-zero, rate_modulo and
    // denominator express a fractional value of step_size unit that src position should advance,
    // for each dest frame.
    uint32_t rate_modulo = 0;

    // If step_size cannot perfectly express the mix's resampling ratio, this parameter (along with
    // precedent rate_modulo) expresses leftover precision. When non-zero, rate_modulo and
    // denominator express a fractional value of step_size unit that src position should advance,
    // for each dest frame.
    uint32_t denominator = 0;

    // This function returns a snapshot of the denominator as determined by the
    // 'dest_frames_to_frac_source_frames' timeline transform.
    uint32_t SnapshotDenominatorFromDestTrans() const {
      return dest_frames_to_frac_source_frames.rate().reference_delta();
    }

    // If src_offset cannot perfectly express the source's position, this parameter (along with
    // denominator) expresses any leftover precision. When present, src_pos_modulo and denominator
    // express a fractional value of src_offset unit that should be used when advancing src
    // position.
    uint32_t src_pos_modulo = 0;

    // This translates a destination frame value into a source subframe value. The output values of
    // this function is in 19.13 input subframes.
    TimelineFunction dest_frames_to_frac_source_frames;

    // dest_frames_to_frac_source_frames may change over time; this value represents the current
    // generation (which version), so any change can be detected.
    uint32_t dest_trans_gen_id = kInvalidGenerationId;

    // This translates a CLOCK_MONOTONIC value into a source subframe value. The output values of
    // this function is in 19.13 input subframes.
    TimelineFunction clock_mono_to_frac_source_frames;

    // clock_mono_to_frac_source_frames may change over time; this value represents the current
    // generation (which version), so any change can be detected.
    uint32_t source_trans_gen_id = kInvalidGenerationId;

    void Reset() {
      src_pos_modulo = 0;
      gain.ClearSourceRamp();
    }

    static constexpr uint32_t kScaleArrLen = 960;
    std::unique_ptr<Gain::AScale[]> scale_arr = std::make_unique<Gain::AScale[]>(kScaleArrLen);
  };

  virtual ~Mixer() = default;

  //
  // Resampler enum
  //
  // This enum lists Fuchsia's available resamplers. Callers of Mixer::Select
  // optionally use this enum to specify a resampler type. Default allows an
  // algorithm to select a resampler based on the ratio of incoming and outgoing
  // rates, using Linear for all except "Integer-to-One" resampling ratios.
  enum class Resampler {
    Default = 0,
    SampleAndHold,
    LinearInterpolation,
    WindowedSinc,
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
  // A pointer to the offset (in fractional input frames) from start of src
  // buffer, at which the first input frame should be sampled. When Mix has
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
  // fractional (19.13 fixed) input subframe units. These widths convey which
  // input frames will be referenced by the filter, when producing output for a
  // specific instant in time. Positive filter width refers to how far forward
  // (positively) the filter looks, from the PTS in question; negative filter
  // width refers to how far backward (negatively) the filter looks, from that
  // same PTS. Specifically...
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
  inline FractionalFrames<uint32_t> pos_filter_width() const { return pos_filter_width_; }
  inline FractionalFrames<uint32_t> neg_filter_width() const { return neg_filter_width_; }

  Bookkeeping& bookkeeping() { return bookkeeping_; }
  const Bookkeeping& bookkeeping() const { return bookkeeping_; }

 protected:
  Mixer(uint32_t pos_filter_width, uint32_t neg_filter_width);

 private:
  FractionalFrames<uint32_t> pos_filter_width_;
  FractionalFrames<uint32_t> neg_filter_width_;
  Bookkeeping bookkeeping_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_MIXER_H_

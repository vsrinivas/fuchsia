// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_MIXER_MIXER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_MIXER_MIXER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <cmath>
#include <limits>
#include <memory>

#include <ffl/string.h>

#include "src/media/audio/audio_core/shared/mixer/constants.h"
#include "src/media/audio/audio_core/shared/mixer/gain.h"
#include "src/media/audio/lib/format/constants.h"
#include "src/media/audio/lib/format2/channel_mapper.h"
#include "src/media/audio/lib/processing/sampler.h"
#include "src/media/audio/lib/timeline/timeline_function.h"

namespace media::audio {

// The Mixer class provides format-conversion, rechannelization, rate-connversion, and gain/mute
// scaling. Each source in a multi-stream mix has its own Mixer instance. When Mixer::Mix() is
// called, it adds that source's contribution, by reading audio from its source, generating the
// appropriately processed result, and summing this output into a common destination buffer.
class Mixer {
 public:
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
  //  * step_size_modulo        must be either zero or less than denominator
  //  * source_position_modulo  must be either zero or less than denominator
  //
  virtual void Mix(float* dest_ptr, int64_t dest_frames, int64_t* dest_offset_ptr,
                   const void* source_void_ptr, int64_t source_frames, Fixed* source_offset_ptr,
                   bool accumulate) = 0;

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

  media_audio::Sampler::State& state() { return sampler().state(); }
  const media_audio::Sampler::State& state() const { return sampler().state(); }

  // Eagerly precompute any needed data. If not called, that data should be lazily computed on the
  // first call to Mix().
  // TODO(fxbug.dev/45074): This is for tests only and can be removed once filter creation is eager.
  virtual void EagerlyPrepare() {}

  // This object maintains gain values in the mix path, including source gain and a snapshot of
  // destination gain (the definitive value for destination gain is owned elsewhere). Gain accepts
  // level in dB, and provides gainscale as float multiplier.
  Gain gain;

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

  static constexpr int64_t kScaleArrLen = 960;
  std::unique_ptr<Gain::AScale[]> scale_arr = std::make_unique<Gain::AScale[]>(kScaleArrLen);

 protected:
  // Template to read normalized source samples, and combine channels if required.
  template <typename SourceSampleType, size_t SourceChanCount, size_t DestChanCount,
            typename Enable = void>
  class SourceReader {
   public:
    static inline float Read(const SourceSampleType* source_ptr, size_t dest_chan) {
      return mapper_.Map(source_ptr, dest_chan);
    }

   private:
    static inline media_audio::ChannelMapper<SourceSampleType, SourceChanCount, DestChanCount>
        mapper_;
  };

  Mixer(Fixed pos_filter_width, Fixed neg_filter_width,
        std::shared_ptr<media_audio::Sampler> sampler, Gain::Limits gain_limits);

  media_audio::Sampler& sampler() {
    FX_DCHECK(sampler_);
    return *sampler_;
  }
  const media_audio::Sampler& sampler() const {
    FX_DCHECK(sampler_);
    return *sampler_;
  }

 private:
  const Fixed pos_filter_width_;
  const Fixed neg_filter_width_;

  std::shared_ptr<media_audio::Sampler> sampler_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_MIXER_MIXER_H_

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_SILENCE_PADDING_STREAM_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_SILENCE_PADDING_STREAM_H_

#include <memory>

#include "src/media/audio/audio_core/shared/mixer/output_producer.h"
#include "src/media/audio/audio_core/v1/stream.h"

namespace media::audio {

// A stream wrapper that appends silence after each discontiguous chunk of audio. We use
// silence to "ring out" or "fade out" audio processors. This wrapper can be used when
// the following conditions are met:
//
//   1. The audio processor assumes that the source stream is preceded by an infinite amount
//      of silence. That is, we don't need to inject silence into the beginning of the stream;
//      initial silence is assumed.
//
//   2. After the processor is fed `silence_frames` worth of silence, it emits no more audible
//      sound; all further output is below the noise floor, at least until it is fed another
//      non-silent chunk of audio. Put differently, `silence_frames` is the minimum number of
//      frames necessary to "ring out" or "fade out" any effects or filters applied by the audio
//      processor.
//
// For example, when a resampling filter produces destination frame X, it actually samples from
// a wider range of the source stream surrounding the corresponding source frame X. This range is
// defined by a "negative filter width" and a "positive filter width":
//
//    +----------------X----------------+  source stream
//               |     ^     |
//               +-----+-----+
//                  ^     ^
//     negative width     positive width
//
// Such a filter will need to be fed `negative_width+positive_width` worth of silence after each
// non-silent segment. To illustrate:
//
//    A-----------------------B                      C-------------------...
//                            |     ^     |    |     ^     |
//                            +-----+-----+    +-----+-----+
//                               ^     ^
//                 neg_filter_width   pos_filter_width
//
// In this example, the source stream includes a chunk of non-silent data in frames [A,B],
// followed later by another non-silent chunk starting at frame C. SilencePaddingStream's job
// is to generate silence to "ring out" the stream between frames B and C.
//
// To produce the destination frame corresponding to source frame A, the filter assumes A
// is preceded by infinite silence (recall condition 1, above). This covers the range
// [A-neg_filter_width,A]. SilencePaddingStream does nothing in this range.
//
// To produce the destination frame corresponding to source frame B + neg_filter_width,
// the filter needs to be fed neg_filter_width + pos_filter_width worth of silence following
// frame B. This quiesces the filter into a silent state. Beyond this frame, the filter is
// in a silent state and does not need to be fed additional silent frames before frame C.
//
// If B and C are separated a non-integral number of frames, there are two cases:
//
//   * If SilencePaddingStream was created with `fractional_gaps_round_down=true`, then at
//     most floor(C-B) frames are generated immediately after B. For example, if B=10, C=15.5,
//     and silence_frames=20, we generate silence at frames [10,15), leaving a gap in the
//     fractional range [15, 15.5).
//
//   * If SilencePaddingStream was created with `fractional_gaps_round_down=false`, then at
//     most ceil(C-B) frames are generated immediately after B. For example, if B=10, C=15.5,
//     and silence_frames=20, we generate silence at frames [10,16), where the last frame
//     of silence overlaps with C.
//
// The second mode (`fractional_gaps_round_down=false`) is useful for pipeline stages that
// sample a source stream using SampleAndHold. In the above example, SampleAndHold samples
// source frame C=15.5 into dest frame 16. If we generate silence in the range [10,15), this
// leaves a full-frame gap before C, even though we've generated only 5 frames of silence and
// silence_frames=20. Hence, in this case, it's better to generate ceil(C-B) frames of silence.
class SilencePaddingStream : public ReadableStream {
 public:
  static std::shared_ptr<ReadableStream> WrapIfNeeded(std::shared_ptr<ReadableStream> source,
                                                      Fixed silence_frames,
                                                      bool fractional_gaps_round_down);
  static std::shared_ptr<SilencePaddingStream> Create(std::shared_ptr<ReadableStream> source,
                                                      Fixed silence_frames,
                                                      bool fractional_gaps_round_down);

  SilencePaddingStream(std::shared_ptr<ReadableStream> source, Fixed silence_frames,
                       bool fractional_gaps_round_down);

  // Implements `media::audio::ReadableStream`.
  TimelineFunctionSnapshot ref_time_to_frac_presentation_frame() const override {
    return source_->ref_time_to_frac_presentation_frame();
  }
  std::shared_ptr<Clock> reference_clock() override { return source_->reference_clock(); }

  void SetPresentationDelay(zx::duration external_delay) override {
    source_->SetPresentationDelay(external_delay);
  }

 private:
  std::optional<ReadableStream::Buffer> ReadLockImpl(ReadLockContext& ctx, Fixed dest_frame,
                                                     int64_t frame_count) override;
  void TrimImpl(Fixed dest_frame) override;

  const int64_t silence_frames_;
  const bool fractional_gaps_round_down_;
  std::shared_ptr<ReadableStream> source_;
  std::vector<char> silence_;

  struct BufferInfo {
    Fixed end_frame;
    StreamUsageMask usage_mask;
    float total_applied_gain_db;
  };

  // Last non-silent buffer we returned from ReadLockImpl.
  std::optional<BufferInfo> last_buffer_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_SILENCE_PADDING_STREAM_H_

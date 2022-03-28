// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_PIPELINE_STAGE_H_
#define SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_PIPELINE_STAGE_H_

#include <fidl/fuchsia.audio.mixer/cpp/wire.h>
#include <lib/fpromise/result.h>
#include <lib/zx/time.h>

#include <atomic>
#include <optional>
#include <string>
#include <string_view>

#include "src/media/audio/lib/clock/audio_clock.h"
#include "src/media/audio/lib/timeline/timeline_function.h"
#include "src/media/audio/mixer_service/common/basic_types.h"
#include "src/media/audio/mixer_service/mix/packet.h"
#include "src/media/audio/mixer_service/mix/ptr_decls.h"
#include "src/media/audio/mixer_service/mix/thread.h"

namespace media_audio_mixer_service {

// A stage in a pipeline tree.
//
// Each PipelineStage consumes zero or more source streams and produces at most one destination
// stream. This abstract class provides functionality common to all pipeline stages.
class PipelineStage {
 public:
  virtual ~PipelineStage() = default;

  // Adds a source stream.
  // REQUIRED: caller must verify that src produces a stream with a compatible format.
  virtual void AddSource(PipelineStagePtr src) TA_REQ(thread()->checker()) = 0;

  // Removes a source stream.
  // REQUIRED: caller must verify that src is currently a source for this PipelineStage.
  virtual void RemoveSource(PipelineStagePtr src) TA_REQ(thread()->checker()) = 0;

  // Returns a function that translates from a timestamp to the corresponding fixed-point frame
  // number that will be presented at that time. The given timestamp is relative to
  // `reference_clock`.
  virtual media::TimelineFunction ref_time_to_frac_presentation_frame() const = 0;

  // Returns the PipelineStage's reference clock.
  virtual media::audio::AudioClock& reference_clock() = 0;

  // Returns the corresponding frame for a given `ref_time`.
  Fixed FracPresentationFrameAtRefTime(zx::time ref_time) const {
    return Fixed::FromRaw(ref_time_to_frac_presentation_frame().Apply(ref_time.get()));
  }

  // Returns the corresponding reference time for a given `frame`.
  zx::time RefTimeAtFracPresentationFrame(Fixed frame) const {
    return zx::time(ref_time_to_frac_presentation_frame().ApplyInverse(frame.raw_value()));
  }

  // Returns the PipelineStage's name. This is used for diagnostics only.
  // The name may not be a unique identifier.
  std::string_view name() const { return name_; }

  // Returns the PipelineStage's format.
  const Format& format() const { return format_; }

  // Returns the thread which currently controls this PipelineStage.
  // It is safe to call this method on any thread, but if not called from thread(),
  // the returned value may change concurrently.
  ThreadPtr thread() const { return std::atomic_load(&thread_); }

  // TODO(fxbug.dev/87651): bring in stuff from the old ReadableStream:
  // - delay aka lead time
  // - reading and trimming the destination stream, perhaps as:
  //
  //   class Buffer {};
  //   std::optional<Buffer> ReadDestStream(MixJobContext& ctx, Fixed frame_start,
  //                                        int64_t frame_count);
  //   void TrimDestStream(Fixed frame);

 protected:
  PipelineStage(std::string_view name, Format format) : name_(name), format_(format) {}

  PipelineStage(const PipelineStage&) = delete;
  PipelineStage& operator=(const PipelineStage&) = delete;

  PipelineStage(PipelineStage&&) = delete;
  PipelineStage& operator=(PipelineStage&&) = delete;

 private:
  const std::string name_;
  const Format format_;

  // This is accessed with atomic instructions (std::atomic_load and std::atomic_store) so that any
  // thread can call thread()->checker(). This can't be a std::atomic<ThreadPtr> until C++20.
  ThreadPtr thread_;
};

}  // namespace media_audio_mixer_service

#endif  // SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_PIPELINE_STAGE_H_

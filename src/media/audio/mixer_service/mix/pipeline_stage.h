// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_PIPELINE_STAGE_H_
#define SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_PIPELINE_STAGE_H_

#include <fidl/fuchsia.audio.mixer/cpp/wire.h>
#include <lib/fpromise/result.h>

#include <atomic>
#include <optional>
#include <string>
#include <string_view>

#include "src/media/audio/lib/format2/format.h"
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

  // Returns the PipelineStage's name. This is used for diagnostics only.
  // The name may not be a unique identifier.
  std::string_view name() const { return name_; }

  // Returns the PipelineStage's format.
  const media_audio::Format& format() const { return format_; }

  // Returns the thread which currently controls this PipelineStage.
  // It is safe to call this method on any thread, but if not called from thread(),
  // the returned value may change concurrently.
  ThreadPtr thread() const { return std::atomic_load(&thread_); }

  // Adds a source stream.
  // REQUIRED: caller must verify that src produces a stream with a compatible format.
  virtual void AddSource(PipelineStagePtr src) TA_REQ(thread()->checker()) = 0;

  // Removes a source stream.
  // REQUIRED: caller must verify that src is currently a source for this PipelineStage.
  virtual void RemoveSource(PipelineStagePtr src) TA_REQ(thread()->checker()) = 0;

  // TODO(fxbug.dev/87651): bring in stuff from the old ReadableStream:
  // - timeline transform
  // - clocks
  // - delay aka lead time
  // - reading and trimming the destination stream, perhaps as:
  //
  //   class Buffer {};
  //   std::optional<Buffer> ReadDestStream(MixJobContext& ctx, Fixed frame_start,
  //                                        int64_t frame_count);
  //   void TrimDestStream(Fixed frame);

 protected:
  PipelineStage(std::string_view name, media_audio::Format format) : name_(name), format_(format) {}

  PipelineStage(const PipelineStage&) = delete;
  PipelineStage& operator=(const PipelineStage&) = delete;

  PipelineStage(PipelineStage&&) = delete;
  PipelineStage& operator=(PipelineStage&&) = delete;

 private:
  const std::string name_;
  const media_audio::Format format_;

  // This is accessed with atomic instructions (std::atomic_load and std::atomic_store) so that any
  // thread can call thread()->checker(). This can't be a std::atomic<ThreadPtr> until C++20.
  ThreadPtr thread_;
};

}  // namespace media_audio_mixer_service

#endif  // SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_PIPELINE_STAGE_H_

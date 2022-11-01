// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_GRAPH_DETACHED_THREAD_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_GRAPH_DETACHED_THREAD_H_

#include <fidl/fuchsia.audio.mixer/cpp/wire.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/profile.h>

#include <memory>
#include <string>

#include "src/media/audio/services/mixer/fidl/graph_thread.h"
#include "src/media/audio/services/mixer/mix/pipeline_detached_thread.h"

namespace media_audio {

// Wraps a PipelineDetachedThread.
class GraphDetachedThread : public GraphThread {
 public:
  explicit GraphDetachedThread(std::shared_ptr<GlobalTaskQueue> task_queue)
      : GraphThread(std::move(task_queue)) {}

  // The value returned by `id()`.
  static inline constexpr ThreadId kId = PipelineDetachedThread::kId;

  // Implements `GraphThread`.
  std::shared_ptr<PipelineThread> pipeline_thread() const final { return pipeline_thread_; }
  void IncrementClockUsage(std::shared_ptr<Clock> clock) final {}
  void DecrementClockUsage(std::shared_ptr<Clock> clock) final {}

 private:
  const std::shared_ptr<PipelineDetachedThread> pipeline_thread_ =
      std::make_shared<PipelineDetachedThread>();
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_GRAPH_DETACHED_THREAD_H_

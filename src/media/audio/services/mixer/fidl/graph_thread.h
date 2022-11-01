// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_GRAPH_THREAD_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_GRAPH_THREAD_H_

#include <memory>
#include <string>

#include "src/media/audio/lib/clock/clock.h"
#include "src/media/audio/services/common/thread_checker.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/common/global_task_queue.h"
#include "src/media/audio/services/mixer/mix/pipeline_thread.h"
#include "src/media/audio/services/mixer/mix/ptr_decls.h"

namespace media_audio {

// An abstract base class which wraps a PipelineThread.
class GraphThread {
 public:
  // Returns the thread's ID.
  // This is guaranteed to be a unique identifier.
  // Safe to call from any thread.
  ThreadId id() const { return pipeline_thread()->id(); }

  // Returns the thread's name. This is used for diagnostics only.
  // The name may not be a unique identifier.
  // Safe to call from any thread.
  std::string_view name() const { return pipeline_thread()->name(); }

  // Returns the underlying PipelineThread.
  virtual std::shared_ptr<PipelineThread> pipeline_thread() const = 0;

  // Runs an asynchronous task on this thread.
  void PushTask(std::function<void()> fn) const { global_task_queue_->Push(id(), std::move(fn)); }

  // Increments number of `clock` usages in this thread.
  virtual void IncrementClockUsage(std::shared_ptr<Clock> clock) = 0;

  // Decrements number of `clock` usages in this thread.
  virtual void DecrementClockUsage(std::shared_ptr<Clock> clock) = 0;

 protected:
  explicit GraphThread(std::shared_ptr<GlobalTaskQueue> global_task_queue)
      : global_task_queue_(std::move(global_task_queue)) {}

  virtual ~GraphThread() = default;

  GraphThread(const GraphThread&) = delete;
  GraphThread& operator=(const GraphThread&) = delete;

  GraphThread(GraphThread&&) = delete;
  GraphThread& operator=(GraphThread&&) = delete;

 private:
  const std::shared_ptr<GlobalTaskQueue> global_task_queue_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_GRAPH_THREAD_H_

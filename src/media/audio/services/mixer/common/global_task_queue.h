// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_COMMON_GLOBAL_WORK_QUEUE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_COMMON_GLOBAL_WORK_QUEUE_H_

#include <lib/fit/function.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <deque>
#include <mutex>
#include <unordered_map>

#include "src/media/audio/lib/clock/timer.h"
#include "src/media/audio/services/mixer/common/basic_types.h"

namespace media_audio {

// A queue of pending tasks.
// The queue has two important properties:
//
//   1. Tasks must execute in the order they are pushed.
//   2. Tasks must execute on specific threads.
//
// This class is thread safe.
class GlobalTaskQueue {
 public:
  GlobalTaskQueue() = default;
  GlobalTaskQueue(const GlobalTaskQueue&) = delete;
  GlobalTaskQueue& operator=(const GlobalTaskQueue&) = delete;
  GlobalTaskQueue(GlobalTaskQueue&&) = delete;
  GlobalTaskQueue& operator=(GlobalTaskQueue&&) = delete;

  // Pushes a task onto the end of the queue.
  // The task must execute on thread `id`, unless `id = kAnyThreadId`,
  // in which case the task may execute on any thread.
  void Push(ThreadId id, fit::closure fn);

  // Runs all tasks that can be processed by the given thread.
  // If `id != kAnyThreadId`, then this must be called from the correct thread.
  // If `id == kAnyThreadId`, then this may be called from any thread.
  void RunForThread(ThreadId id);

  // Registers a Timer to be notified via SetEventBit when thread `id` is ready to run.
  // There can be at most one Timer registered per `id`.
  void RegisterTimer(ThreadId id, std::shared_ptr<Timer> timer);

  // Discards a previously-registered Timer.
  void UnregisterTimer(ThreadId id);

 private:
  struct Task {
    Task(ThreadId _id, fit::closure _fn) : id(_id), fn(std::move(_fn)) {}
    ThreadId id;
    fit::closure fn;
    bool running TA_GUARDED(&GlobalTaskQueue::mutex_) = false;
  };

  std::shared_ptr<Timer> NextThreadToRun() const TA_REQ(mutex_);

  std::mutex mutex_;
  std::deque<Task> queue_ TA_GUARDED(mutex_);
  std::unordered_map<ThreadId, std::shared_ptr<Timer>> timers_ TA_GUARDED(mutex_);
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_COMMON_GLOBAL_WORK_QUEUE_H_

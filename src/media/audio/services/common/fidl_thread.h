// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_REALTIME_REALTIME_FIDL_THREAD_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_REALTIME_REALTIME_FIDL_THREAD_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>

#include <memory>

#include "src/media/audio/services/common/thread_checker.h"

namespace media_audio {

// Encapsulates a thread which services FIDL requests on an `async::Loop`.
//
// All methods are safe to call from any thread.
class FidlThread {
 public:
  // Creates a FidlThread from a new thread. The loop is started automatically.
  static std::shared_ptr<FidlThread> CreateFromNewThread(std::string name);

  // Creates a FidlThread from the current thread. The loop is not started automatically.
  static std::shared_ptr<FidlThread> CreateFromCurrentThread(std::string name);

  // Reports the name of this thread.
  std::string_view name() const { return name_; }

  // Returns the loop and dispatcher which back this thread.
  async::Loop& loop() { return *loop_; }
  const async::Loop& loop() const { return *loop_; }
  async_dispatcher_t* dispatcher() const { return loop_->dispatcher(); }

  // Returns a checker which validates that code is running on this thread.
  const ThreadChecker& checker() const { return checker_; }

  // Posts a task to this thread.
  zx_status_t PostTask(fit::closure task) const {
    return async::PostTask(dispatcher(), std::move(task));
  }

 private:
  static std::shared_ptr<FidlThread> Create(std::string name, std::thread::id thread_id,
                                            std::unique_ptr<async::Loop> loop);

  explicit FidlThread(std::string name, std::thread::id thread_id,
                      std::unique_ptr<async::Loop> loop)
      : name_(name), checker_(thread_id), loop_(std::move(loop)) {}

  FidlThread(const FidlThread&) = delete;
  FidlThread& operator=(const FidlThread&) = delete;
  FidlThread(FidlThread&&) = delete;
  FidlThread& operator=(FidlThread&&) = delete;

  const std::string name_;
  const ThreadChecker checker_;
  std::unique_ptr<async::Loop> loop_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_REALTIME_REALTIME_FIDL_THREAD_H_

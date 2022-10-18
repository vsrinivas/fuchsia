// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_COMMON_FIDL_THREAD_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_COMMON_FIDL_THREAD_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>

#include <memory>

#include "src/media/audio/services/common/thread_checker.h"

namespace media_audio {

// Encapsulates a thread which services FIDL requests on an `async_dispatcher_t`.
//
// All methods are safe to call from any thread.
class FidlThread {
 public:
  // Creates a FidlThread from a new thread. This creates a new async::Loop and starts a thread on
  // that loop.
  static std::shared_ptr<FidlThread> CreateFromNewThread(std::string name);

  // Creates a FidlThread from the current thread using the given dispatcher.
  static std::shared_ptr<FidlThread> CreateFromCurrentThread(std::string name,
                                                             async_dispatcher_t* dispatcher);

  // Reports the name of this thread.
  std::string_view name() const { return name_; }

  // Returns the dispatcher which back this thread.
  async_dispatcher_t* dispatcher() const { return dispatcher_; }

  // Returns a checker which validates that code is running on this thread.
  const ThreadChecker& checker() const { return checker_; }

  // Posts a task to this thread.
  zx_status_t PostTask(fit::closure task) const {
    return async::PostTask(dispatcher(), std::move(task));
  }

 private:
  static std::shared_ptr<FidlThread> Create(std::string name, std::thread::id thread_id,
                                            async_dispatcher_t* dispatcher,
                                            std::unique_ptr<async::Loop> loop);

  explicit FidlThread(std::string name, std::thread::id thread_id, async_dispatcher_t* dispatcher,
                      std::unique_ptr<async::Loop> loop)
      : name_(std::move(name)),
        checker_(thread_id),
        dispatcher_(dispatcher),
        loop_(std::move(loop)) {}

  FidlThread(const FidlThread&) = delete;
  FidlThread& operator=(const FidlThread&) = delete;
  FidlThread(FidlThread&&) = delete;
  FidlThread& operator=(FidlThread&&) = delete;

  const std::string name_;
  const ThreadChecker checker_;
  async_dispatcher_t* const dispatcher_;
  const std::unique_ptr<async::Loop> loop_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_COMMON_FIDL_THREAD_H_

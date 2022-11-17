// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V2_TASK_BARRIER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V2_TASK_BARRIER_H_

#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>

namespace media_audio {

// A barrier for async tasks. Call `AddPending` to add a pending task, then `CompleteSuccess` or
// `CompleteFailed` to complete that task. Once all pending tasks have been completed, the callback
// is invoked, with `true` if at least one task failed, and `false` otherwise.
class TaskBarrier {
 public:
  // When the barrier is created, there are no pending tasks. The callback will not be invoked until
  // at least one task completes.
  explicit TaskBarrier(fit::callback<void(bool failed)> done) : done_(std::move(done)) {}

  // Verify the Add and Complete calls are balanced.
  ~TaskBarrier() { FX_CHECK(pending_ == 0); }

  void AddPending(int64_t n = 1) { pending_ += n; }

  void CompleteSuccess() {
    FX_CHECK(pending_ > 0);
    if (--pending_ == 0) {
      done_(failed_);
    }
  }

  void CompleteFailed() {
    FX_CHECK(pending_ > 0);
    failed_ = true;
    if (--pending_ == 0) {
      done_(failed_);
    }
  }

 private:
  int64_t pending_ = 0;
  bool failed_ = false;
  fit::callback<void(bool)> done_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V2_TASK_BARRIER_H_

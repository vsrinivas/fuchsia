// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <mutex>

#include "lib/fxl/functional/closure.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/tasks/task_runner.h"

namespace media {

// ThreadsafeCallbackJoiner is used to take action after multiple 'child'
// operations are completed. Unlike CallbackJoiner, ThreadsafeCallbackJoiner is
// threadsafe and can be used with multiple threads.
//
// See callback_joiner.h for details about how this class is used.
//
class ThreadsafeCallbackJoiner
    : public std::enable_shared_from_this<ThreadsafeCallbackJoiner> {
 public:
  // Creates a ThreadsafeCallbackJoiner and returns a shared pointer to it.
  // CallbackJoiners created in this way can safely create callbacks via the
  // NewCallback method.
  static std::shared_ptr<ThreadsafeCallbackJoiner> Create();

  // Constructs a ThreadsafeCallbackJoiner.
  // NOTE: ThreadsafeCallbackJoiner::NewCallback only works for joiners that
  // already have shared pointers to them. The static Create method is
  // recommended for joiners whose NewCallback method will be invoked.
  ThreadsafeCallbackJoiner();

  ~ThreadsafeCallbackJoiner();

  // Indicates the initiation of a child operation. Every call to Spawn should
  // be matched by a subsequent call to Complete.
  void Spawn();

  // Indicates the completion of a child operation.
  void Complete();

  // Calls Spawn and returns a new callback, which calls Complete. THIS METHOD
  // WILL ONLY WORK IF THERE IS ALREADY A SHARED POINTER TO THIS OBJECT.
  fxl::Closure NewCallback();

  // Specifies a callback to be called when all child operations have completed.
  // |task_runner| specifies the task runner on which to call |join_callback|.
  // If no child operations are currently pending, the callback is posted
  // immediately. If child operations are pending, the callback is posted when
  // all child operations have completed. Only one callback at a time can be
  // registered with WhenJoined.
  void WhenJoined(fxl::RefPtr<fxl::TaskRunner> task_runner,
                  const fxl::Closure& join_callback);

  // Cancels a callback registered with WhenJoined if it hasn't run yet. The
  // return value indicates whether a callback was cancelled.
  bool Cancel();

 private:
  std::mutex mutex_;
  size_t counter_ = 0;
  fxl::Closure join_callback_;
  fxl::RefPtr<fxl::TaskRunner> join_callback_runner_;
};

}  // namespace media

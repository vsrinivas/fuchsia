// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/util/threadsafe_callback_joiner.h"

#include <lib/async/cpp/task.h>

namespace media_player {

std::ostream& operator<<(std::ostream& os, ThreadsafeCallbackJoiner* value) {
  return os << "ThreadsafeCallbackJoiner#" << std::hex << uint64_t(value);
}

// static
std::shared_ptr<ThreadsafeCallbackJoiner> ThreadsafeCallbackJoiner::Create() {
  return std::make_shared<ThreadsafeCallbackJoiner>();
}

ThreadsafeCallbackJoiner::ThreadsafeCallbackJoiner() {}

ThreadsafeCallbackJoiner::~ThreadsafeCallbackJoiner() {}

void ThreadsafeCallbackJoiner::Spawn() {
  std::lock_guard<std::mutex> locker(mutex_);
  ++counter_;
}

void ThreadsafeCallbackJoiner::Complete() {
  fxl::Closure callback;
  async_t* async;

  {
    std::lock_guard<std::mutex> locker(mutex_);
    FXL_DCHECK(counter_ != 0);
    --counter_;
    if (counter_ != 0 || !join_callback_) {
      return;
    }

    std::swap(callback, join_callback_);
    std::swap(async, join_callback_async_);
  }

  async::PostTask(
      async, [shared_this = shared_from_this(), callback]() { callback(); });
}

fxl::Closure ThreadsafeCallbackJoiner::NewCallback() {
  Spawn();
  std::shared_ptr<ThreadsafeCallbackJoiner> this_ptr = shared_from_this();
  FXL_DCHECK(!this_ptr.unique());
  return [this_ptr]() {
    FXL_DCHECK(this_ptr);
    this_ptr->Complete();
  };
}

void ThreadsafeCallbackJoiner::WhenJoined(async_t* async,
                                          const fxl::Closure& join_callback) {
  FXL_DCHECK(async);
  FXL_DCHECK(join_callback);

  {
    std::lock_guard<std::mutex> locker(mutex_);
    FXL_DCHECK(!join_callback_);
    if (counter_ != 0) {
      join_callback_ = join_callback;
      join_callback_async_ = async;
      return;
    }
  }

  async::PostTask(async, [shared_this = shared_from_this(), join_callback]() {
    join_callback();
  });
}

bool ThreadsafeCallbackJoiner::Cancel() {
  std::lock_guard<std::mutex> locker(mutex_);

  if (join_callback_) {
    join_callback_ = nullptr;
    join_callback_async_ = nullptr;
    return true;
  }

  return false;
}

}  // namespace media_player

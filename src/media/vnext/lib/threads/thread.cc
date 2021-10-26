// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/threads/thread.h"

namespace fmlib {

// static
Thread Thread::CreateNewThread(const char* thread_name) {
  FX_CHECK(thread_name);
  return Thread(std::make_shared<Shared>(thread_name));
}

// static
Thread Thread::CreateForLoop(async::Loop& loop) { return Thread(std::make_shared<Shared>(loop)); }

Thread::Shared::Shared(const char* thread_name)
    : loop_(&owned_loop_),
      owned_loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
      executor_(loop_->dispatcher()) {
  FX_CHECK(thread_name);
  loop_->StartThread(thread_name);
}

Thread::Shared::Shared(async::Loop& loop)
    : loop_(&loop),
      owned_loop_(&kAsyncLoopConfigNeverAttachToThread),
      executor_(loop_->dispatcher()) {}

Thread::Shared::~Shared() {
  if (loop_ == &owned_loop_) {
    loop_->Shutdown();
  }
}

}  // namespace fmlib

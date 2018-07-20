// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/threading/thread.h"

#include <limits.h>

#include <algorithm>

namespace fxl {

Thread::Thread(std::function<void(void)> runnable)
    : runnable_(std::move(runnable)), running_(false) {}

Thread::~Thread() { Join(); }

bool Thread::Run(size_t stack_size) {
  if (running_) {
    return false;
  }
  pthread_attr_t attr;

  if (pthread_attr_init(&attr) != 0) {
    return false;
  }

  stack_size = std::max<size_t>(PTHREAD_STACK_MIN, stack_size);

  if (pthread_attr_setstacksize(&attr, stack_size) != 0) {
    pthread_attr_destroy(&attr);
    return false;
  }

  auto result = pthread_create(&thread_, &attr, &Thread::Entry, this);
  if (result == 0) {
    running_ = true;
  }

  pthread_attr_destroy(&attr);

  return running_;
}

bool Thread::IsRunning() const { return running_; }

void Thread::Main(void) { runnable_(); }

void* Thread::Entry(void* context) {
  ((Thread*)context)->Main();
  return nullptr;
}

bool Thread::Join() {
  if (!running_) {
    return false;
  }

  int exit_code = pthread_join(thread_, nullptr);

  if (exit_code == 0) {
    running_ = false;
  }
  return !running_;
}

}  // namespace fxl

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/threading/thread.h"

#include <unistd.h>

#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/incoming_task_queue.h"
#include "lib/mtl/tasks/message_loop.h"

namespace mtl {

Thread::Thread()
    : running_(false),
      task_runner_(ftl::MakeRefCounted<mtl::internal::IncomingTaskQueue>()) {}

Thread::~Thread() {
  Join();
}

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

bool Thread::IsRunning() const {
  return running_;
}

ftl::RefPtr<ftl::TaskRunner> Thread::TaskRunner() const {
  return task_runner_;
}

void Thread::Main(void) {
  mtl::MessageLoop message_loop(task_runner_);
  message_loop.Run();
}

void* Thread::Entry(void* context) {
  ((Thread*)context)->Main();
  return nullptr;
}

bool Thread::Join() {
  if (!running_) {
    return false;
  }

  if (pthread_join(thread_, nullptr) == 0) {
    running_ = false;
  }
  return !running_;
}

}  // namespace mtl

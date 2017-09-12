// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/threading/thread.h"

#include "lib/fsl/tasks/incoming_task_queue.h"
#include "lib/fsl/tasks/message_loop.h"

namespace fsl {

Thread::Thread()
    : thread_([this] { Main(); }),
      task_runner_(fxl::MakeRefCounted<fsl::internal::IncomingTaskQueue>()) {}

Thread::~Thread() {}

bool Thread::Run(size_t stack_size) {
  return thread_.Run(stack_size);
}

bool Thread::IsRunning() const {
  return thread_.IsRunning();
}

fxl::RefPtr<fxl::TaskRunner> Thread::TaskRunner() const {
  return task_runner_;
}

void Thread::Main(void) {
  fsl::MessageLoop message_loop(task_runner_);
  message_loop.Run();
}

bool Thread::Join() {
  return thread_.Join();
}

}  // namespace fsl

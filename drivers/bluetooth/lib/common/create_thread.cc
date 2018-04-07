// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "create_thread.h"

#include <condition_variable>
#include <mutex>
#include <utility>

#include "lib/fsl/handles/object_info.h"
#include "lib/fsl/tasks/incoming_task_queue.h"
#include "lib/fsl/tasks/message_loop.h"

namespace btlib {
namespace common {
namespace {

void RunMessageLoop(std::string thread_name,
                    std::mutex* mtx,
                    std::condition_variable* cv,
                    fxl::RefPtr<fxl::TaskRunner>* out_task_runner,
                    async_t** out_dispatcher) {
  if (!thread_name.empty()) {
    fsl::SetCurrentThreadName(thread_name);
  }

  fsl::MessageLoop message_loop;
  {
    std::lock_guard<std::mutex> lock(*mtx);
    *out_task_runner = message_loop.task_runner();
    *out_dispatcher = message_loop.async();
  }
  cv->notify_one();

  message_loop.Run();
}

}  // namespace

std::thread CreateThread(fxl::RefPtr<fxl::TaskRunner>* out_task_runner,
                         async_t** out_dispatcher,
                         std::string thread_name) {
  FXL_DCHECK(out_task_runner);
  FXL_DCHECK(out_dispatcher);

  // This method blocks until the thread is spawned and provides us with an
  // async dispatcher pointer.
  std::mutex mtx;
  std::condition_variable cv;

  fxl::RefPtr<fxl::TaskRunner> task_runner;
  async_t* dispatcher = nullptr;
  std::thread thrd(RunMessageLoop, std::move(thread_name), &mtx, &cv,
                   &task_runner, &dispatcher);

  std::unique_lock<std::mutex> lock(mtx);
  cv.wait(lock, [&dispatcher] { return dispatcher != nullptr; });

  FXL_DCHECK(task_runner);
  FXL_DCHECK(dispatcher);
  *out_task_runner = task_runner;
  *out_dispatcher = dispatcher;

  return thrd;
}

}  // namespace common
}  // namespace btlib

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_DISPATCHER_H_
#define SRC_SYS_FUZZING_COMMON_DISPATCHER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>
#include <threads.h>

#include <atomic>
#include <memory>

#include "src/sys/fuzzing/common/run-once.h"

namespace fuzzing {

// This class wraps an async::Loop that is started on its own thread and joined when the object is
// destroyed. This makes it easy to create a FIDL dispatcher with RAII semantics.
class Dispatcher final {
 public:
  Dispatcher();
  ~Dispatcher();

  bool is_running() const { return running_; }
  async_dispatcher_t* get() const { return loop_->dispatcher(); }
  thrd_t thrd() const { return thrd_; }

  // Queues a task to be run on the dispatcher thread.
  zx_status_t PostTask(fit::closure&& task);

  // Shuts down the underlying async loop. This can be used to ensure references in callbacks are no
  // longer required.
  void Shutdown();

 private:
  void ShutdownImpl();

  std::atomic<bool> running_ = true;
  std::unique_ptr<async::Loop> loop_;
  thrd_t thrd_;
  RunOnce shutdown_;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_DISPATCHER_H_

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_THREAD_H_
#define LIB_MTL_THREAD_H_

#include <pthread.h>

#include <functional>

#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/tasks/task_runner.h"

namespace mtl {

class Thread {
 public:
  static constexpr size_t default_stack_size = 1 * 1024 * 1024;

  Thread();
  ~Thread();
  bool Run(size_t stack_size = default_stack_size);
  bool IsRunning() const;
  bool Join();
  ftl::RefPtr<ftl::TaskRunner> TaskRunner() const;

 private:
  bool running_;
  pthread_t thread_;
  ftl::RefPtr<mtl::internal::IncomingTaskQueue> task_runner_;
  void Main();
  static void* Entry(void* context);

  FTL_DISALLOW_COPY_AND_ASSIGN(Thread);
};

}  // namespace mtl

#endif  // LIB_MTL_THREAD_H_

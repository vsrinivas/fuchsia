// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_THREADING_THREAD_H_
#define LIB_MTL_THREADING_THREAD_H_

#include "lib/ftl/ftl_export.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/ftl/threading/thread.h"

namespace mtl {

namespace internal {
class IncomingTaskQueue;
}  // namespace internal

class FTL_EXPORT Thread {
 public:
  static constexpr size_t default_stack_size = 1 * 1024 * 1024;

  Thread();
  ~Thread();
  bool Run(size_t stack_size = default_stack_size);
  bool IsRunning() const;
  bool Join();
  ftl::RefPtr<ftl::TaskRunner> TaskRunner() const;

 private:
  void Main();

  ftl::Thread thread_;
  ftl::RefPtr<mtl::internal::IncomingTaskQueue> task_runner_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Thread);
};

}  // namespace mtl

#endif  // LIB_MTL_THREADING_THREAD_H_

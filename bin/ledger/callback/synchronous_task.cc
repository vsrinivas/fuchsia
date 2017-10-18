// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/callback/synchronous_task.h"

#include <mutex>

#include "lib/fxl/functional/make_copyable.h"

namespace callback {

bool RunSynchronously(const fxl::RefPtr<fxl::TaskRunner>& task_runner,
                      fxl::Closure task,
                      fxl::TimeDelta timeout) {
  bool ran = false;
  std::timed_mutex mutex;
  task_runner->PostTask(fxl::MakeCopyable(
      [guard = std::make_unique<std::lock_guard<std::timed_mutex>>(mutex),
       task = std::move(task), &ran] {
        task();
        ran = true;
      }));
  if (mutex.try_lock_for(std::chrono::nanoseconds(timeout.ToNanoseconds()))) {
    mutex.unlock();
  }
  return ran;
}

}  // namespace callback

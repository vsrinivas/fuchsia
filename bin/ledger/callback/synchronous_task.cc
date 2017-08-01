// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/callback/synchronous_task.h"

#include <mutex>

#include "lib/ftl/functional/make_copyable.h"

namespace callback {

bool RunSynchronously(const ftl::RefPtr<ftl::TaskRunner>& task_runner,
                      ftl::Closure task,
                      ftl::TimeDelta timeout) {
  bool ran = false;
  std::timed_mutex mutex;
  task_runner->PostTask(ftl::MakeCopyable([
    guard = std::make_unique<std::lock_guard<std::timed_mutex>>(mutex),
    task = std::move(task), &ran
  ] {
    task();
    ran = true;
  }));
  if (mutex.try_lock_for(std::chrono::nanoseconds(timeout.ToNanoseconds()))) {
    mutex.unlock();
  }
  return ran;
}

}  // namespace callback

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CALLBACK_SCOPED_TASK_RUNNER_H_
#define LIB_CALLBACK_SCOPED_TASK_RUNNER_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <zx/time.h>

#include "lib/callback/scoped_callback.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace callback {

// An object that wraps the posting logic of an |async_t|, but that is
// neither copyable nor moveable and will never run any task after being
// deleted.
// Because this class also acts as a WeakPtrFactory, it needs to be the last
// member of a class.
class ScopedTaskRunner {
 public:
  explicit ScopedTaskRunner(async_t* async);

  ~ScopedTaskRunner();

  // Posts a task to run as soon as possible.
  void PostTask(fit::closure task);

  // Posts a task to run as soon as possible after the specified |target_time|.
  void PostTaskForTime(fit::closure task, zx::time target_time);

  // Posts a task to run as soon as possible after the specified |delay|.
  void PostDelayedTask(fit::closure task, zx::duration delay);

  // Scope the given callback to the current task runner. This means that the
  // given callback will be called when the returned callback is called if and
  // only if this task runner has not been deleted.
  template <typename T>
  auto MakeScoped(T lambda) {
    return callback::MakeScoped(weak_factory_.GetWeakPtr(), std::move(lambda));
  }

 private:
  async_t* const async_;
  fxl::WeakPtrFactory<ScopedTaskRunner> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ScopedTaskRunner);
};

}  // namespace callback

#endif  // LIB_CALLBACK_SCOPED_TASK_RUNNER_H_

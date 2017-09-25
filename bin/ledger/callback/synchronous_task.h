// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CALLBACK_SYNCHRONOUS_TASK_H_
#define PERIDOT_BIN_LEDGER_CALLBACK_SYNCHRONOUS_TASK_H_

#include "lib/fxl/functional/closure.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/fxl/time/time_delta.h"

namespace callback {

// Posts |task| on |task_runner| and waits up to |timeout| for it to run.
// Returns |true| if the task has been run. The task can fail to run either
// because the message loop associated with |task_runner| is deleted, or because
// the calls timed out.
bool RunSynchronously(const fxl::RefPtr<fxl::TaskRunner>& task_runner,
                      fxl::Closure task,
                      fxl::TimeDelta timeout);
}  // namespace callback

#endif  // PERIDOT_BIN_LEDGER_CALLBACK_SYNCHRONOUS_TASK_H_

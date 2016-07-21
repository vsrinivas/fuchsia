// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_THREADING_CREATE_THREAD_H_
#define LIB_FTL_THREADING_CREATE_THREAD_H_

#include <thread>

#include "lib/ftl/tasks/task_runner.h"

namespace ftl {

// Creates a thread with a |MessageLoop|.
//
// The |task_runner| parameter is assigned a |TaskRunner| for running tasks on
// the newly created thread.
std::thread CreateThread(RefPtr<TaskRunner>* task_runner);

}  // namespace ftl

#endif  // LIB_FTL_TASKS_TASK_RUNNER_H_

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_THREADING_CREATE_THREAD_H_
#define LIB_MTL_THREADING_CREATE_THREAD_H_

#include <string>
#include <thread>

#include "lib/ftl/tasks/task_runner.h"

namespace mtl {

// Creates a thread with a |MessageLoop| named |thread_name|.
//
// The |task_runner| parameter is assigned a |TaskRunner| for running tasks on
// the newly created thread.
std::thread CreateThread(ftl::RefPtr<ftl::TaskRunner>* task_runner,
                         std::string thread_name = "message loop");

}  // namespace mtl

#endif  // LIB_FTL_TASKS_TASK_RUNNER_H_

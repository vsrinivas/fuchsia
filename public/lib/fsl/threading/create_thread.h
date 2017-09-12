// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FSL_THREADING_CREATE_THREAD_H_
#define LIB_FSL_THREADING_CREATE_THREAD_H_

#include <string>
#include <thread>

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/tasks/task_runner.h"

namespace fsl {

// Creates a thread with a |MessageLoop| named |thread_name|.
//
// The |task_runner| parameter is assigned a |TaskRunner| for running tasks on
// the newly created thread.
FXL_EXPORT std::thread CreateThread(fxl::RefPtr<fxl::TaskRunner>* task_runner,
                                    std::string thread_name = "message loop");

}  // namespace fsl

#endif  // LIB_FSL_THREADING_CREATE_THREAD_H_

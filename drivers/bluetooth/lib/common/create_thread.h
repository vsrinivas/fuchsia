// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <thread>

#include <lib/async/dispatcher.h>

#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/tasks/task_runner.h"

namespace btlib {
namespace common {

// Just like fsl::CreateThread but also returns an async dispatcher.
// TODO(NET-695): Remove this function once nothing depends on TaskRunner and
// MessageLoop.
std::thread CreateThread(fxl::RefPtr<fxl::TaskRunner>* task_runner,
                         async_t** dispatcher,
                         std::string thread_name = "message loop");

}  // namespace common
}  // namespace btlib

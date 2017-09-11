// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fxl/functional/closure.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/tasks/task_runner.h"

namespace bluetooth {
namespace common {

// Posts |callback| on |task_runner| and waits for it to finish running. |callback| will always
// finish running before this function returns. |task_runner| cannot be bound to the thread on which
// this function gets called.
//
// NOTE: This should generally be avoided. This is primarily intended for synchronous setup/shutdown
// sequences and unit tests.
void RunTaskSync(const fxl::Closure& callback, fxl::RefPtr<fxl::TaskRunner> task_runner);

}  // namespace common
}  // namespace bluetooth

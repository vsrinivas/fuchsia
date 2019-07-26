// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_FIT_EXAMPLES_UTILS_H_
#define ZIRCON_SYSTEM_UTEST_FIT_EXAMPLES_UTILS_H_

#include <lib/fit/promise.h>

namespace utils {

// Returns a task that completes a little later.
// Used by examples to simulate the passage of time in asynchronous logic.
fit::promise<> sleep_for_a_little_while();

// Resumes the task after some time has elapsed.
// Used by examples to simulate the passage of time in asynchronous logic.
void resume_in_a_little_while(fit::suspended_task task);

}  // namespace utils

#endif  // ZIRCON_SYSTEM_UTEST_FIT_EXAMPLES_UTILS_H_

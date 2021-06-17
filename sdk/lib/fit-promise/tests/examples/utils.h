// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_PROMISE_TESTS_EXAMPLES_UTILS_H_
#define LIB_FIT_PROMISE_TESTS_EXAMPLES_UTILS_H_

#include <lib/fpromise/promise.h>

namespace utils {

// Returns a task that completes a little later.
// Used by examples to simulate the passage of time in asynchronous logic.
fpromise::promise<> sleep_for_a_little_while();

// Resumes the task after some time has elapsed.
// Used by examples to simulate the passage of time in asynchronous logic.
void resume_in_a_little_while(fpromise::suspended_task task);

}  // namespace utils

#endif  // LIB_FIT_PROMISE_TESTS_EXAMPLES_UTILS_H_

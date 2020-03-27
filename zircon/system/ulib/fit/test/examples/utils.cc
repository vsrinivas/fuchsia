// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utils.h"

// These includes are only needed for simulating the passage of time
// in a portable manner for the purpose of writing these examples.
// You do not need to include these headers just to use |fit::promise|
// or |fit::future|.
#include <chrono>
#include <thread>

namespace utils {

fit::promise<> sleep_for_a_little_while() {
  // This is a rather inefficient way to wait for time to pass but it
  // is sufficient for our examples.
  return fit::make_promise([waited = false](fit::context& context) mutable {
    if (waited)
      return;
    waited = true;
    resume_in_a_little_while(context.suspend_task());
  });
}

void resume_in_a_little_while(fit::suspended_task task) {
  std::thread([task]() mutable {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    task.resume_task();
  }).detach();
}

}  // namespace utils

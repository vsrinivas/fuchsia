// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_UTILS_TASK_UTILS_H_
#define SRC_UI_SCENIC_LIB_UTILS_TASK_UTILS_H_

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

namespace utils {

// If `dispatcher` is the current dispatcher, invoke the `handler` closure immediately.  Otherwise,
// post a task to invoke `handler`.
inline void ExecuteOrPostTaskOnDispatcher(async_dispatcher_t* dispatcher, fit::closure handler) {
  if (dispatcher == async_get_default_dispatcher()) {
    handler();
  } else {
    async::PostTask(dispatcher, std::move(handler));
  }
}

}  // namespace utils

#endif  // SRC_UI_SCENIC_LIB_UTILS_TASK_UTILS_H_

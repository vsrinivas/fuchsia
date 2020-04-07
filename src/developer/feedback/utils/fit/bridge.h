// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_UTILS_FIT_BRIDGE_H_
#define SRC_DEVELOPER_FEEDBACK_UTILS_FIT_BRIDGE_H_

#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/bridge.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>
#include <lib/zx/time.h>

#include <type_traits>

#include "src/developer/feedback/utils/fit/timeout.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace fit {

// Wrapper around ::fit::bridge with the ability to post a task that that will complete the bridge
// at a certain point in the future if the bridge hasn't already been completed.
template <typename V = void, typename E = void>
class Bridge {
 public:
  Bridge(async_dispatcher_t* dispatcher, const std::string& task_name)
      : dispatcher_(dispatcher), task_name_(task_name) {}

  template <typename VV = V, typename = std::enable_if_t<std::is_void_v<VV>>>
  void CompleteOk() {
    if (bridge_.completer) {
      bridge_.completer.complete_ok();
    }
  }
  template <typename VV = V, typename = std::enable_if_t<!std::is_void_v<VV>>>
  void CompleteOk(VV value) {
    if (bridge_.completer) {
      bridge_.completer.complete_ok(std::move(value));
    }
  }

  void CompleteError() {
    if (bridge_.completer) {
      bridge_.completer.complete_error();
    }
  }

  bool IsAlreadyDone() const { return !bridge_.completer; }

  // Get the promise that will be ungated when |bridge_| is completed.
  ::fit::promise<V, E> WaitForDone() { return bridge_.consumer.promise_or(::fit::error()); }

  // Start the timeout and get the promise that will be ungated when |bridge_| is completed.
  ::fit::promise<V, E> WaitForDone(Timeout timeout) {
    timeout_ = std::move(timeout);

    if (zx_status_t status = timeout_task_.PostDelayed(dispatcher_, timeout_.value);
        status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to post timeout task, aborting " << task_name_;
      return ::fit::make_result_promise<V, E>(::fit::error());
    }

    return WaitForDone();
  }

 private:
  void AtTimeout() {
    if (IsAlreadyDone()) {
      return;
    }

    FX_LOGS(WARNING) << task_name_ << " timed out";
    if (timeout_.action) {
      (*timeout_.action)();
    }

    bridge_.completer.complete_error();
  }

  async_dispatcher_t* dispatcher_;
  const std::string task_name_;
  ::fit::bridge<V, E> bridge_;

  async::TaskClosureMethod<Bridge, &Bridge::AtTimeout> timeout_task_{this};

  Timeout timeout_;
};

}  // namespace fit
}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_UTILS_FIT_BRIDGE_H_

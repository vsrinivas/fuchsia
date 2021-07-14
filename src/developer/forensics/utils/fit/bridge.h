// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_FIT_BRIDGE_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_FIT_BRIDGE_H_

#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <type_traits>

#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fit/timeout.h"

namespace forensics {
namespace fit {

// Wrapper around ::fpromise::bridge with the ability to post a task that that will complete the
// bridge at a certain point in the future if the bridge hasn't already been completed.
template <typename V = void>
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

  void CompleteError(Error error) {
    if (bridge_.completer) {
      bridge_.completer.complete_error(error);
    }
  }

  bool IsAlreadyDone() const { return !bridge_.completer; }

  // Get the promise that will be ungated when |bridge_| is completed.
  ::fpromise::promise<V, Error> WaitForDone() { return bridge_.consumer.promise(); }

  // Start the timeout and get the promise that will be ungated when |bridge_| is completed.
  ::fpromise::promise<V, Error> WaitForDone(Timeout timeout) {
    timeout_ = std::move(timeout);

    if (zx_status_t status = timeout_task_.PostDelayed(dispatcher_, timeout_.value);
        status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to post timeout task, aborting " << task_name_;
      return ::fpromise::make_result_promise<V, Error>(
          ::fpromise::error(Error::kAsyncTaskPostFailure));
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

    bridge_.completer.complete_error(Error::kTimeout);
  }

  async_dispatcher_t* dispatcher_;
  const std::string task_name_;
  ::fpromise::bridge<V, Error> bridge_;

  async::TaskClosureMethod<Bridge, &Bridge::AtTimeout> timeout_task_{this};

  Timeout timeout_;
};

}  // namespace fit
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_FIT_BRIDGE_H_

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_NETEMUL_SYNC_CALLBACK_WATCH_H_
#define LIB_NETEMUL_SYNC_CALLBACK_WATCH_H_

#include <lib/async/cpp/task.h>
#include <src/lib/fxl/macros.h>

namespace netemul {

// Helper class to hold onto callbacks
template <typename T>
class CallbackWatch {
 public:
  explicit CallbackWatch(T callback)
      : callback_(std::move(callback)), timeout_(this) {}

  virtual ~CallbackWatch() = default;

  void TaskTimeout(async_dispatcher_t* dispatcher, async::TaskBase* task,
                   zx_status_t status) {
    if (status == ZX_OK) {
      OnTimeout();
    }
  }

  template <typename... Args>
  void FireCallback(Args... args) {
    if (callback_) {
      callback_(std::forward<Args>(args)...);
      callback_ = nullptr;
    }
  }

  bool valid() const { return static_cast<bool>(callback_); }

  void PostTimeout(async_dispatcher_t* dispatcher, int64_t timeout_nanos) {
    timeout_.PostDelayed(dispatcher, zx::nsec(timeout_nanos));
  }

  virtual void OnTimeout() = 0;

 private:
  T callback_;
  async::TaskMethod<CallbackWatch, &CallbackWatch::TaskTimeout> timeout_;
  FXL_DISALLOW_COPY_AND_ASSIGN(CallbackWatch);
};

}  // namespace netemul

#endif  // LIB_NETEMUL_SYNC_CALLBACK_WATCH_H_

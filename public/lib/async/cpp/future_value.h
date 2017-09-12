// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <list>
#include <memory>

#include "garnet/public/lib/fxl/functional/make_copyable.h"
#include "garnet/public/lib/fxl/macros.h"
#include "garnet/public/lib/fsl/tasks/message_loop.h"

namespace maxwell {

// Supports asynchronous computation by queueing tasks that depend on a value
// that is to be produced at an unspecified time in the future.
//
// The value type must be either copyable or move-only. FutureValue itself is
// move-only.
//
// When the FutureValue is destroyed, callbacks enqueued using OnValue() that
// have not yet run are still run if the FutureValue has a value set
// (SetValue() or the assignment operator were invoked). Otherwise, the pending
// callbacks are discarded.
//
// Usage:
//
// FutureValue<int> future_int;
//
// future_int.OnValue([] (const int& value) {
//   FXL_LOG(INFO) << "Finally using the int: " << value;
// });
//
// ... some time later, asynchronously ...
//
// future_int = 10;

template <typename T>
class FutureValue {
 public:
  using UseValueFunction = std::function<void(const T&)>;

  FutureValue() {}
  FutureValue(FutureValue<T>&& o)
      : value_(o.value_), on_ready_(std::move(o.on_ready_)) {}

  // Moves |value| to |value_| and dispatches all waiting tasks.
  void SetValue(T value) {
    FXL_DCHECK(!value_) << "SetValue() called twice.";
    value_.reset(new T(std::move(value)));

    for (auto it = on_ready_.begin(); it != on_ready_.end(); ++it) {
      Dispatch(std::move(*it));
    }
    on_ready_.clear();
  }

  void operator=(const T& value) { SetValue(value); }
  void operator=(T&& value) { SetValue(std::move(value)); }

  // If |value_| is available, dispatches |fn| immediately. Otherwise, queues
  // |fn| for dispatch later when SetValue() is called. |fn| may be move-only.
  void OnValue(UseValueFunction fn) {
    if (value_) {
      // Put it on the event loop immediately.
      Dispatch(std::move(fn));
      return;
    }
    on_ready_.push_back(std::move(fn));
  }

 private:
  void Dispatch(UseValueFunction fn) {
    fsl::MessageLoop::GetCurrent()->task_runner()->PostTask(
        [ value = value_, fn = std::move(fn) ] { fn(*value); });
  }

  std::shared_ptr<T> value_;
  std::list<UseValueFunction> on_ready_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FutureValue);
};

}  // namespace maxwell

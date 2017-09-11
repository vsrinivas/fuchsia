// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_FUNCTIONAL_CANCELABLE_CALLBACK_H_
#define LIB_FXL_FUNCTIONAL_CANCELABLE_CALLBACK_H_

#include <functional>

#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace fxl {

// CancelableCallback is a wrapper around std::function that allows the
// cancellation of a callback. CancelableCallback takes a reference on the
// wrapped callback until this object is destroyed or Reset()/Cancel() are
// called.
//
// THREAD-SAFETY:
//
// CancelableCallback objects must be created on, posted to, canceled on, and
// destroyed on the same thread.
//
//
// EXAMPLE USAGE:
//
// void MyTimeoutCallback(std::string message) {
//   FXL_LOG(INFO) << "Timeout has expired: " << message
// }
//
// CancelableClosure cancelable(
//     std::bind(&MyTimeoutCallback, "Drinks at Foo Bar!"));
//
// my_task_runner->PostDelayedTask(cancelable.callback(),
//                                 TimeDelta::FromSeconds(5));
// ...
//
// cancelable.Cancel();  // Assuming this happens before the 5 seconds expire.
template <typename Sig>
class CancelableCallback;

template <typename... Args>
class CancelableCallback<void(Args...)> {
 public:
  CancelableCallback() : weak_ptr_factory_(this) {}

  explicit CancelableCallback(const std::function<void(Args...)>& callback)
      : callback_(callback), weak_ptr_factory_(this) {
    FXL_DCHECK(callback_);
    BindWrapper();
  }

  // Cancels and drops the reference to the wrapped callback.
  void Cancel() {
    weak_ptr_factory_.InvalidateWeakPtrs();
    wrapper_ = nullptr;
    callback_ = nullptr;
  }

  // Returns true if the wrapped callback has been canceled.
  bool IsCanceled() const { return !callback_; }

  // Returns a callback that can be disabled by calling Cancel(). This returns a
  // null callback if this object currently does not wrap around a callback,
  // e.g. after a call to Cancel() or when in default-constructed state.
  const std::function<void(Args...)>& callback() const { return wrapper_; }

  // Sets |callback| as the closure that may be canceled. |callback| may not be
  // null. Outstanding and any previously wrapped callbacks are canceled.
  void Reset(const std::function<void(Args...)>& callback) {
    FXL_DCHECK(callback);
    Cancel();

    callback_ = callback;
    BindWrapper();
  }

 private:
  void BindWrapper() {
    auto self = weak_ptr_factory_.GetWeakPtr();
    wrapper_ = [self](Args... args) {
      if (!self)
        return;
      FXL_DCHECK(self->callback_);
      self->callback_(std::forward<Args>(args)...);
    };
  }

  // The closure that wraps around |callback_|. This acts as the cancelable
  // closure that gets vended out to clients.
  std::function<void(Args...)> wrapper_;

  // The stored closure that may be canceled.
  std::function<void(Args...)> callback_;

  // Used to make sure that |wrapper_| is not run when this object is destroyed.
  fxl::WeakPtrFactory<CancelableCallback> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CancelableCallback);
};

using CancelableClosure = CancelableCallback<void(void)>;

}  // namespace fxl

#endif  // LIB_FXL_FUNCTIONAL_CANCELABLE_CALLBACK_H_

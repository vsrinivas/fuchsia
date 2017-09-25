// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CALLBACK_CANCELLABLE_HELPER_H_
#define PERIDOT_BIN_LEDGER_CALLBACK_CANCELLABLE_HELPER_H_

#include <type_traits>

#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/logging.h"
#include "peridot/bin/ledger/callback/cancellable.h"

namespace callback {

namespace internal {

template <typename T>
class WrappedCancellableCallback {
 public:
  WrappedCancellableCallback(T wrapped_callback,
                             bool* is_done_ptr,
                             fxl::Closure post_run)
      : wrapped_callback_(std::move(wrapped_callback)),
        post_run_(std::move(post_run)),
        is_done_ptr_(is_done_ptr) {
    FXL_DCHECK(post_run_);
  }

  template <typename... ArgType>
  void operator()(ArgType&&... args) const {
    if (*is_done_ptr_) {
      return;
    }
    *is_done_ptr_ = true;
    auto call_on_exit = fxl::MakeAutoCall(std::move(post_run_));
    return wrapped_callback_(std::forward<ArgType>(args)...);
  }

 private:
  T wrapped_callback_;
  fxl::Closure post_run_;
  // This is safe as long as a refptr to the CancellableImpl is held by
  // |post_run_| callback.
  bool* is_done_ptr_;
};

}  // namespace internal

// Implementation of |Cancellable| for services. A service that wants to return
// a |Cancellable| can return an instance of |CancellableImpl|. It passes to its
// factory method a callback that will be executed if the client calls the
// |Cancel()| method.
//
// A client callback associated with the cancellable request can be wrapped
// using |WrapCallback()|. This ensures that:
//  - the cancellable becomes done automatically when the wrapped callback is
//    called
//  - if the wrapped callback is called after the request was cancelled, the
//    client callback is not called
class CancellableImpl final : public Cancellable {
 public:
  inline static fxl::RefPtr<CancellableImpl> Create(fxl::Closure on_cancel) {
    return fxl::AdoptRef(new CancellableImpl(std::move(on_cancel)));
  }

  template <typename T>
  internal::WrappedCancellableCallback<T> WrapCallback(T callback) {
    return internal::WrappedCancellableCallback<T>(
        callback, &is_done_, [ref_ptr = fxl::RefPtr<CancellableImpl>(this)] {
          FXL_DCHECK(ref_ptr->is_done_);
          // Never call the done callback after Cancel(). Note that Cancel() can
          // be called from within the wrapped callback.
          if (ref_ptr->is_cancelled_) {
            return;
          }
          if (ref_ptr->on_done_) {
            ref_ptr->on_done_();
          }
        });
  }

  // Cancellable
  void Cancel() override;
  bool IsDone() override;
  void SetOnDone(fxl::Closure callback) override;

 private:
  explicit CancellableImpl(fxl::Closure on_cancel);

  bool is_cancelled_;
  fxl::Closure on_cancel_;
  bool is_done_;
  fxl::Closure on_done_;
};

// Creates a cancellable that is already done.
fxl::RefPtr<callback::Cancellable> CreateDoneCancellable();

}  // namespace callback

#endif  // PERIDOT_BIN_LEDGER_CALLBACK_CANCELLABLE_HELPER_H_

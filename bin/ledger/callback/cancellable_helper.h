// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CALLBACK_CANCELLABLE_HELPER_H_
#define APPS_LEDGER_SRC_CALLBACK_CANCELLABLE_HELPER_H_

#include <type_traits>

#include "apps/ledger/src/callback/cancellable.h"
#include "lib/ftl/functional/auto_call.h"
#include "lib/ftl/logging.h"

namespace callback {

namespace internal {

template <typename T>
class WrappedCancellableCallback {
 public:
  WrappedCancellableCallback(T wrapped_callback,
                             bool* is_done_ptr,
                             ftl::Closure post_run)
      : wrapped_callback_(std::move(wrapped_callback)),
        post_run_(std::move(post_run)),
        is_done_ptr_(is_done_ptr) {
    FTL_DCHECK(post_run_);
  }

  template <typename... ArgType>
  void operator()(ArgType&&... args) const {
    if (*is_done_ptr_) {
      return;
    }
    *is_done_ptr_ = true;
    auto call_on_exit = ftl::MakeAutoCall(std::move(post_run_));
    return wrapped_callback_(std::forward<ArgType>(args)...);
  }

 private:
  T wrapped_callback_;
  ftl::Closure post_run_;
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
  inline static ftl::RefPtr<CancellableImpl> Create(ftl::Closure on_cancel) {
    return ftl::AdoptRef(new CancellableImpl(std::move(on_cancel)));
  }

  template <typename T>
  internal::WrappedCancellableCallback<T> WrapCallback(T callback) {
    return internal::WrappedCancellableCallback<T>(
        callback, &is_done_, [ref_ptr = ftl::RefPtr<CancellableImpl>(this)] {
          FTL_DCHECK(ref_ptr->is_done_);
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
  void SetOnDone(ftl::Closure callback) override;

 private:
  CancellableImpl(ftl::Closure on_cancel);

  bool is_cancelled_;
  ftl::Closure on_cancel_;
  bool is_done_;
  ftl::Closure on_done_;
};

// Creates a cancellable that is already done.
ftl::RefPtr<callback::Cancellable> CreateDoneCancellable();

}  // namespace callback

#endif  // APPS_LEDGER_SRC_CALLBACK_CANCELLABLE_HELPER_H_

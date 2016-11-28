// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CALLBACK_CANCELLABLE_HELPER_H_
#define APPS_LEDGER_SRC_CALLBACK_CANCELLABLE_HELPER_H_

#include <type_traits>

#include "apps/ledger/src/callback/cancellable.h"
#include "lib/ftl/logging.h"

namespace callback {

namespace internal {

template <typename T>
class LambdaPostRunWrapper {
 public:
  LambdaPostRunWrapper(T func, ftl::Closure callback)
      : func_(std::move(func)), callback_(std::move(callback)) {
    FTL_DCHECK(callback_);
  }

  template <typename... ArgType,
            typename std::enable_if<!std::is_void<typename std::result_of<
                T(ArgType...)>::type>::value>::type* = nullptr>
  auto operator()(ArgType&&... args) const {
    auto callback = std::move(callback_);
    auto result = func_(std::forward<ArgType>(args)...);
    callback();
    return result;
  }

  template <typename... ArgType,
            typename std::enable_if<std::is_void<typename std::result_of<
                T(ArgType...)>::type>::value>::type* = nullptr>
  void operator()(ArgType&&... args) const {
    auto callback = std::move(callback_);
    func_(std::forward<ArgType>(args)...);
    callback();
  }

 private:
  T func_;
  ftl::Closure callback_;
};

}  // namespace internal

// Implementation of |Cancellable| for services. A service that wants to return
// a |Cancellable| can return an instance of |CancellableImpl|. It passes to its
// factory method a callback that will be executed if the client calls the
// |Cancel| method. It then wrap the client callback with the |WrapCallback|
// method which will then handle the lifecycle of the |Cancellable| ensuring
// that the |OnDone| callback is correctly called and that the |IsDone| method
// is correct.
class CancellableImpl final : public Cancellable {
 public:
  inline static ftl::RefPtr<CancellableImpl> Create(ftl::Closure on_cancel) {
    return ftl::AdoptRef(new CancellableImpl(std::move(on_cancel)));
  }

  template <typename T>
  internal::LambdaPostRunWrapper<T> WrapCallback(T callback) {
    return internal::LambdaPostRunWrapper<T>(
        callback, [ref_ptr = ftl::RefPtr<CancellableImpl>(this)]() {
          if (ref_ptr->is_done_)
            return;
          ref_ptr->is_done_ = true;
          if (ref_ptr->on_done_)
            ref_ptr->on_done_();
        });
  }

  // Cancellable
  void Cancel() override;
  bool IsDone() override;
  void SetOnDone(ftl::Closure callback) override;

 private:
  CancellableImpl(ftl::Closure on_cancel);

  ftl::Closure on_cancel_;
  bool is_done_;
  ftl::Closure on_done_;
};

// Creates a cancellable that is already done.
ftl::RefPtr<callback::Cancellable> CreateDoneCancellable();

}  // namespace callback

#endif  // APPS_LEDGER_SRC_CALLBACK_CANCELLABLE_HELPER_H_

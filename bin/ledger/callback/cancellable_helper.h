// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CALLBACK_CANCELLABLE_HELPER_H_
#define APPS_LEDGER_SRC_CALLBACK_CANCELLABLE_HELPER_H_

#include <type_traits>

#include "apps/ledger/src/callback/cancellable.h"

namespace callback {

namespace internal {

template <typename T>
class LambdaPostRunWrapper {
 public:
  LambdaPostRunWrapper(T func, std::function<void()> callback)
      : func_(std::move(func)), callback_(std::move(callback)) {}

  template <typename... ArgType,
            typename std::enable_if<!std::is_void<typename std::result_of<
                T(ArgType...)>::type>::value>::type* = nullptr>
  auto operator()(ArgType&&... args) const {
    auto result = func_(std::forward<ArgType>(args)...);
    callback_();
    return result;
  }

  template <typename... ArgType,
            typename std::enable_if<std::is_void<typename std::result_of<
                T(ArgType...)>::type>::value>::type* = nullptr>
  void operator()(ArgType&&... args) const {
    func_(std::forward<ArgType>(args)...);
    callback_();
  }

 private:
  T func_;
  std::function<void()> callback_;
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
  inline static ftl::RefPtr<CancellableImpl> Create(
      std::function<void()> on_cancel) {
    return ftl::AdoptRef(new CancellableImpl(std::move(on_cancel)));
  }

  template <typename T>
  internal::LambdaPostRunWrapper<T> WrapCallback(T callback) {
    return internal::LambdaPostRunWrapper<T>(callback, [this]() {
      if (is_done_)
        return;
      is_done_ = true;
      if (on_done_)
        on_done_();
    });
  }

  // Cancellable
  void Cancel() override;
  bool IsDone() override;
  void OnDone(std::function<void()> callback) override;

 private:
  CancellableImpl(std::function<void()> on_cancel);

  std::function<void()> on_cancel_;
  bool is_done_;
  std::function<void()> on_done_;
};

}  // namespace callback

#endif  // APPS_LEDGER_SRC_CALLBACK_CANCELLABLE_HELPER_H_

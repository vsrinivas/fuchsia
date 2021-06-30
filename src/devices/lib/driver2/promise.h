// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DRIVER2_PROMISE_H_
#define SRC_DEVICES_LIB_DRIVER2_PROMISE_H_

#include <lib/fit/promise.h>

#include "src/devices/lib/driver2/namespace.h"

namespace promise {

template <typename T>
class ContinueWith;

namespace internal {

// Connects to the given |path| in |ns|, and returns a fit::result containing a
// fidl::Client on success.
template <typename T>
fit::result<fidl::Client<T>, zx_status_t> ConnectWithResult(const Namespace& ns,
                                                            async_dispatcher_t* dispatcher,
                                                            std::string_view path) {
  auto result = ns.Connect(path);
  if (result.is_error()) {
    return fit::error(result.status_value());
  }
  return fit::ok(fidl::Client<T>(std::move(*result), dispatcher));
}

// Helps to call a fit::promise lambda function.
template <typename Func, size_t size>
struct ContinueCall {
  static_assert(size == 2, "Unexpected number of arguments");

  using return_type = typename fit::callable_traits<Func>::return_type;
  using value_type = typename fit::callable_traits<Func>::args::template at<1>;

  static auto Call(Func func) {
    return [func = std::move(func), done = false, with = ContinueWith<return_type>()](
               fit::context& context, value_type value) mutable -> return_type {
      if (done) {
        return with.Result();
      }
      done = true;
      with.Suspend(context);
      return func(with, std::forward<value_type>(value));
    };
  }
};

template <typename Func>
struct ContinueCall<Func, 1> {
  using return_type = typename fit::callable_traits<Func>::return_type;

  static auto Call(Func func) {
    return [func = std::move(func), done = false,
            with = ContinueWith<return_type>()](fit::context& context) mutable -> return_type {
      if (done) {
        return with.Result();
      }
      done = true;
      with.Suspend(context);
      return func(with);
    };
  }
};

}  // namespace internal

// Connects to the given |path| in |ns|, and returns a fit::promise containing a
// fidl::Client on success.
template <typename T>
fit::promise<fidl::Client<T>, zx_status_t> Connect(const Namespace& ns,
                                                   async_dispatcher_t* dispatcher,
                                                   std::string_view path) {
  return fit::make_result_promise(internal::ConnectWithResult<T>(ns, dispatcher, path));
}

// Wraps a fit::suspended_task in order to provide an ergonomic way to suspend
// and resume when using a FIDL callback, without the need for a fit::bridge.
//
// TODO(fxbug.dev/62049): Consider moving this into libfit.
template <typename T>
class ContinueWith {
 public:
  // Returns |result| when the promise is resumed.
  void Return(T result) {
    task_.resume_task();
    result_.swap(result);
  }

 private:
  fit::suspended_task task_;
  T result_;

  void Suspend(fit::context& context) {
    auto task = context.suspend_task();
    task_.swap(task);
  }

  T Result() { return std::move(result_); }

  template <typename Func, size_t size>
  friend struct internal::ContinueCall;
};

// Allows a fit::promise compatible lambda function to be easily suspended and
// resumed. This is achieved by replacing the first argument with a ContinueWith
// object that can capture the result of a callback and resume execution of a
// promise.
//
// TODO(fxbug.dev/62049): Consider moving this into libfit.
template <typename Func>
auto Continue(Func func) {
  constexpr size_t size = fit::callable_traits<Func>::args::size;
  return internal::ContinueCall<Func, size>::Call(std::move(func));
}

}  // namespace promise

#endif  // SRC_DEVICES_LIB_DRIVER2_PROMISE_H_

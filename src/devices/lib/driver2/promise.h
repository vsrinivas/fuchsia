// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DRIVER2_PROMISE_H_
#define SRC_DEVICES_LIB_DRIVER2_PROMISE_H_

#include <lib/fpromise/promise.h>

#include "src/devices/lib/driver2/namespace.h"

namespace driver {

template <typename T>
class ContinueWith;

namespace internal {

// Connects to the given |path| in |ns|, and returns a fpromise::result containing a
// fidl::WireSharedClient on success.
template <typename T>
fpromise::result<fidl::WireSharedClient<T>, zx_status_t> ConnectWithResult(
    const driver::Namespace& ns, async_dispatcher_t* dispatcher, std::string_view path) {
  auto result = ns.Connect<T>(path);
  if (result.is_error()) {
    return fpromise::error(result.status_value());
  }
  fidl::WireSharedClient<T> client(std::move(*result), dispatcher);
  return fpromise::ok(std::move(client));
}

// Helps to call a fpromise::promise lambda function.
template <typename Func, size_t size>
struct ContinueCall {
  static_assert(size == 2, "Unexpected number of arguments");

  using return_type = typename fit::callable_traits<Func>::return_type;
  using value_type = typename fit::callable_traits<Func>::args::template at<1>;

  static auto Call(Func func) {
    return [func = std::move(func), done = false, with = ContinueWith<return_type>()](
               fpromise::context& context, value_type value) mutable -> return_type {
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
            with = ContinueWith<return_type>()](fpromise::context& context) mutable -> return_type {
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

// Connects to the given |path| in |ns|, and returns a fpromise::promise containing a
// fidl::WireSharedClient on success.
template <typename T>
fpromise::promise<fidl::WireSharedClient<T>, zx_status_t> Connect(
    const driver::Namespace& ns, async_dispatcher_t* dispatcher,
    std::string_view path = fidl::DiscoverableProtocolDefaultPath<T>) {
  return fpromise::make_result_promise(internal::ConnectWithResult<T>(ns, dispatcher, path));
}

// Wraps a fpromise::suspended_task in order to provide an ergonomic way to suspend
// and resume when using a FIDL callback, without the need for a fpromise::bridge.
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
  fpromise::suspended_task task_;
  T result_;

  void Suspend(fpromise::context& context) {
    auto task = context.suspend_task();
    task_.swap(task);
  }

  T Result() { return std::move(result_); }

  template <typename Func, size_t size>
  friend struct internal::ContinueCall;
};

// Allows a fpromise::promise compatible lambda function to be easily suspended and
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

}  // namespace driver

#endif  // SRC_DEVICES_LIB_DRIVER2_PROMISE_H_

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_COMMON_COMPLETION_CALLBACK_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_COMMON_COMPLETION_CALLBACK_H_

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

// Represents a completion callback function that MUST be called and takes an
// error and an optional list of parameters.
//
// It is a wrapper around a fit::callback that asserts if is destroyed before
// the callback is run. When using continuations the thread of execution will
// be lost if any step every forgets to call the completion callback.
//
// A completion callback always takes a "const Err&" as its first parameter and
// returns void. The template arguments to the CompletionCallback are the
// parameters following the Err.
//
// The operator() overloads simplify execution by taking either an "Err" (which
// assumes all other parameters are default-constructed) or the parameter list
// (which assumes no error);
//
// Receiver of a callback example:
//
//   void DoStuff(int some_param, CompletionCallback<ExprValue> cb) {
//     Err err = Foo();
//     if (err.has_error())
//       cb(err);
//     else
//       cb(ExprValue(5));
//   }
//
// Caller example:
//
//   int main() {
//     DoStuff(1, [](const Err& err, ExprValue v) {
//       if (err.has_error()) {
//         ...
//       } else {
//         ...
//       }
//     });
//   }
//
// If the parameters can't be default-constructed in the error case there is
// also a version that takes all callback parameters.
template <typename... Args>
class CompletionCallback {
 public:
  using Callback = fit::callback<void(const Err&, Args...)>;

  // These constructors are mirrors of fit::callback. See that class for more.
  CompletionCallback() = default;

  CompletionCallback(decltype(nullptr)) : callback_(nullptr) {}

  CompletionCallback(void (*target)(const Err& err, Args...)) : callback_(target) {}

  // For functors, we need to capture the raw type but also restrict on the
  // existence of an appropriate operator () to resolve overloads and implicit
  // casts properly.
  template <typename Callable,
            typename = std::enable_if_t<std::is_convertible<
                decltype(std::declval<Callable&>()(Err(), std::declval<Args>()...)), void>::value>>
  CompletionCallback(Callable target) : callback_(std::move(target)) {}

  // Delete specialization for fit::callback.
  template <size_t other_inline_target_size, bool other_require_inline>
  CompletionCallback(
      ::fit::callback_impl<other_inline_target_size, other_require_inline, void(Args...)>) = delete;

  CompletionCallback(CompletionCallback&& other) : callback_(std::move(other.callback_)) {}

  ~CompletionCallback() {
    FXL_CHECK(!callback_) << "Completion callback not run before destruction.";
  }

  // Assignment from a callable function. See fit::callback.
  //
  // Unlike fit::callback, this will assert if the current object has a
  // function that has not been called.
  template <typename Callable,
            typename = std::enable_if_t<std::is_convertible<
                decltype(std::declval<Callable&>()(Err(), std::declval<Args>()...)), void>::value>>
  CompletionCallback& operator=(Callable target) {
    FXL_CHECK(!callback_) << "Overwriting a completion callback without calling it.";
    callback_ = std::move(target);
    return *this;
  }

  // Move assignment
  CompletionCallback& operator=(CompletionCallback&& other) {
    if (&other == this)
      return *this;

    FXL_CHECK(!callback_) << "Overwriting a completion callback without calling it.";
    callback_ = std::move(other.callback_);
    return *this;
  }

  explicit operator bool() const { return callback_; }

  // This version takes all parameters to the callback, the "Err" first
  // parameter and all others.
  //
  // It is parameterized to only be available when there are more than one
  // arguments to avoid collisions with the error case below when there are no
  // other parameters.
  template <typename = std::enable_if<(sizeof...(Args) != 0)>>
  void operator()(const Err& err, Args... args) {
    callback_(err, std::forward<Args>(args)...);
    callback_ = nullptr;
  }

  // Execute the callback with the given error.
  //
  // The other parameters to the callback are default-constructed. If this
  // won't compile because a parameter can't be default constructed or the
  // code needs to specify one of them manually in the error case, use the
  // version above that takes all parameters.
  void operator()(const Err& err) {
    FXL_CHECK(err.has_error()) << "Expected error to be set.";
    callback_(err, Args()...);
    callback_ = nullptr;
  }

  // Executes the callback with no error. Only the callback parameters
  // following the "Err" need to be specified.
  void operator()(Args... args) {
    callback_(Err(), std::forward<Args>(args)...);
    callback_ = nullptr;
  }

 private:
  Callback callback_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_COMMON_COMPLETION_CALLBACK_H_

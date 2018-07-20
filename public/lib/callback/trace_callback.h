// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CALLBACK_TRACE_CALLBACK_H_
#define LIB_CALLBACK_TRACE_CALLBACK_H_

#include <cstring>
#include <functional>
#include <utility>

#include <lib/fit/function.h>
#include <trace/event.h>

#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/functional/make_copyable.h"

namespace callback {

namespace internal {

template <typename C>
class TracingLambda {
 public:
  TracingLambda(C callback, uint64_t id, const char* category, const char* name,
                const char* callback_name)
      : id_(id),
        category_(category),
        name_(name),
        callback_name_(callback_name),
        callback_(std::move(callback)),
        did_run_or_moved_out_(false),
        trace_enabled_(true) {}

  explicit TracingLambda(C callback)
      : id_(0u),
        category_(nullptr),
        name_(nullptr),
        callback_name_(nullptr),
        callback_(std::move(callback)),
        did_run_or_moved_out_(false),
        trace_enabled_(false) {}

  // Copy constructor so that the resulting callback can be used as an
  // fit::function, but acts as a move constructor. Only the last copy is valid.
  TracingLambda(TracingLambda&& other) noexcept
      : id_(other.id_),
        category_(other.category_),
        name_(other.name_),
        callback_name_(other.callback_name_),
        callback_(std::move(other.callback_)),
        did_run_or_moved_out_(other.did_run_or_moved_out_),
        trace_enabled_(other.trace_enabled_) {
    FXL_DCHECK(!other.did_run_or_moved_out_);
    other.did_run_or_moved_out_ = true;
  }

  ~TracingLambda() {
    if (!did_run_or_moved_out_ && trace_enabled_) {
      TRACE_ASYNC_END(category_, name_, id_, "NotRun", true);
    }
  }

  template <typename... ArgType>
  auto operator()(ArgType&&... args) const {
    FXL_DCHECK(!did_run_or_moved_out_);
    did_run_or_moved_out_ = true;
    if (trace_enabled_) {
      TRACE_ASYNC_END(category_, name_, id_);
      TRACE_ASYNC_BEGIN(category_, callback_name_, id_);
    }
    auto guard = fxl::MakeAutoCall([trace_enabled = trace_enabled_, id = id_,
                                    category = category_,
                                    callback_name = callback_name_] {
      if (trace_enabled) {
        TRACE_ASYNC_END(category, callback_name, id);
      }
    });

    return callback_(std::forward<ArgType>(args)...);
  }

 private:
  const uint64_t id_;
  const char* const category_;
  const char* const name_;
  const char* const callback_name_;
  mutable C callback_;
  mutable bool did_run_or_moved_out_;
  bool trace_enabled_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TracingLambda);
};

// NOLINT suppresses check about 'args' being unused. It might be used in the
// future if TRACE_CALLBACK (see below) is used providing additional arguments.
template <typename C, typename... ArgType>
auto TraceCallback(C callback, const char* category, const char* name,
                   const char* callback_name,
                   ArgType... args) {  // NOLINT
  uint64_t id = TRACE_NONCE();
  TRACE_ASYNC_BEGIN(category, name, id, std::forward<ArgType>(args)...);
  return TracingLambda<C>(std::move(callback), id, category, name,
                          callback_name);
}

template <typename C>
auto TraceCallback(C callback) {
  return TracingLambda<C>(std::move(callback));
}

// Identity functions. This is used to ensure that a C string is a compile time
// constant in conjunction with __builtin_strlen.
template <size_t S>
constexpr const char* CheckConstantCString(const char* value) {
  return value;
}

}  // namespace internal
}  // namespace callback

// Wraps the given callback so that it's traced using async tracing from the
// time it's wrapped to the time it completes. Can be used only for callbacks
// that will be called at most once.
#define TRACE_CALLBACK(cb, category, name, args...)                            \
  ([&]() mutable {                                                             \
    auto trace_local_cb__ = cb;                                                \
    return (                                                                   \
        TRACE_ENABLED()                                                        \
            ? ::callback::internal::TraceCallback(                             \
                  std::move(trace_local_cb__),                                 \
                  ::callback::internal::CheckConstantCString<__builtin_strlen( \
                      category)>(category),                                    \
                  ::callback::internal::CheckConstantCString<__builtin_strlen( \
                      name)>(name),                                            \
                  name "_callback", ##args)                                    \
            : ::callback::internal::TraceCallback(                             \
                  std::move(trace_local_cb__)));                               \
  }())

#endif  // LIB_CALLBACK_TRACE_CALLBACK_H_

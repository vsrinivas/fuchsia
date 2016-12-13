// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CALLBACK_TRACE_CALLBACK_H_
#define APPS_LEDGER_SRC_CALLBACK_TRACE_CALLBACK_H_

#include <functional>
#include <utility>

#include "apps/tracing/lib/trace/event.h"
#include "lib/ftl/functional/make_copyable.h"

namespace callback {

namespace internal {

template <typename C, typename... TraceArgType>
class TracingLambda {
 public:
  TracingLambda(C callback,
                const char* category,
                const char* name,
                TraceArgType... args)
      : id_(reinterpret_cast<uintptr_t>(this)),
        category_(category),
        name_(name),
        callback_(std::move(callback)),
        did_run_(false) {
    TRACE_ASYNC_BEGIN(category_, name_, id_,
                      std::forward<TraceArgType>(args)...);
  }

  TracingLambda(TracingLambda&& other)
      : id_(other.id_),
        category_(other.category_),
        name_(other.name_),
        callback_(std::move(other.callback_)),
        did_run_(other.did_run_) {
    other.did_run_ = true;
  }

  ~TracingLambda() {
    if (!did_run_) {
      TRACE_ASYNC_END(category_, name_, id_, "NotRun", true);
    }
  }

  template <typename... ArgType>
  auto operator()(ArgType&&... args) {
    did_run_ = true;
    auto id = id_;
    auto category = category_;
    auto name = name_;
    callback_(std::forward<ArgType>(args)...);
    TRACE_ASYNC_END(category, name, id);
  }

 private:
  const uintptr_t id_;
  const char* const category_;
  const char* const name_;
  const C callback_;
  bool did_run_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TracingLambda);
};

template <typename F>
struct ToStdFunction;

template <typename Ret, typename Class, typename... Args>
struct ToStdFunction<Ret (Class::*)(Args...) const> {
  using type = std::function<Ret(Args...)>;
};

template <typename C, typename... ArgType>
auto TraceCallback(C callback,
                   const char* category,
                   const char* name,
                   ArgType... args) {
  // Ensures the returned type of TraceCallback is the same whether or not
  // tracing is enabled.
  return
      typename ToStdFunction<decltype(&C::operator())>::type(ftl::MakeCopyable(
          TracingLambda<C, ArgType...>(std::move(callback), category, name,
                                       std::forward<ArgType>(args)...)));
}

template <typename C, typename... ArgType>
auto TraceCallback(C callback) {
  // Ensures the returned type of TraceCallback is the same whether or not
  // tracing is enabled.
  return typename ToStdFunction<decltype(&C::operator())>::type(
      ftl::MakeCopyable(std::move(callback)));
}

}  // namespace internal
}  // namespace callback

#define TRACE_CALLBACK(cb, category, name, args...)                      \
  (TRACE_ENABLED()                                                       \
       ? ::callback::internal::TraceCallback(cb, category, name, ##args) \
       : ::callback::internal::TraceCallback(cb))

#endif  // APPS_LEDGER_SRC_CALLBACK_TRACE_CALLBACK_H_

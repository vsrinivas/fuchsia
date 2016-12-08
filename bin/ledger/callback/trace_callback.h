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

template <typename... T>
void TraceAsyncBegin(const char* category,
                     const char* name,
                     uint64_t id,
                     T... args);

template <>
inline void TraceAsyncBegin(const char* category,
                            const char* name,
                            uint64_t id) {
  TRACE_ASYNC_BEGIN0(category, name, id);
}

template <typename K1, typename V1>
inline void TraceAsyncBegin(const char* category,
                            const char* name,
                            uint64_t id,
                            K1&& k1,
                            V1&& v1) {
  TRACE_ASYNC_BEGIN1(category, name, id, std::forward<K1>(k1),
                     std::forward<V1>(v1));
}

template <typename K1, typename V1, typename K2, typename V2>
inline void TraceAsyncBegin(const char* category,
                            const char* name,
                            uint64_t id,
                            K1&& k1,
                            V1&& v1,
                            K2&& k2,
                            V2&& v2) {
  TRACE_ASYNC_BEGIN2(category, name, id, std::forward<K1>(k1),
                     std::forward<V1>(v1), std::forward<K2>(k2),
                     std::forward<V2>(v2));
}

template <typename K1,
          typename V1,
          typename K2,
          typename V2,
          typename K3,
          typename V3>
inline void TraceAsyncBegin(const char* category,
                            const char* name,
                            uint64_t id,
                            K1&& k1,
                            V1&& v1,
                            K2&& k2,
                            V2&& v2,
                            K3&& k3,
                            V3&& v3) {
  TRACE_ASYNC_BEGIN3(category, name, id, std::forward<K1>(k1),
                     std::forward<V1>(v1), std::forward<K2>(k2),
                     std::forward<V2>(v2), std::forward<K3>(k3),
                     std::forward<V3>(v3));
}

template <typename K1,
          typename V1,
          typename K2,
          typename V2,
          typename K3,
          typename V3,
          typename K4,
          typename V4>
inline void TraceAsyncBegin(const char* category,
                            const char* name,
                            uint64_t id,
                            K1&& k1,
                            V1&& v1,
                            K2&& k2,
                            V2&& v2,
                            K3&& k3,
                            V3&& v3,
                            K4&& k4,
                            V4&& v4) {
  TRACE_ASYNC_BEGIN4(
      category, name, id, std::forward<K1>(k1), std::forward<V1>(v1),
      std::forward<K2>(k2), std::forward<V2>(v2), std::forward<K3>(k3),
      std::forward<V3>(v3), std::forward<K4>(k4), std::forward<V4>(v4));
}

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
    TraceAsyncBegin(category_, name_, id_, std::forward<TraceArgType>(args)...);
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
      TRACE_ASYNC_END1(category_, name_, id_, "NotRun", true);
    }
  }

  template <typename... ArgType>
  auto operator()(ArgType&&... args) {
    did_run_ = true;
    auto id = id_;
    auto category = category_;
    auto name = name_;
    callback_(std::forward<ArgType>(args)...);
    TRACE_ASYNC_END0(category, name, id);
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

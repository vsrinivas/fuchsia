// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CALLBACK_TRACE_CALLBACK_H_
#define APPS_LEDGER_SRC_CALLBACK_TRACE_CALLBACK_H_

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
void TraceAsyncBegin(const char* category, const char* name, uint64_t id) {
  TRACE_ASYNC_BEGIN0(category, name, id);
}

template <typename K1, typename V1>
void TraceAsyncBegin(const char* category,
                     const char* name,
                     uint64_t id,
                     K1&& k1,
                     V1&& v1) {
  TRACE_ASYNC_BEGIN1(category, name, id, std::forward(k1), std::forward(v1));
}

template <typename K1, typename V1, typename K2, typename V2>
void TraceAsyncBegin(const char* category,
                     const char* name,
                     uint64_t id,
                     K1&& k1,
                     V1&& v1,
                     K2&& k2,
                     V2&& v2) {
  TRACE_ASYNC_BEGIN2(category, name, id, std::forward(k1), std::forward(v1),
                     std::forward(k2), std::forward(v2));
}

template <typename K1,
          typename V1,
          typename K2,
          typename V2,
          typename K3,
          typename V3>
void TraceAsyncBegin(const char* category,
                     const char* name,
                     uint64_t id,
                     K1&& k1,
                     V1&& v1,
                     K2&& k2,
                     V2&& v2,
                     K3&& k3,
                     V3&& v3) {
  TRACE_ASYNC_BEGIN3(category, name, id, std::forward(k1), std::forward(v1),
                     std::forward(k2), std::forward(v2), std::forward(k3),
                     std::forward(v3));
}

template <typename K1,
          typename V1,
          typename K2,
          typename V2,
          typename K3,
          typename V3,
          typename K4,
          typename V4>
void TraceAsyncBegin(const char* category,
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
  TRACE_ASYNC_BEGIN4(category, name, id, std::forward(k1), std::forward(v1),
                     std::forward(k2), std::forward(v2), std::forward(k3),
                     std::forward(v3), std::forward(k4), std::forward(v4));
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
        callback_(std::move(callback)) {
    TraceAsyncBegin(category_, name_, id_, std::forward<TraceArgType>(args)...);
  }

  TracingLambda(TracingLambda&&) = default;
  TracingLambda& operator=(TracingLambda&&) = default;

  template <typename... ArgType>
  auto operator()(ArgType&&... args) const {
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

  FTL_DISALLOW_COPY_AND_ASSIGN(TracingLambda);
};

}  // namespace internal

template <typename C, typename... ArgType>
auto TraceCallback(C callback,
                   const char* category,
                   const char* name,
                   ArgType... args) {
  return ftl::MakeCopyable(internal::TracingLambda<C, ArgType...>(
      std::move(callback), category, name, std::forward<ArgType>(args)...));
}

}  // namespace callback

#endif  // APPS_LEDGER_SRC_CALLBACK_TRACE_CALLBACK_H_

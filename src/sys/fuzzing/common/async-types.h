// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_ASYNC_TYPES_H_
#define SRC_SYS_FUZZING_COMMON_ASYNC_TYPES_H_

// This file contains aliases for commonly used types from fpromise in order to reduce the "clutter"
// of having fully-qualified types everywhere, e.g.
//
//   fpromise::promise<V, E> Foo() {
//     return fpromise::make_promise([](fpromise::context& context) -> fpromise::result<V, E> {
//       ...
//     });
//   }
//
// becomes
//
//   Promise<V, E> Foo() {
//     return fpromise::make_promise([] (Context& context) -> Result<V, E> { ... });
//   }

#include <lib/async/cpp/executor.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/promise.h>
#include <lib/fpromise/result.h>
#include <lib/fpromise/scope.h>
#include <zircon/types.h>

namespace fuzzing {

// All tasks should be scheduled on a common executor.
using ExecutorPtr = std::shared_ptr<async::Executor>;
inline ExecutorPtr MakeExecutor(async_dispatcher_t* dispatcher) {
  return std::make_shared<async::Executor>(dispatcher);
}

// Generic futures, promises, and results.

template <typename V = void, typename E = void>
using Future = fpromise::future<V, E>;

template <typename V = void, typename E = void>
using Promise = fpromise::promise<V, E>;

template <typename V = void, typename E = void>
using Result = fpromise::result<V, E>;

template <typename V = void, typename E = void>
using Bridge = fpromise::bridge<V, E>;

// Futures, promises, and results that report errors using |zx_status_t|.

template <typename V = void>
using ZxFuture = fpromise::future<V, zx_status_t>;

template <typename V = void>
using ZxPromise = fpromise::promise<V, zx_status_t>;

template <typename V = void>
using ZxResult = fpromise::result<V, zx_status_t>;

template <typename V = void>
using ZxBridge = fpromise::bridge<V, zx_status_t>;

// Like |fpromise::completer::bind|, but can handle |zx_status_t| errors. This is useful for
// bridging FIDL callbacks for methods like "... -> ... error zx.status;".
template <typename V = void>
inline fit::function<void(ZxResult<V>)> ZxBind(typename ZxBridge<V>::completer_type&& completer) {
  return [completer = std::move(completer)](ZxResult<V> result) mutable {
    if (result.is_error()) {
      completer.complete_error(result.error());
      return;
    }
    if constexpr (std::is_same<V, void>::value) {
      completer.complete_ok();
    } else {
      completer.complete_ok(result.take_value());
    }
  };
}

// Supporting types.

using Scope = fpromise::scope;

using Context = fpromise::context;

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_ASYNC_TYPES_H_

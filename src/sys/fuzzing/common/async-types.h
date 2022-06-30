// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_ASYNC_TYPES_H_
#define SRC_SYS_FUZZING_COMMON_ASYNC_TYPES_H_

// This file contains aliases and helper functions for commonly used types from fpromise in order to
// reduce the "clutter" of having fully-qualified types everywhere.
//
// For example, a promise that creates some value before making a FIDL call might go from looking
// like this:
//
//   return fpromise::make_promise([](fpromise::context& context) ->
//     fpromise::result<V, zx_status_t> {
//     return fpromise::ok(CreateValue());
//   })
//   .and_then([](V& value){
//     fpromise::bridge<V, zx_status_t> bridge;
//     MakeFidlCall(value, [completer = std::move(bridge.completer)]
//       (fpromise::result<U, zx_status_t>& result) {
//       if (result.is_error()) {
//         completer.complete_error(result.error());
//         return;
//       }
//       completer.complete_ok(result.take_value());
//     });
//     return consumer.promise_or(fpromise::error(ZX_ERR_CANCELED));
//   });
//
// to this:
//
//   return fpromise::make_promise([](Context& context) -> ZxResult<V> {
//     return fpromise::ok(CreateValue());
//   })
//   .and_then([](V& value){
//     ZxBridge<V> bridge;
//     MakeFidlCall(value, ZxBind(std::move(bridge.completer)));
//     return consumer.promise_or(fpromise::error(ZX_ERR_CANCELED));
//   });
//

#include <lib/async/cpp/executor.h>
#include <lib/fpromise/barrier.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/promise.h>
#include <lib/fpromise/result.h>
#include <lib/fpromise/scope.h>
#include <lib/fpromise/sequencer.h>
#include <zircon/types.h>

namespace fuzzing {

// All tasks should be scheduled on a common executor.
using ExecutorPtr = std::shared_ptr<async::Executor>;
inline ExecutorPtr MakeExecutor(async_dispatcher_t* dispatcher) {
  return std::make_shared<async::Executor>(dispatcher);
}

// Types templated on values and errors, for both generic errors and zx_status_t.
//
// This will generate both an |Aliased| and |ZxAliased| type for the given |fpromise::original|,
// e.g. WRAP_RESULT_TYPE(fpromise::result, Result) produces:
//
//   * |Result<V, E>| which aliases |fpromise::result<V, E>|, and
//   * |ZxResult<V, E>| which aliases |fpromise::result<V, zx_status_t>|.
//
#define WRAP_RESULT_TYPE(original, Aliased)       \
  template <typename V = void, typename E = void> \
  using Aliased = original<V, E>;                 \
  template <typename V = void>                    \
  using Zx##Aliased = original<V, zx_status_t>

WRAP_RESULT_TYPE(fpromise::result, Result);
WRAP_RESULT_TYPE(fpromise::promise, Promise);
WRAP_RESULT_TYPE(fpromise::future, Future);
WRAP_RESULT_TYPE(fpromise::bridge, Bridge);
WRAP_RESULT_TYPE(fpromise::completer, Completer);
WRAP_RESULT_TYPE(fpromise::consumer, Consumer);

#undef WRAP_RESULT_TYPE

// Like |Completer::bind|, but can handle |zx_status_t| errors. This is useful for bridging FIDL
// callbacks for methods like "... -> ... error zx.status;".
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

// Converts a status code result to a |ZxResult|.
inline ZxResult<> AsZxResult(zx_status_t status) {
  if (status != ZX_OK) {
    return fpromise::error(status);
  }
  return fpromise::ok();
}

inline ZxResult<> AsZxResult(const Result<zx_status_t>& result) {
  if (result.is_error()) {
    return fpromise::error(ZX_ERR_INTERNAL);
  }
  return AsZxResult(result.value());
}

// Additional supporting types from fpromise.

using Barrier = fpromise::barrier;
using Context = fpromise::context;
using Scope = fpromise::scope;
using Sequencer = fpromise::sequencer;

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_ASYNC_TYPES_H_

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_COROUTINE_COROUTINE_WAITER_H_
#define SRC_LEDGER_LIB_COROUTINE_COROUTINE_WAITER_H_

#include <lib/fit/defer.h>

#include <utility>

#include "src/ledger/lib/coroutine/coroutine.h"
#include "third_party/abseil-cpp/absl/base/attributes.h"

// Utilities to interact with coroutines and callback::Waiter.

namespace coroutine {

// Waits on a callback::Waiter (and other waiter utilities). This method interrupts the coroutine
// until the finalizer of the waiter is executed. The results of the waiter are stored in
// |parameters|. If |Wait| returns |INTERRUPTED|, the coroutine must unwind its stack and terminate.
// Cancels the waiter when Wait terminates: callbacks scoped to the waiter may safely use the
// coroutine's stack.

template <typename A, typename... Args>
ABSL_MUST_USE_RESULT ContinuationStatus Wait(coroutine::CoroutineHandler* handler, A waiter,
                                             Args... parameters) {
  auto cleanup = fit::defer([&waiter] { waiter->Cancel(); });
  return coroutine::SyncCall(
      handler, [&waiter](auto callback) { waiter->Finalize(std::move(callback)); },
      std::forward<Args>(parameters)...);
}

}  // namespace coroutine

#endif  // SRC_LEDGER_LIB_COROUTINE_COROUTINE_WAITER_H_

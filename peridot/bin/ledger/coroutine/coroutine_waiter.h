// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_COROUTINE_COROUTINE_WAITER_H_
#define PERIDOT_BIN_LEDGER_COROUTINE_COROUTINE_WAITER_H_

#include <utility>

#include <lib/fxl/compiler_specific.h>

#include "peridot/bin/ledger/coroutine/coroutine.h"

// Utilities to interact with coroutines and callback::Waiter.

namespace coroutine {

// Wait on a callback::Waiter (and other waiter utilities). This method will
// interrupt the coroutine until the finalizer of the waiter is executed. The
// results of the waiter will be stored in |parameters|. If |Wait| returns
// |INTERRUPTED|, the coroutine must unwind its stack and terminate.

template <typename A, typename... Args>
FXL_WARN_UNUSED_RESULT ContinuationStatus
Wait(coroutine::CoroutineHandler* handler, A waiter, Args... parameters) {
  return coroutine::SyncCall(handler,
                             [waiter = std::move(waiter)](auto callback) {
                               waiter->Finalize(std::move(callback));
                             },
                             std::forward<Args>(parameters)...);
}

}  // namespace coroutine

#endif  // PERIDOT_BIN_LEDGER_COROUTINE_COROUTINE_WAITER_H_

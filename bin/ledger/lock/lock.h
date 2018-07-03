// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_LOCK_LOCK_H_
#define PERIDOT_BIN_LEDGER_LOCK_LOCK_H_

#include <memory>

#include <lib/callback/operation_serializer.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/memory/weak_ptr.h>

#include "peridot/bin/ledger/coroutine/coroutine.h"

namespace lock {
// A lock. As long as this object lives, OperationSerializer blocks all other
// operations.
class Lock {
 public:
  virtual ~Lock(){};
};

// Creates and acquires a lock.
// |handler| and |serializer| are inputs, |lock| is the output.
// Returns OK if the lock is acquired (meaning the coroutine is now running as
// a serialized operation of |serializer|), and INTERRUPTED if the coroutine
// stack must be unwound immediately (see coroutine::SyncCall for this case).
FXL_WARN_UNUSED_RESULT coroutine::ContinuationStatus AcquireLock(
    coroutine::CoroutineHandler* handler,
    callback::OperationSerializer* serializer, std::unique_ptr<Lock>* lock);

}  // namespace lock

#endif  // PERIDOT_BIN_LEDGER_LOCK_LOCK_H_

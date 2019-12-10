// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_SYNCHRONIZATION_LOCK_H_
#define SRC_LEDGER_BIN_SYNCHRONIZATION_LOCK_H_

#include <memory>

#include "src/ledger/lib/callback/operation_serializer.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "third_party/abseil-cpp/absl/base/attributes.h"

namespace lock {
// A lock. As long as this object lives, OperationSerializer blocks all other
// operations.
class Lock {
 public:
  virtual ~Lock() = default;
};

// Creates and acquires a lock.
// |handler| and |serializer| are inputs, |lock| is the output.
// Returns OK if the lock is acquired (meaning the coroutine is now running as
// a serialized operation of |serializer|), and INTERRUPTED if the coroutine
// stack must be unwound immediately (see coroutine::SyncCall for this case).
ABSL_MUST_USE_RESULT coroutine::ContinuationStatus AcquireLock(
    coroutine::CoroutineHandler* handler, ledger::OperationSerializer* serializer,
    std::unique_ptr<Lock>* lock);

}  // namespace lock

#endif  // SRC_LEDGER_BIN_SYNCHRONIZATION_LOCK_H_

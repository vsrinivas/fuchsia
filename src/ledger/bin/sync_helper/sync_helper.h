// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_SYNC_HELPER_SYNC_HELPER_H_
#define SRC_LEDGER_BIN_SYNC_HELPER_SYNC_HELPER_H_

#include <lib/fit/defer.h>
#include <lib/fit/function.h>

#include <map>
#include <utility>

#include "src/ledger/bin/sync_helper/mutable.h"
#include "src/ledger/lib/callback/scoped_callback.h"
#include "src/ledger/lib/memory/weak_ptr.h"

namespace ledger {

// This class allows to register operations and synchronization callback.
// Operation are registered by wrapping the callback that they are expected to
// call when they are finished.
// A synchronization callback is an callback that takes no parameter and that
// will be called by this class when all operations registered before the
// synchronization callback have finished.
class SyncHelper {
 public:
  SyncHelper();
  SyncHelper(const SyncHelper&) = delete;
  SyncHelper& operator=(const SyncHelper&) = delete;

  // Sets the callback to be called every time the SyncHelper is discardable.
  // SyncHelper is discardable when no operation is currently in progress.
  void SetOnDiscardable(fit::closure on_discardable);

  // Returns whether there is currently no running operation.
  bool IsDiscardable() const;

  // Registers a synchronization callback. |callback| will be called when all
  // operation wrapped by |WrapOperation| before the call to
  // |RegisterSynchronizationCallback| have finished.
  void RegisterSynchronizationCallback(fit::function<void()> callback);

  // Wraps |callback| and marks it as a live operation. No callback registered
  // through |RegisterSynchronizationCallback| after this call will be called
  // until the returned callback has been called at least once.
  template <typename A>
  auto WrapOperation(A callback) {
    auto sync_point = current_sync_point_;
    in_flight_operation_counts_per_sync_point_[sync_point]++;
    auto on_first_call = fit::defer(MakeScoped(weak_ptr_factory_.GetWeakPtr(), [this, sync_point] {
      if (--in_flight_operation_counts_per_sync_point_[sync_point] == 0) {
        CallSynchronizationCallbacks();
      }
    }));
    // The lambda is not marked mutable, because the original callback might
    // have a const operator, and this should not force the receiver to use
    // only non-const operator.
    // Because of this:
    // - on_first_callcallback| must be wrap into a Mutable because calling it
    //   is not a const operation.
    // - |callback| must be wrap into a Mutable because it might not have a
    //   const operator().
    return [callback = Mutable(std::move(callback)),
            on_first_call = Mutable(std::move(on_first_call))](auto&&... params) {
      (*callback)(std::forward<decltype(params)>(params)...);
      on_first_call->call();
    };
  }

 private:
  // Calls all synchronization callbacks that are currently due.
  void CallSynchronizationCallbacks();

  // This class operates with a virtual timestamp.
  // - Each time an operation is registered, it increases the number of
  //   operation at the current timestamp.
  // - Each time a synchronization callback is registered, it is either
  //   immediately called if no operation is in progress, or it is associated
  //   with the current timestamp, and after this, the current timestamp is
  //   incremented.
  // - Each time an operation terminates, it decrements the number of operations
  //   at the current timestamp. Then the algorithm looks at all timestamp in
  //   increasing order. Until it finds one for which there is still operation
  //   in progress, it calls the associated synchronization callback.

  // The current timestamp.
  int64_t current_sync_point_;
  // The synchronization callbacks associated to their respective timestamp.
  std::map<int64_t, fit::function<void()>> sync_callback_per_sync_points_;
  // The number of operation in progress for each timestamp.
  std::map<int64_t, int64_t> in_flight_operation_counts_per_sync_point_;

  fit::closure on_discardable_;

  // This must be the last member.
  WeakPtrFactory<SyncHelper> weak_ptr_factory_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_SYNC_HELPER_SYNC_HELPER_H_

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYNC_CPP_COMPLETION_H_
#define LIB_SYNC_CPP_COMPLETION_H_

#include <lib/sync/completion.h>
#include <lib/zx/time.h>

namespace sync {

/// C++ wrapper for a completion object, |sync_completion_t|. A |Completion| is
/// a synchronization primitive that allows for one or more threads to wait for
/// a signal from another thread.
///
/// See <lib/sync/completion.h> for full details.
///
/// This class is thread-safe.
class Completion {
 public:
  /// Returns ZX_OK if woken by a call to |Signal| or if the completion has
  /// already been signaled. Otherwise, waits forever.
  zx_status_t Wait() { return Wait(zx::time::infinite()); }

  /// Returns ZX_ERR_TIMED_OUT if |timeout| elapses, and ZX_OK if woken by a
  /// call to |Signal| or if the completion has already been signaled.
  zx_status_t Wait(zx::duration timeout) {
    return sync_completion_wait(&completion_, timeout.get());
  }

  /// Returns ZX_ERR_TIMED_OUT if |deadline| elapses, and ZX_OK if woken by a
  /// call to |Signal| or if the completion has already been signaled.
  zx_status_t Wait(zx::time deadline) {
    return sync_completion_wait_deadline(&completion_, deadline.get());
  }

  /// Awakens all waiters on the completion, and marks it as signaled. Waits
  /// after this call but before a reset of the completion will also see the
  /// signal and immediately return.
  void Signal() { sync_completion_signal(&completion_); }

  /// Resets the completion object's signaled state to unsignaled.
  void Reset() { sync_completion_reset(&completion_); }

  /// Returns true iff the completion object has been signaled.
  bool signaled() const { return sync_completion_signaled(&completion_); }

  /// Gets the underlying |sync_completion_t|.
  sync_completion_t* get() { return &completion_; }

  /// Gets the underlying |sync_completion_t|.
  const sync_completion_t* get() const { return &completion_; }

 private:
  sync_completion_t completion_;
};

}  // namespace sync

#endif  // LIB_SYNC_CPP_COMPLETION_H_

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_RUNTIME_DISPATCHER_H_
#define SRC_DEVICES_BIN_DRIVER_RUNTIME_DISPATCHER_H_

#include <lib/fdf/dispatcher.h>

#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>

#include "src/devices/bin/driver_runtime/callback_request.h"

// This is currently a bare-bones implementation for testing.

namespace driver_runtime {

struct Dispatcher {
 public:
  // Public for std::make_unique.
  // Use |Create| instead of calling directly.
  // |use_async_loop| is only specified by test instances.
  Dispatcher(bool unsynchronized, bool allow_sync_calls, bool use_async_loop = true)
      : unsynchronized_(unsynchronized),
        allow_sync_calls_(allow_sync_calls),
        use_async_loop_(use_async_loop) {}

  // |use_async_loop| is set to false for some unit tests that don't
  // want to deal with different threads.
  static fdf_status_t Create(uint32_t options, const char* scheduler_role,
                             size_t scheduler_role_len, bool use_async_loop,
                             std::unique_ptr<Dispatcher>* out_dispatcher);

  // Queue a callback to be invoked by the dispatcher.
  // Depending on dispatcher options, this can occur on the current thread
  // or be queued up to run on a dispatcher thread.
  void QueueCallback(std::unique_ptr<CallbackRequest> callback_request);

  std::unique_ptr<CallbackRequest> CancelCallback(CallbackRequest& callback_request);

  bool unsynchronized() const { return unsynchronized_; }
  bool allow_sync_calls() const { return allow_sync_calls_; }

  // For testing only.
  size_t callback_queue_size_slow() {
    fbl::AutoLock lock(&callback_lock_);
    return callback_queue_.size_slow();
  }

 private:
  // Dispatcher options
  bool unsynchronized_;
  bool allow_sync_calls_;

  // Set to false for testing only.
  bool use_async_loop_;

  fbl::Mutex callback_lock_;
  fbl::DoublyLinkedList<std::unique_ptr<CallbackRequest>> callback_queue_
      __TA_GUARDED(&callback_lock_);
};

}  // namespace driver_runtime

struct fdf_dispatcher : public driver_runtime::Dispatcher {
  // NOTE: Intentionally empty, do not add to this.
};

#endif  //  SRC_DEVICES_BIN_DRIVER_RUNTIME_DISPATCHER_H_

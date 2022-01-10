// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_RUNTIME_DISPATCHER_H_
#define SRC_DEVICES_BIN_DRIVER_RUNTIME_DISPATCHER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>

#include <fbl/auto_lock.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>

#include "src/devices/bin/driver_runtime/async_loop_owned_event_handler.h"
#include "src/devices/bin/driver_runtime/callback_request.h"

// Loop shared across all dispatchers in a process.
class ProcessSharedLoop {
 public:
  // We default to one thread, and start additional threads when blocking dispatchers are created.
  ProcessSharedLoop() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) { loop_.StartThread(); }

  async::Loop* loop() { return &loop_; }

 private:
  async::Loop loop_;
};

namespace driver_runtime {

class Dispatcher : public async_dispatcher_t {
 public:
  // Public for std::make_unique.
  // Use |Create| or |CreateWithLoop| instead of calling directly.
  Dispatcher(uint32_t options, bool unsynchronized, bool allow_sync_calls, const void* owner,
             async_dispatcher_t* process_shared_dispatcher);

  // Creates a dispatcher which is backed by |loop|.
  // |loop| can be the |ProcessSharedLoop|, or a private async loop created by a test.
  static fdf_status_t CreateWithLoop(uint32_t options, const char* scheduler_role,
                                     size_t scheduler_role_len, const void* owner,
                                     async::Loop* loop,
                                     std::unique_ptr<Dispatcher>* out_dispatcher);

  // fdf_dispatcher_t implementation
  static fdf_status_t Create(uint32_t options, const char* scheduler_role,
                             size_t scheduler_role_len,
                             std::unique_ptr<Dispatcher>* out_dispatcher);

  // |dispatcher| must have been retrieved via `GetAsyncDispatcher`.
  static Dispatcher* FromAsyncDispatcher(async_dispatcher_t* dispatcher);
  async_dispatcher_t* GetAsyncDispatcher();
  void Destroy();

  // async_dispatcher_t implementation
  zx_time_t GetTime();
  zx_status_t BeginWait(async_wait_t* wait);
  zx_status_t CancelWait(async_wait_t* wait);
  zx_status_t PostTask(async_task_t* task);
  zx_status_t CancelTask(async_task_t* task);
  zx_status_t QueuePacket(async_receiver_t* receiver, const zx_packet_user_t* data);
  zx_status_t BindIrq(async_irq_t* irq);
  zx_status_t UnbindIrq(async_irq_t* irq);

  // Queue a callback to be invoked by the dispatcher.
  // Depending on the dispatcher options set and which driver is calling this,
  // the callback can occur on the current thread or be queued up to run on a dispatcher thread.
  void QueueCallback(std::unique_ptr<CallbackRequest> callback_request);

  // Removes the callback matching |callback_request| from the queue and returns it.
  // May return nullptr if no such callback is found.
  std::unique_ptr<CallbackRequest> CancelCallback(CallbackRequest& callback_request);

  // Returns the dispatcher options specified by the user.
  uint32_t options() const { return options_; }
  bool unsynchronized() const { return unsynchronized_; }
  bool allow_sync_calls() const { return allow_sync_calls_; }

  // Returns the driver which owns this dispatcher.
  const void* owner() const { return owner_; }

  // For use by testing only.
  size_t callback_queue_size_slow() {
    fbl::AutoLock lock(&callback_lock_);
    return callback_queue_.size_slow();
  }

 private:
  // TODO(fxbug.dev/87834): determine an appropriate size.
  static constexpr uint32_t kBatchSize = 10;

  class EventWaiter : public AsyncLoopOwnedEventHandler<EventWaiter> {
    using Callback = fit::inline_function<void(std::unique_ptr<EventWaiter>), sizeof(Dispatcher*)>;

   public:
    EventWaiter(zx::event event, Callback callback)
        : AsyncLoopOwnedEventHandler<EventWaiter>(std::move(event)),
          callback_(std::move(callback)) {}

    static void HandleEvent(std::unique_ptr<EventWaiter> event, async_dispatcher_t* dispatcher,
                            async::WaitBase* wait, zx_status_t status,
                            const zx_packet_signal_t* signal);

    bool signaled() const { return signaled_; }

    void signal() {
      ZX_ASSERT(event()->signal(0, ZX_USER_SIGNAL_0) == ZX_OK);
      signaled_ = true;
    }

    void designal() {
      ZX_ASSERT(event()->signal(ZX_USER_SIGNAL_0, 0) == ZX_OK);
      signaled_ = false;
    }

    void InvokeCallback(std::unique_ptr<EventWaiter> event_waiter) {
      callback_(std::move(event_waiter));
    }

   private:
    bool signaled_ = false;
    Callback callback_;
  };

  // Calls |callback_request|.
  void DispatchCallback(std::unique_ptr<driver_runtime::CallbackRequest> callback_request);
  // Calls the callbacks in |callback_queue_|.
  void DispatchCallbacks(std::unique_ptr<EventWaiter> event_waiter);

  // Dispatcher options set by the user.
  uint32_t options_;
  bool unsynchronized_;
  bool allow_sync_calls_;

  // The driver which owns this dispatcher. May be nullptr if undeterminable.
  const void* const owner_;

  // Global dispatcher shared across all dispatchers in a process.
  async_dispatcher_t* process_shared_dispatcher_;
  EventWaiter* event_waiter_;

  fbl::Mutex callback_lock_;
  // Callback requests from channels.
  fbl::DoublyLinkedList<std::unique_ptr<CallbackRequest>> callback_queue_
      __TA_GUARDED(&callback_lock_);

  // True if currently dispatching a message.
  // This is only relevant in the synchronized mode.
  bool dispatching_sync_ __TA_GUARDED(&callback_lock_) = false;

  fbl::Canary<fbl::magic("FDFD")> canary_;
};

}  // namespace driver_runtime

struct fdf_dispatcher : public driver_runtime::Dispatcher {
  // NOTE: Intentionally empty, do not add to this.
};

#endif  //  SRC_DEVICES_BIN_DRIVER_RUNTIME_DISPATCHER_H_

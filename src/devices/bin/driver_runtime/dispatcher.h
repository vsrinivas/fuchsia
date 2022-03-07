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
#include <lib/zx/status.h>

#include <vector>

#include <fbl/auto_lock.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>

#include "src/devices/bin/driver_runtime/async_loop_owned_event_handler.h"
#include "src/devices/bin/driver_runtime/callback_request.h"
#include "src/devices/bin/driver_runtime/driver_context.h"

namespace driver_runtime {

class Dispatcher : public async_dispatcher_t,
                   public fbl::RefCounted<Dispatcher>,
                   public fbl::DoublyLinkedListable<fbl::RefPtr<Dispatcher>> {
 public:
  // Public for std::make_unique.
  // Use |Create| or |CreateWithLoop| instead of calling directly.
  Dispatcher(uint32_t options, bool unsynchronized, bool allow_sync_calls, const void* owner,
             async_dispatcher_t* process_shared_dispatcher,
             fdf_dispatcher_destructed_observer_t* observer);

  // Creates a dispatcher which is backed by |loop|.
  // |loop| can be the |ProcessSharedLoop|, or a private async loop created by a test.
  //
  // Returns ownership of the dispatcher in |out_dispatcher|. The caller should call
  // |Destroy| once they are done using the dispatcher. Once |Destroy| is called,
  // the dispatcher will be deleted once all callbacks canclled or completed by the dispatcher.
  static fdf_status_t CreateWithLoop(uint32_t options, const char* scheduler_role,
                                     size_t scheduler_role_len, const void* owner,
                                     async::Loop* loop, fdf_dispatcher_destructed_observer_t*,
                                     Dispatcher** out_dispatcher);

  // fdf_dispatcher_t implementation
  // Returns ownership of the dispatcher in |out_dispatcher|. The caller should call
  // |Destroy| once they are done using the dispatcher. Once |Destroy| is called,
  // the dispatcher will be deleted once all callbacks cancelled or completed by the dispatcher.
  static fdf_status_t Create(uint32_t options, const char* scheduler_role,
                             size_t scheduler_role_len, fdf_dispatcher_destructed_observer_t*,
                             Dispatcher** out_dispatcher);

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

  // Registers a callback with a dispatcher that should not yet be run.
  // This should be called by the channel if a client has started waiting with a
  // ChannelRead, but the channel has not yet received a write from its peer.
  //
  // Tracking these requests allows the dispatcher to cancel the callback if the
  // dispatcher is destroyed before any write is received.
  //
  // Takes ownership of |callback_request|. If the dispatcher is already shutting down,
  // ownership of |callback_request| will be returned to the caller.
  std::unique_ptr<driver_runtime::CallbackRequest> RegisterCallbackWithoutQueueing(
      std::unique_ptr<CallbackRequest> callback_request);

  // Queues a previously registered callback to be invoked by the dispatcher.
  // Asserts if no such callback is found.
  // |unowned_callback_request| is used to locate the callback.
  // |callback_reason| is the status that should be set for the callback.
  // Depending on the dispatcher options set and which driver is calling this,
  // the callback can occur on the current thread or be queued up to run on a dispatcher thread.
  void QueueRegisteredCallback(CallbackRequest* unowned_callback_request,
                               fdf_status_t callback_reason);

  // Removes the callback matching |callback_request| from the queue and returns it.
  // May return nullptr if no such callback is found.
  std::unique_ptr<CallbackRequest> CancelCallback(CallbackRequest& callback_request);

  // Sets the callback reason for a currently queued callback request.
  // This may fail if the callback is already running or scheduled to run.
  // Returns true if a callback matching |callback_request| was found, false otherwise.
  bool SetCallbackReason(CallbackRequest* callback_request, fdf_status_t callback_reason);

  // Removes the callback that manages the async dispatcher |operation| and returns it.
  // May return nullptr if no such callback is found.
  std::unique_ptr<CallbackRequest> CancelAsyncOperation(void* operation);

  // Returns true if the dispatcher has no active threads or queued requests.
  // This unlocked version of |IsIdleLocked| is called by tests.
  bool IsIdle() {
    fbl::AutoLock lock(&callback_lock_);
    return IsIdleLocked();
  }

  // Returns ownership of an event that will be signaled once the dispatcher is idle.
  zx::status<zx::event> RegisterForIdleEvent();

  // Blocks the current thread until the dispatcher is idle.
  fdf_status_t WaitUntilIdle();

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
    using Callback =
        fit::inline_function<void(std::unique_ptr<EventWaiter>, fbl::RefPtr<Dispatcher>),
                             sizeof(Dispatcher*)>;

   public:
    EventWaiter(zx::event event, Callback callback)
        : AsyncLoopOwnedEventHandler<EventWaiter>(std::move(event)),
          callback_(std::move(callback)) {}

    static void HandleEvent(std::unique_ptr<EventWaiter> event, async_dispatcher_t* dispatcher,
                            async::WaitBase* wait, zx_status_t status,
                            const zx_packet_signal_t* signal);

    // Begins waiting in the underlying async dispatcher on |event->wait|.
    // This transfers ownership of |event| and the |dispatcher| reference to the async dispatcher.
    // The async dispatcher returns ownership when the handler is invoked.
    static zx_status_t BeginWaitWithRef(std::unique_ptr<EventWaiter> event,
                                        fbl::RefPtr<Dispatcher> dispatcher);

    bool signaled() const { return signaled_; }

    void signal() {
      ZX_ASSERT(event()->signal(0, ZX_USER_SIGNAL_0) == ZX_OK);
      signaled_ = true;
    }

    void designal() {
      ZX_ASSERT(event()->signal(ZX_USER_SIGNAL_0, 0) == ZX_OK);
      signaled_ = false;
    }

    void InvokeCallback(std::unique_ptr<EventWaiter> event_waiter,
                        fbl::RefPtr<Dispatcher> dispatcher_ref) {
      callback_(std::move(event_waiter), std::move(dispatcher_ref));
    }

    std::unique_ptr<EventWaiter> Cancel() {
      // Cancelling may fail if the callback is happening right now, in which
      // case the callback will take ownership of the dispatcher reference.
      auto event = AsyncLoopOwnedEventHandler<EventWaiter>::Cancel();
      if (event) {
        event->dispatcher_ref_ = nullptr;
      }
      return event;
    }

   private:
    bool signaled_ = false;
    Callback callback_;

    // The EventWaiter is provided ownership of a dispatcher reference when
    // |BeginWaitWithRef| is called, and returns the reference with the callback.
    fbl::RefPtr<Dispatcher> dispatcher_ref_;
  };

  class IdleEventManager {
   public:
    // Returns a duplicate of the event that will be signaled when the dispatcher is next idle.
    zx::status<zx::event> GetIdleEvent();
    // Signal and reset the idle event.
    zx_status_t Signal();

   private:
    zx::event event_;
  };

  // Calls |callback_request|.
  void DispatchCallback(std::unique_ptr<driver_runtime::CallbackRequest> callback_request);
  // Calls the callbacks in |callback_queue_|.
  void DispatchCallbacks(std::unique_ptr<EventWaiter> event_waiter,
                         fbl::RefPtr<Dispatcher> dispatcher_ref);

  // Cancels the callbacks in |shutdown_queue_|, and drops the initial reference to the dispatcher.
  void CompleteDestroy();

  // Returns true if the dispatcher has no active threads or queued requests.
  bool IsIdleLocked() __TA_REQUIRES(&callback_lock_);

  // Checks whether the dispatcher has entered and idle state and if so notifies any registered
  // waiters.
  void IdleCheckLocked() __TA_REQUIRES(&callback_lock_);

  // Returns true if the current thread is managed by the driver runtime.
  bool IsRuntimeManagedThread() { return !driver_context::IsCallStackEmpty(); }

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
  // Callback requests that have been registered by channels, but not yet queued.
  // This occurs when a client has started waiting on a channel, but the channel
  // has not yet received a write from its peer.
  fbl::DoublyLinkedList<std::unique_ptr<CallbackRequest>> registered_callbacks_
      __TA_GUARDED(&callback_lock_);
  // Queued callback requests from channels. These are requests that should
  // be run on the next available thread.
  fbl::DoublyLinkedList<std::unique_ptr<CallbackRequest>> callback_queue_
      __TA_GUARDED(&callback_lock_);
  // Callback requests that have been removed to be completed by |CompleteDestroy|.
  // These are removed from the active queues to ensure the dispatcher does not
  // attempt to continue processing them.
  fbl::DoublyLinkedList<std::unique_ptr<CallbackRequest>> shutdown_queue_
      __TA_GUARDED(&callback_lock_);

  // True if currently dispatching a message.
  // This is only relevant in the synchronized mode.
  bool dispatching_sync_ __TA_GUARDED(&callback_lock_) = false;

  // True if |Destroy| has been called.
  bool shutting_down_ __TA_GUARDED(&callback_lock_) = false;

  // Number of threads currently servicing callbacks.
  size_t num_active_threads_ __TA_GUARDED(&callback_lock_) = 0;

  IdleEventManager idle_event_manager_ __TA_GUARDED(&callback_lock_);

  // The observer that should be called when destruction of the dispatcher completes.
  fdf_dispatcher_destructed_observer_t* destructed_observer_ = nullptr;

  fbl::Canary<fbl::magic("FDFD")> canary_;
};

// Coordinator for all dispatchers in a process.
class DispatcherCoordinator {
 public:
  // We default to one thread, and start additional threads when blocking dispatchers are created.
  DispatcherCoordinator() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) { loop_.StartThread(); }

  static fdf_status_t WaitUntilDispatchersIdle();

  void AddDispatcher(fbl::RefPtr<Dispatcher> dispatcher);
  void RemoveDispatcher(Dispatcher& dispatcher);

  async::Loop* loop() { return &loop_; }

 private:
  // Make sure this destructs after |loop_|. This is as dispatchers will remove themselves
  // from this list on shutdown.
  fbl::Mutex lock_;
  fbl::DoublyLinkedList<fbl::RefPtr<driver_runtime::Dispatcher>> dispatchers_ __TA_GUARDED(&lock_);

  async::Loop loop_;
};

}  // namespace driver_runtime

struct fdf_dispatcher : public driver_runtime::Dispatcher {
  // NOTE: Intentionally empty, do not add to this.
};

#endif  //  SRC_DEVICES_BIN_DRIVER_RUNTIME_DISPATCHER_H_

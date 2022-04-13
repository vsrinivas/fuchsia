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
#include <lib/sync/cpp/completion.h>
#include <lib/zx/status.h>
#include <lib/zx/time.h>
#include <zircon/compiler.h>

#include <vector>

#include <fbl/auto_lock.h>
#include <fbl/canary.h>
#include <fbl/condition_variable.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/ref_counted.h>

#include "fbl/intrusive_container_utils.h"
#include "src/devices/bin/driver_runtime/async_loop_owned_event_handler.h"
#include "src/devices/bin/driver_runtime/callback_request.h"
#include "src/devices/bin/driver_runtime/driver_context.h"

namespace driver_runtime {

class Dispatcher : public async_dispatcher_t,
                   public fbl::RefCounted<Dispatcher>,
                   public fbl::DoublyLinkedListable<fbl::RefPtr<Dispatcher>> {
  // Forward Declaration
  class AsyncWait;

 public:
  // Public for std::make_unique.
  // Use |Create| or |CreateWithLoop| instead of calling directly.
  Dispatcher(uint32_t options, bool unsynchronized, bool allow_sync_calls, const void* owner,
             async_dispatcher_t* process_shared_dispatcher,
             fdf_dispatcher_shutdown_observer_t* observer);

  // Creates a dispatcher which is backed by |loop|.
  // |loop| can be the |ProcessSharedLoop|, or a private async loop created by a test.
  //
  // Returns ownership of the dispatcher in |out_dispatcher|. The caller should call
  // |Destroy| once they are done using the dispatcher. Once |Destroy| is called,
  // the dispatcher will be deleted once all callbacks canclled or completed by the dispatcher.
  static fdf_status_t CreateWithLoop(uint32_t options, const char* scheduler_role,
                                     size_t scheduler_role_len, const void* owner,
                                     async::Loop* loop, fdf_dispatcher_shutdown_observer_t*,
                                     Dispatcher** out_dispatcher);

  // fdf_dispatcher_t implementation
  // Returns ownership of the dispatcher in |out_dispatcher|. The caller should call
  // |Destroy| once they are done using the dispatcher. Once |Destroy| is called,
  // the dispatcher will be deleted once all callbacks cancelled or completed by the dispatcher.
  static fdf_status_t Create(uint32_t options, const char* scheduler_role,
                             size_t scheduler_role_len, fdf_dispatcher_shutdown_observer_t*,
                             Dispatcher** out_dispatcher);

  // |dispatcher| must have been retrieved via `GetAsyncDispatcher`.
  static Dispatcher* FromAsyncDispatcher(async_dispatcher_t* dispatcher);
  async_dispatcher_t* GetAsyncDispatcher();
  void ShutdownAsync();
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

  bool HasQueuedTasks();

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

  // Adds wait to |waits_|.
  void AddWaitLocked(std::unique_ptr<AsyncWait> wait) __TA_REQUIRES(&callback_lock_);
  // Removes wait from |waits_| and triggers idle check.
  std::unique_ptr<AsyncWait> RemoveWait(AsyncWait* wait) __TA_EXCLUDES(&callback_lock_);
  std::unique_ptr<AsyncWait> RemoveWaitLocked(AsyncWait* wait) __TA_REQUIRES(&callback_lock_);
  // Moves wait from |waits_| queue onto |registered_callbacks_| and signals that it can be called.
  void QueueWait(AsyncWait* wait, zx_status_t status);

  // Removes the callback matching |callback_request| from the queue and returns it.
  // May return nullptr if no such callback is found.
  std::unique_ptr<CallbackRequest> CancelCallback(CallbackRequest& callback_request);

  // Sets the callback reason for a currently queued callback request.
  // This may fail if the callback is already running or scheduled to run.
  // Returns true if a callback matching |callback_request| was found, false otherwise.
  bool SetCallbackReason(CallbackRequest* callback_request, fdf_status_t callback_reason);

  // Removes the callback that manages the async dispatcher |operation| and returns it.
  // May return nullptr if no such callback is found.
  std::unique_ptr<CallbackRequest> CancelAsyncOperation(void* operation)
      __TA_EXCLUDES(&callback_lock_);

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
  enum class DispatcherState {
    // The dispatcher is running and accepting new requests.
    kRunning,
    // The dispatcher is in the process of shutting down.
    kShuttingDown,
    // The dispatcher has completed shutdown and can be destroyed.
    kShutdown,
    // The dispatcher is about to be destroyed.
    kDestroyed,
  };

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

  struct AsyncWaitTag {};

  // Indirect wait object which is used to ensure waits are tracked and synchronize waits on
  // SYNCHRONIZED dispatchers.
  class AsyncWait
      : public CallbackRequest,
        public async_wait_t,
        // This is owned by a Dispatcher, but in two different lists, however only one at a time. We
        // could avoid this by storing |waits_| as a CallbackRequest, however that would require
        // additional casts and pointer math when erasing the wait from the list.
        public fbl::ContainableBaseClasses<fbl::TaggedDoublyLinkedListable<
            std::unique_ptr<AsyncWait>, AsyncWaitTag, fbl::NodeOptions::AllowMultiContainerUptr>> {
   public:
    AsyncWait(async_wait_t* original_wait, Dispatcher& dispatcher);
    ~AsyncWait();

    static zx_status_t BeginWait(std::unique_ptr<AsyncWait> wait, Dispatcher& dispatcher)
        __TA_REQUIRES(&dispatcher.callback_lock_);

    bool Cancel();

    static void Handler(async_dispatcher_t* dispatcher, async_wait_t* wait, zx_status_t status,
                        const zx_packet_signal_t* signal);

    void SetupCallback(Dispatcher& dispatcher, const zx_packet_signal_t* signal);

    void OnSignal(async_dispatcher_t* async_dispatcher, zx_status_t status,
                  const zx_packet_signal_t* signal);

   private:
    // Implementing a specialization of std::atomic<fbl::RefPtr<T>> is more challenging than just
    // manipulating it as a raw pointer. It must be stored as an atomic because it is mutated from
    // multiple threads after AsyncWait is constructed, and we wish to avoid a lock.
    std::atomic<Dispatcher*> dispatcher_ref_;
    async_wait_t* original_wait_;

    // driver_runtime::Callback can store only 2 pointers, so we store other state in the async
    // wait.
    zx_packet_signal_t signal_packet_ = {};
  };

  // A task which will be triggered at some point in the future.
  struct DelayedTask : public CallbackRequest {
    DelayedTask(zx::time deadline)
        : CallbackRequest(CallbackRequest::RequestType::kTask), deadline(deadline) {}
    zx::time deadline;
  };

  // A timer primitive built on top of an async task.
  class Timer {
   public:
    Timer(Dispatcher* dispatcher) : dispatcher_(dispatcher) {}

    zx_status_t BeginWait(async_dispatcher_t* dispatcher, zx::time deadline) {
      ZX_ASSERT(is_armed() == false);
      zx_status_t status = task_.PostForTime(dispatcher, deadline);
      if (status == ZX_OK) {
        current_deadline_ = deadline;
      }
      return status;
    }

    bool is_armed() const { return current_deadline_ != zx::time::infinite(); }

    zx_status_t Cancel() {
      if (!is_armed()) {
        // Nothing to cancel.
        return ZX_OK;
      }
      zx_status_t status = task_.Cancel();
      // ZX_ERR_NOT_FOUND can happen here when a pending timer fires and
      // the packet is picked up by port_wait in another thread but has
      // not reached dispatch.
      ZX_ASSERT(status == ZX_OK || status == ZX_ERR_NOT_FOUND);
      if (status == ZX_OK) {
        current_deadline_ = zx::time::infinite();
      }
      return status;
    }

    zx::time current_deadline() const { return current_deadline_; }

   private:
    void Handler();

    async::TaskClosureMethod<Timer, &Timer::Handler> task_{this};
    // zx::time::infinite() means we are not scheduled.
    zx::time current_deadline_ = zx::time::infinite();
    Dispatcher* dispatcher_;
  };

  zx::time GetNextTimeoutLocked() const __TA_REQUIRES(&callback_lock_);
  void ResetTimerLocked() __TA_REQUIRES(&callback_lock_);
  void InsertDelayedTaskSortedLocked(std::unique_ptr<DelayedTask> task)
      __TA_REQUIRES(&callback_lock_);
  void CheckDelayedTasks() __TA_EXCLUDES(&callback_lock_);
  void CheckDelayedTasksLocked() __TA_REQUIRES(&callback_lock_);

  // Calls |callback_request|.
  void DispatchCallback(std::unique_ptr<driver_runtime::CallbackRequest> callback_request);
  // Calls the callbacks in |callback_queue_|.
  void DispatchCallbacks(std::unique_ptr<EventWaiter> event_waiter,
                         fbl::RefPtr<Dispatcher> dispatcher_ref);

  // Cancels the callbacks in |shutdown_queue_|.
  void CompleteShutdown();

  // Returns true if the dispatcher has no active threads or queued requests.
  bool IsIdleLocked() __TA_REQUIRES(&callback_lock_);

  // Checks whether the dispatcher has entered and idle state and if so notifies any registered
  // waiters.
  void IdleCheckLocked() __TA_REQUIRES(&callback_lock_);

  // Returns true if the current thread is managed by the driver runtime.
  bool IsRuntimeManagedThread() { return !driver_context::IsCallStackEmpty(); }

  // Returns whether the dispatcher is in the running state.
  bool IsRunningLocked() __TA_REQUIRES(&callback_lock_) {
    return state_ == DispatcherState::kRunning;
  }

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
  // Callback requests that have been removed to be completed by |CompleteShutdown|.
  // These are removed from the active queues to ensure the dispatcher does not
  // attempt to continue processing them.
  fbl::DoublyLinkedList<std::unique_ptr<CallbackRequest>> shutdown_queue_
      __TA_GUARDED(&callback_lock_);

  // Waits which are queued up against |process_shared_dispatcher|. These are moved onto the
  // |registered_callbacks_| queue once completed. They are tracked so that they may be canceled
  // during |Destroy| prior to calling |CompleteDestroy|.
  fbl::TaggedDoublyLinkedList<std::unique_ptr<AsyncWait>, AsyncWaitTag> waits_
      __TA_GUARDED(&callback_lock_);

  Timer timer_ __TA_GUARDED(&callback_lock_);

  // Tasks which should move into callback_queue as soon as they are ready.
  // Sorted by earliest deadline first.
  fbl::DoublyLinkedList<std::unique_ptr<CallbackRequest>> delayed_tasks_
      __TA_GUARDED(&callback_lock_);

  // True if currently dispatching a message.
  // This is only relevant in the synchronized mode.
  bool dispatching_sync_ __TA_GUARDED(&callback_lock_) = false;

  // TODO(fxbug.dev/97753): consider using std::atomic.
  DispatcherState state_ __TA_GUARDED(&callback_lock_) = DispatcherState::kRunning;

  // Number of threads currently servicing callbacks.
  size_t num_active_threads_ __TA_GUARDED(&callback_lock_) = 0;

  IdleEventManager idle_event_manager_ __TA_GUARDED(&callback_lock_);

  // The observer that should be called when shutting down the dispatcher completes.
  fdf_dispatcher_shutdown_observer_t* shutdown_observer_ __TA_GUARDED(&callback_lock_) = nullptr;

  fbl::Canary<fbl::magic("FDFD")> canary_;
};

// Coordinator for all dispatchers in a process.
class DispatcherCoordinator {
 public:
  // We default to one thread, and start additional threads when blocking dispatchers are created.
  DispatcherCoordinator() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) { loop_.StartThread(); }

  static void DestroyAllDispatchers();
  static fdf_status_t WaitUntilDispatchersIdle();
  static void WaitUntilDispatchersDestroyed();
  static fdf_status_t ShutdownDispatchersAsync(const void* driver,
                                               fdf_internal_driver_shutdown_observer_t* observer);

  // Returns ZX_OK if |dispatcher| was added successfully.
  // Returns ZX_ERR_BAD_STATE if the driver is currently shutting down.
  zx_status_t AddDispatcher(fbl::RefPtr<Dispatcher> dispatcher);
  // Records the dispatcher as being shutdown.
  void SetShutdown(driver_runtime::Dispatcher& dispatcher);
  // Notifies the dispatcher coordinator that a dispatcher has completed shutdown.
  void NotifyShutdown(driver_runtime::Dispatcher& dispatcher);
  void RemoveDispatcher(Dispatcher& dispatcher);

  async::Loop* loop() { return &loop_; }

 private:
  // Tracks the dispatchers owned by a driver.
  class DriverState : public fbl::WAVLTreeContainable<std::unique_ptr<DriverState>> {
   public:
    explicit DriverState(const void* driver) : driver_(driver) {}

    // Required to instantiate fbl::DefaultKeyedObjectTraits.
    const void* GetKey() const { return driver_; }

    void AddDispatcher(fbl::RefPtr<driver_runtime::Dispatcher> dispatcher) {
      dispatchers_.push_back(std::move(dispatcher));
    }
    void SetDispatcherShutdown(driver_runtime::Dispatcher& dispatcher) {
      shutdown_dispatchers_.push_back(dispatchers_.erase(dispatcher));
    }
    void RemoveDispatcher(driver_runtime::Dispatcher& dispatcher) {
      shutdown_dispatchers_.erase(dispatcher);
    }

    // Appends reference pointers of the driver's dispatchers to the |dispatchers| vector.
    void GetDispatchers(std::vector<fbl::RefPtr<driver_runtime::Dispatcher>>& dispatchers) {
      dispatchers.reserve(dispatchers.size() + dispatchers_.size_slow());
      for (auto& dispatcher : dispatchers_) {
        dispatchers.emplace_back(fbl::RefPtr<Dispatcher>(&dispatcher));
      }
    }

    // Appends reference pointers of the driver's shutdown dispatchers to the |dispatchers| vector.
    void GetShutdownDispatchers(std::vector<fbl::RefPtr<driver_runtime::Dispatcher>>& dispatchers) {
      for (auto& dispatcher : shutdown_dispatchers_) {
        dispatchers.emplace_back(fbl::RefPtr<Dispatcher>(&dispatcher));
      }
    }

    // Sets the observer which will be notified once shutting down the driver's dispatchers
    // completes.
    zx_status_t SetShutdownObserver(fdf_internal_driver_shutdown_observer_t* observer) {
      if (shutdown_observer_) {
        // Currently we only support one observer at a time.
        return ZX_ERR_BAD_STATE;
      }
      shutdown_observer_ = observer;
      return ZX_OK;
    }

    fdf_internal_driver_shutdown_observer_t* TakeShutdownObserver() {
      auto observer = shutdown_observer_;
      shutdown_observer_ = nullptr;
      return observer;
    }

    // Returns whether all dispatchers owned by the driver have completed shutdown.
    bool CompletedShutdown() { return dispatchers_.is_empty(); }

    // Returns whether the driver is currently being shut down.
    bool IsShuttingDown() { return !!shutdown_observer_; }

    // Returns whether there are dispatchers that have not yet been removed with |RemoveDispatcher|.
    bool HasDispatchers() { return !dispatchers_.is_empty() || !shutdown_dispatchers_.is_empty(); }

   private:
    const void* driver_ = nullptr;
    // Dispatchers that have been shutdown.
    fbl::DoublyLinkedList<fbl::RefPtr<driver_runtime::Dispatcher>> shutdown_dispatchers_;
    // All other dispatchers owned by |driver|.
    fbl::DoublyLinkedList<fbl::RefPtr<driver_runtime::Dispatcher>> dispatchers_;
    // The observer which will be notified once shutdown completes.
    fdf_internal_driver_shutdown_observer_t* shutdown_observer_ = nullptr;
  };

  // Make sure this destructs after |loop_|. This is as dispatchers will remove themselves
  // from this list on shutdown.
  fbl::Mutex lock_;
  // Maps from driver owner to driver state.
  fbl::WAVLTree<const void*, std::unique_ptr<DriverState>> drivers_ __TA_GUARDED(&lock_);
  // Notified when all drivers are destroyed.
  fbl::ConditionVariable drivers_destroyed_event_ __TA_GUARDED(&lock_);

  async::Loop loop_;
};

}  // namespace driver_runtime

struct fdf_dispatcher : public driver_runtime::Dispatcher {
  // NOTE: Intentionally empty, do not add to this.
};

#endif  //  SRC_DEVICES_BIN_DRIVER_RUNTIME_DISPATCHER_H_

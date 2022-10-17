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
#include <lib/async/irq.h>
#include <lib/fdf/env.h>
#include <lib/fdf/token.h>
#include <lib/sync/cpp/completion.h>
#include <lib/zx/status.h>
#include <lib/zx/time.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <unordered_set>
#include <vector>

#include <fbl/auto_lock.h>
#include <fbl/canary.h>
#include <fbl/condition_variable.h>
#include <fbl/intrusive_container_utils.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/ref_counted.h>
#include <fbl/string_buffer.h>

#include "src/devices/bin/driver_runtime/async_loop_owned_event_handler.h"
#include "src/devices/bin/driver_runtime/callback_request.h"
#include "src/devices/bin/driver_runtime/driver_context.h"
#include "src/devices/bin/driver_runtime/token_manager.h"

namespace driver_runtime {

class Dispatcher : public async_dispatcher_t,
                   public fbl::RefCounted<Dispatcher>,
                   public fbl::DoublyLinkedListable<fbl::RefPtr<Dispatcher>> {
  // Forward Declaration
  class AsyncWait;

 public:
  using ThreadAdder = fit::callback<zx_status_t()>;

  // Indirect irq object which is used to ensure irqs are tracked and synchronize irqs on
  // SYNCHRONIZED dispatchers.
  // Public so it can be referenced by the DispatcherCoordinator.
  class AsyncIrq : public async_irq_t, public fbl::DoublyLinkedListable<std::unique_ptr<AsyncIrq>> {
   public:
    AsyncIrq(async_irq_t* original_irq, Dispatcher& dispatcher);
    ~AsyncIrq();

    static zx_status_t Bind(std::unique_ptr<AsyncIrq> irq, Dispatcher& dispatcher)
        __TA_REQUIRES(&dispatcher.callback_lock_);
    bool Unbind();

    static void Handler(async_dispatcher_t* dispatcher, async_irq_t* irq, zx_status_t status,
                        const zx_packet_interrupt_t* packet);
    void OnSignal(async_dispatcher_t* async_dispatcher, zx_status_t status,
                  const zx_packet_interrupt_t* packet);

    // Returns a callback request representing the triggered irq.
    std::unique_ptr<driver_runtime::CallbackRequest> CreateCallbackRequest(Dispatcher& dispatcher);

    fbl::RefPtr<Dispatcher> GetDispatcherRef() {
      fbl::AutoLock lock(&lock_);
      return dispatcher_;
    }

   private:
    void SetDispatcherRef(fbl::RefPtr<Dispatcher> dispatcher) {
      fbl::AutoLock lock(&lock_);
      dispatcher_ = std::move(dispatcher);
    }
    // Unlike async::Wait, we cannot store the dispatcher_ref as a std::atomic<Dispatcher*>.
    //
    // Since the |OnSignal| handler may be called many times, it copies the dispatcher reference,
    // rather than taking ownership of it. While |OnSignal| is accessing |dispatcher_|,
    // another thread could be attempting to unbind the dispatcher, so with an atomic raw pointer,
    // is is possible that the dispatcher has been destructed between when we access |dispatcher_|
    // and when we try to convert it back to a RefPtr.
    //
    // If |lock_| needs to be acquired at the same time as the dispatcher's |callback_lock_|,
    // you must acquire |callback_lock_| first.
    fbl::Mutex lock_;
    fbl::RefPtr<Dispatcher> dispatcher_ __TA_GUARDED(&lock_);

    async_irq_t* original_irq_;

    zx_packet_interrupt_t interrupt_packet_ = {};
  };

  // Public for std::make_unique.
  // Use |Create| or |CreateWithLoop| instead of calling directly.
  Dispatcher(uint32_t options, std::string_view name, bool unsynchronized, bool allow_sync_calls,
             const void* owner, async_dispatcher_t* process_shared_dispatcher,
             fdf_dispatcher_shutdown_observer_t* observer);

  // Creates a dispatcher which is backed by |dispatcher|.
  // |adder| should add additional threads to back the dispatcher when invoked.
  //
  // Returns ownership of the dispatcher in |out_dispatcher|. The caller should call
  // |Destroy| once they are done using the dispatcher. Once |Destroy| is called,
  // the dispatcher will be deleted once all callbacks canclled or completed by the dispatcher.
  static zx_status_t CreateWithAdder(uint32_t options, std::string_view name,
                                     std::string_view scheduler_role, const void* owner,
                                     async_dispatcher_t* dispatcher, ThreadAdder adder,
                                     fdf_dispatcher_shutdown_observer_t*,
                                     Dispatcher** out_dispatcher);

  // Creates a dispatcher which is backed by |loop|.
  // |loop| can be the |ProcessSharedLoop|, or a private async loop created by a test.
  //
  // Returns ownership of the dispatcher in |out_dispatcher|. The caller should call
  // |Destroy| once they are done using the dispatcher. Once |Destroy| is called,
  // the dispatcher will be deleted once all callbacks canclled or completed by the dispatcher.
  static zx_status_t CreateWithLoop(uint32_t options, std::string_view name,
                                    std::string_view scheduler_role, const void* owner,
                                    async::Loop* loop, fdf_dispatcher_shutdown_observer_t*,
                                    Dispatcher** out_dispatcher);

  // fdf_dispatcher_t implementation
  // Returns ownership of the dispatcher in |out_dispatcher|. The caller should call
  // |Destroy| once they are done using the dispatcher. Once |Destroy| is called,
  // the dispatcher will be deleted once all callbacks cancelled or completed by the dispatcher.
  static zx_status_t Create(uint32_t options, std::string_view name,
                            std::string_view scheduler_role, fdf_dispatcher_shutdown_observer_t*,
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
  zx_status_t GetSequenceId(async_sequence_id_t* out_sequence_id, const char** out_error);
  zx_status_t CheckSequenceId(async_sequence_id_t sequence_id, const char** out_error);

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
                               zx_status_t callback_reason);

  // Adds wait to |waits_|.
  void AddWaitLocked(std::unique_ptr<AsyncWait> wait) __TA_REQUIRES(&callback_lock_);
  // Removes wait from |waits_| and triggers idle check.
  std::unique_ptr<AsyncWait> RemoveWait(AsyncWait* wait) __TA_EXCLUDES(&callback_lock_);
  std::unique_ptr<AsyncWait> RemoveWaitLocked(AsyncWait* wait) __TA_REQUIRES(&callback_lock_);
  // Moves wait from |waits_| queue onto |registered_callbacks_| and signals that it can be called.
  void QueueWait(AsyncWait* wait, zx_status_t status);

  // Adds irq to |irqs_|.
  void AddIrqLocked(std::unique_ptr<AsyncIrq> irq) __TA_REQUIRES(&callback_lock_);
  // Removes irq from |irqs_| and triggers idle check.
  std::unique_ptr<AsyncIrq> RemoveIrqLocked(AsyncIrq* irq) __TA_REQUIRES(&callback_lock_);
  // Creates a new callback request for |irq|, queues it onto |registered_callbacks_| and signals
  // that it can be called.
  void QueueIrq(AsyncIrq* irq, zx_status_t status);

  // Removes the callback matching |callback_request| from the queue and returns it.
  // May return nullptr if no such callback is found.
  std::unique_ptr<CallbackRequest> CancelCallback(CallbackRequest& callback_request);

  // Sets the callback reason for a currently queued callback request.
  // This may fail if the callback is already running or scheduled to run.
  // Returns true if a callback matching |callback_request| was found, false otherwise.
  bool SetCallbackReason(CallbackRequest* callback_request, zx_status_t callback_reason);

  // Removes the callback that manages the async dispatcher |operation| and returns it.
  // May return nullptr if no such callback is found.
  std::unique_ptr<CallbackRequest> CancelAsyncOperationLocked(void* operation)
      __TA_REQUIRES(&callback_lock_);

  // Returns true if the dispatcher has no active threads or queued requests.
  // This does not include unsignaled waits, or tasks which have been scheduled
  // for a future deadline.
  // This unlocked version of |IsIdleLocked| is called by tests.
  bool IsIdle() {
    fbl::AutoLock lock(&callback_lock_);
    return IsIdleLocked();
  }

  // Returns ownership of an event that will be signaled once the dispatcher is ready
  // to complete shutdown.
  zx::result<zx::event> RegisterForCompleteShutdownEvent();

  // Blocks the current thread until the dispatcher is idle.
  void WaitUntilIdle();

  // Registers |token| as waiting to be exchanged for a fdf handle. This |token| is already
  // registered with the token manager, but this allows the dispatcher to call the token
  // exchange cancellation callback in the case where the dispatcher shuts down before the
  // exchange is completed. This is as the token manager would not be able to queue a
  // cancellation callback once the dispatcher is in a shutdown state.
  zx_status_t RegisterPendingToken(fdf_token_t* token);
  // Queues a |CallbackRequest| for the token exchange callback and removes |token|
  // from the pending list. This is called when |fdf_token_register| and |fdf_token_exchange|
  // have been called for the same token.
  // TODO(fxbug.dev/105578): replace fdf::Channel with a generic C++ handle type when available.
  zx_status_t ScheduleTokenCallback(fdf_token_t* token, zx_status_t status, fdf::Channel channel);

  // Returns the dispatcher options specified by the user.
  uint32_t options() const { return options_; }
  bool unsynchronized() const { return unsynchronized_; }
  bool allow_sync_calls() const { return allow_sync_calls_; }

  // Returns the driver which owns this dispatcher.
  const void* owner() const { return owner_; }

  const async_dispatcher_t* process_shared_dispatcher() const { return process_shared_dispatcher_; }

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

  class CompleteShutdownEventManager {
   public:
    // Returns a duplicate of the event that will be signaled when the dispatcher
    // is ready to complete shutdown.
    zx::result<zx::event> GetEvent();
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

    void OnSignal(async_dispatcher_t* async_dispatcher, zx_status_t status,
                  const zx_packet_signal_t* signal);

    // Sets the pending_cancellation_ flag to true. See that field's comment for details.
    void MarkPendingCancellation() { pending_cancellation_ = true; }
    bool is_pending_cancellation() const { return pending_cancellation_; }

   private:
    // Implementing a specialization of std::atomic<fbl::RefPtr<T>> is more challenging than just
    // manipulating it as a raw pointer. It must be stored as an atomic because it is mutated from
    // multiple threads after AsyncWait is constructed, and we wish to avoid a lock.
    std::atomic<Dispatcher*> dispatcher_ref_;
    async_wait_t* original_wait_;

    // If true, CancelWait() has been called on another thread and we should cancel the wait rather
    // than invoking the callback.
    //
    // This condition occurs when a wait has been pulled off the dispatcher's port but the callback
    // has not yet been invoked. AsyncWait wraps the underlying async_wait_t callback in its own
    // custom callback (OnSignal), so there is an interval between when OnSignal is invoked and the
    // underlying callback is invoked during which a race with Dispatcher::CancelWait() can occur.
    // See fxbug.dev/109988 for details.
    bool pending_cancellation_ = false;

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

  void SetEventWaiter(EventWaiter* event_waiter) __TA_EXCLUDES(&callback_lock_) {
    fbl::AutoLock lock(&callback_lock_);
    event_waiter_ = event_waiter;
  }

  // Returns true if the dispatcher has no active threads or queued requests.
  // This does not include unsignaled waits.
  bool IsIdleLocked() __TA_REQUIRES(&callback_lock_);

  // Returns true if the dispatcher has waits or tasks scheduled for a future deadline.
  // This includes unsignaled waits and delayed tasks.
  bool HasFutureOpsScheduledLocked() __TA_REQUIRES(&callback_lock_);

  // Checks whether the dispatcher has entered and idle state and if so notifies any registered
  // waiters.
  void IdleCheckLocked() __TA_REQUIRES(&callback_lock_);

  // Returns true if the current thread is managed by the driver runtime.
  bool IsRuntimeManagedThread() { return !driver_context::IsCallStackEmpty(); }

  // Returns whether the dispatcher is in the running state.
  bool IsRunningLocked() __TA_REQUIRES(&callback_lock_) {
    return state_ == DispatcherState::kRunning;
  }

  // User provided name. Useful for debugging purposes.
  fbl::StringBuffer<ZX_MAX_NAME_LEN> name_;

  // Dispatcher options set by the user.
  uint32_t options_;
  bool unsynchronized_;
  bool allow_sync_calls_;

  // The driver which owns this dispatcher. May be nullptr if undeterminable.
  const void* const owner_;

  // Global dispatcher shared across all dispatchers in a process.
  async_dispatcher_t* process_shared_dispatcher_;
  EventWaiter* event_waiter_ __TA_GUARDED(&callback_lock_);

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

  // Irqs which are bound to the dispatcher. A new callback request is added to
  // the |registered_callbacks_| queue when an interrupt is triggered.
  // They are tracked so that they may be canceled during |Destroy| prior to calling
  // |CompleteDestroy|.
  fbl::DoublyLinkedList<std::unique_ptr<AsyncIrq>> irqs_ __TA_GUARDED(&callback_lock_);

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

  CompleteShutdownEventManager complete_shutdown_event_manager_ __TA_GUARDED(&callback_lock_);

  // Notified when the dispatcher enters an idle state, not including pending waits or delayed
  // tasks.
  fbl::ConditionVariable idle_event_ __TA_GUARDED(&callback_lock_);

  // The observer that should be called when shutting down the dispatcher completes.
  fdf_dispatcher_shutdown_observer_t* shutdown_observer_ __TA_GUARDED(&callback_lock_) = nullptr;

  // Tokens waiting to be exchanged for fdf handles that have been registered with the token manager
  // on this dispatcher.
  std::unordered_set<fdf_token_t*> registered_tokens_;

  fbl::Canary<fbl::magic("FDFD")> canary_;
};

// Coordinator for all dispatchers in a process.
class DispatcherCoordinator {
 public:
  // We default to one thread, and start additional threads when blocking dispatchers are created.
  DispatcherCoordinator() : config_(MakeConfig()), loop_(&config_) {
    loop_.StartThread("fdf-dispatcher-0");

    token_manager_.SetGlobalDispatcher(loop_.dispatcher());
  }

  static void DestroyAllDispatchers();
  static void WaitUntilDispatchersIdle();
  static void WaitUntilDispatchersDestroyed();
  static zx_status_t ShutdownDispatchersAsync(const void* driver,
                                              fdf_env_driver_shutdown_observer_t* observer);

  // Implementation of fdf_protocol_*.
  static zx_status_t TokenRegister(zx_handle_t token, fdf_dispatcher_t* dispatcher,
                                   fdf_token_t* handler);
  static zx_status_t TokenExchange(zx_handle_t token, fdf_handle_t channel);

  // Returns ZX_OK if |dispatcher| was added successfully.
  // Returns ZX_ERR_BAD_STATE if the driver is currently shutting down.
  zx_status_t AddDispatcher(fbl::RefPtr<Dispatcher> dispatcher);
  // Records the dispatcher as being shutdown.
  void SetShutdown(driver_runtime::Dispatcher& dispatcher);
  // Notifies the dispatcher coordinator that a dispatcher has completed shutdown.
  void NotifyShutdown(driver_runtime::Dispatcher& dispatcher);
  void RemoveDispatcher(Dispatcher& dispatcher);
  // Stores |irq| which has been unbound.
  // This is avoid destroying the irq wrapper immediately after unbinding, as it's possible
  // another process dispatcher thread has already pulled an irq packet from the port
  // and may attempt to call the irq handler.
  static void CacheUnboundIrq(std::unique_ptr<driver_runtime::Dispatcher::AsyncIrq> irq);
  // Notifies the coordinator that the current thread has woken up. This will check if there is
  // cached irq garbage collection to do.
  // |thread_irq_generation_id| is the generation id seen by the thread at its last wakeup.
  // |out_cur_irq_generation_id| is the generation id to update the thread to.
  static void OnThreadWakeup(uint32_t thread_irq_generation_id,
                             uint32_t* out_cur_irq_generation_id);
  zx_status_t AddThread();

  // Resets back down to 1 thread.
  // Must only be called when there are no outstanding dispatchers.
  // Must not be called from within a driver_runtime managed thread as that will result in a
  // deadlock.
  void Reset();

  async::Loop* loop() { return &loop_; }

  uint32_t num_threads() {
    fbl::AutoLock al(&lock_);
    return number_threads_;
  }

 private:
  static constexpr async_loop_config_t MakeConfig() {
    async_loop_config_t config = kAsyncLoopConfigNoAttachToCurrentThread;
    config.irq_support = true;
    return config;
  }

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

    // Sets the driver as shutting down, and the observer which will be notified once
    // shutting down the driver's dispatchers completes.
    zx_status_t SetShuttingDown(fdf_env_driver_shutdown_observer_t* observer) {
      if (shutdown_observer_ || driver_shutting_down_) {
        // Currently we only support one observer at a time.
        return ZX_ERR_BAD_STATE;
      }
      driver_shutting_down_ = true;
      shutdown_observer_ = observer;
      return ZX_OK;
    }

    void SetShutdownComplete() {
      ZX_ASSERT(driver_shutting_down_);
      // We should have already called the shutdown observer.
      ZX_ASSERT(!shutdown_observer_);
      driver_shutting_down_ = false;
    }

    // Returns whether all dispatchers owned by the driver have completed shutdown.
    bool CompletedShutdown() { return dispatchers_.is_empty(); }

    // Returns whether the driver is currently being shut down.
    bool IsShuttingDown() { return driver_shutting_down_; }

    // Returns whether there are dispatchers that have not yet been removed with |RemoveDispatcher|.
    bool HasDispatchers() { return !dispatchers_.is_empty() || !shutdown_dispatchers_.is_empty(); }

    fdf_env_driver_shutdown_observer_t* take_shutdown_observer() {
      auto observer = shutdown_observer_;
      shutdown_observer_ = nullptr;
      return observer;
    }

   private:
    const void* driver_ = nullptr;
    // Dispatchers that have been shutdown.
    fbl::DoublyLinkedList<fbl::RefPtr<driver_runtime::Dispatcher>> shutdown_dispatchers_;
    // All other dispatchers owned by |driver|.
    fbl::DoublyLinkedList<fbl::RefPtr<driver_runtime::Dispatcher>> dispatchers_;
    // Whether the driver is in the process of shutting down.
    bool driver_shutting_down_ = false;
    // The observer which will be notified once shutdown completes.
    fdf_env_driver_shutdown_observer_t* shutdown_observer_ = nullptr;
  };

  // This stores irqs to avoid destroying them immediately after unbinding.
  // Even though unbinding an irq will clear all irq packets on a port,
  // it's possible another process dispatcher thread has already pulled an irq packet
  // from the port and may attempt to call the irq handler.
  //
  // It is safe to destroy a cached irq once we can determine that all threads
  // have woken up at least once since the irq was unbound.
  class CachedIrqs {
   public:
    // Adds an unbound irq to the cached irqs.
    void AddIrq(std::unique_ptr<Dispatcher::AsyncIrq> irq) __TA_REQUIRES(&lock_);
    // Updates the thread tracking and checks whether to garbage collect the current generation of
    // irqs.
    void NewThreadWakeup(uint32_t total_threads) __TA_REQUIRES(&lock_);

    // The coordinator can compare the current generation id to a thread's stored generation id to
    // see if the thread wakeup has not yet been tracked, in which case |NewThreadWakeup| should be
    // called.
    uint32_t cur_generation_id() { return cur_generation_id_.load(); }

   private:
    using List = fbl::DoublyLinkedList<std::unique_ptr<Dispatcher::AsyncIrq>, fbl::DefaultObjectTag,
                                       fbl::SizeOrder::Constant>;

    void IncrementGenerationId() __TA_REQUIRES(&lock_) {
      if (cur_generation_id_.fetch_add(1) == UINT32_MAX) {
        // |fetch_add| returns the value before adding. Avoid using 0 for a new generation id,
        // since new threads may be spawned with default generation id 0.
        cur_generation_id_++;
      }
    }

    // The current generation of cached irqs to be garbage collected once all threads wakeup.
    List cur_generation_ __TA_GUARDED(&lock_);
    // These are the irqs that were unbound after we already tracked a thread wakeup for the
    // current generation.
    List next_generation_ __TA_GUARDED(&lock_);

    // The number of threads that have woken up since the irqs in the |cur_generation_| list was
    // populated.
    uint32_t threads_wakeup_count_ __TA_GUARDED(&lock_) = 0;

    // This is not locked for reads, so that threads do not need to deal with lock contention if
    // there are no cached irqs.
    std::atomic<uint32_t> cur_generation_id_ = 0;
  };

  // Make sure this destructs after |loop_|. This is as dispatchers will remove themselves
  // from this list on shutdown.
  fbl::Mutex lock_;
  // Maps from driver owner to driver state.
  fbl::WAVLTree<const void*, std::unique_ptr<DriverState>> drivers_ __TA_GUARDED(&lock_);
  // Notified when all drivers are destroyed.
  fbl::ConditionVariable drivers_destroyed_event_ __TA_GUARDED(&lock_);

  // Tracks the number of threads we've spawned via |loop_|.
  uint32_t number_threads_ __TA_GUARDED(&lock_) = 1;
  // Tracks the number of dispatchers which have sync calls allowed. We will only spawn additional
  // threads if this number exceeds |number_threads_|.
  uint32_t dispatcher_threads_needed_ __TA_GUARDED(&lock_) = 1;

  // Stores unbound irqs which will be garbage collected at a later time.
  CachedIrqs cached_irqs_;

  TokenManager token_manager_;

  async_loop_config_t config_;
  // |loop_| must be declared last, to ensure that the loop shuts down before
  // other members are destructed.
  async::Loop loop_;
};

}  // namespace driver_runtime

struct fdf_dispatcher : public driver_runtime::Dispatcher {
  // NOTE: Intentionally empty, do not add to this.
};

#endif  //  SRC_DEVICES_BIN_DRIVER_RUNTIME_DISPATCHER_H_

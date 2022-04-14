// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dispatcher.h"

#include <lib/async/dispatcher.h>
#include <lib/async/irq.h>
#include <lib/async/receiver.h>
#include <lib/async/task.h>
#include <lib/async/trap.h>
#include <lib/ddk/device.h>
#include <lib/fdf/dispatcher.h>
#include <lib/fit/defer.h>
#include <lib/zx/clock.h>
#include <stdlib.h>
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/listnode.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <memory>
#include <string>

#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>

#include "src/devices/bin/driver_runtime/callback_request.h"
#include "src/devices/bin/driver_runtime/driver_context.h"
#include "src/devices/lib/log/log.h"

namespace driver_runtime {

namespace {

const async_ops_t g_dispatcher_ops = {
    .version = ASYNC_OPS_V2,
    .reserved = 0,
    .v1 = {
        .now =
            [](async_dispatcher_t* dispatcher) {
              return static_cast<Dispatcher*>(dispatcher)->GetTime();
            },
        .begin_wait =
            [](async_dispatcher_t* dispatcher, async_wait_t* wait) {
              return static_cast<Dispatcher*>(dispatcher)->BeginWait(wait);
            },
        .cancel_wait =
            [](async_dispatcher_t* dispatcher, async_wait_t* wait) {
              return static_cast<Dispatcher*>(dispatcher)->CancelWait(wait);
            },
        .post_task =
            [](async_dispatcher_t* dispatcher, async_task_t* task) {
              return static_cast<Dispatcher*>(dispatcher)->PostTask(task);
            },
        .cancel_task =
            [](async_dispatcher_t* dispatcher, async_task_t* task) {
              return static_cast<Dispatcher*>(dispatcher)->CancelTask(task);
            },
        .queue_packet =
            [](async_dispatcher_t* dispatcher, async_receiver_t* receiver,
               const zx_packet_user_t* data) {
              return static_cast<Dispatcher*>(dispatcher)->QueuePacket(receiver, data);
            },
        .set_guest_bell_trap = [](async_dispatcher_t* dispatcher, async_guest_bell_trap_t* trap,
                                  zx_handle_t guest, zx_vaddr_t addr,
                                  size_t length) { return ZX_ERR_NOT_SUPPORTED; },
    },
    .v2 = {
        .bind_irq =
            [](async_dispatcher_t* dispatcher, async_irq_t* irq) {
              return static_cast<Dispatcher*>(dispatcher)->BindIrq(irq);
            },
        .unbind_irq =
            [](async_dispatcher_t* dispatcher, async_irq_t* irq) {
              return static_cast<Dispatcher*>(dispatcher)->UnbindIrq(irq);
            },
        .create_paged_vmo = [](async_dispatcher_t* dispatcher, async_paged_vmo_t* paged_vmo,
                               uint32_t options, zx_handle_t pager, uint64_t vmo_size,
                               zx_handle_t* vmo_out) { return ZX_ERR_NOT_SUPPORTED; },
        .detach_paged_vmo = [](async_dispatcher_t* dispatcher,
                               async_paged_vmo_t* paged_vmo) { return ZX_ERR_NOT_SUPPORTED; },
    },
};

}  // namespace

Dispatcher::AsyncWait::AsyncWait(async_wait_t* original_wait, Dispatcher& dispatcher)
    : async_wait_t{{ASYNC_STATE_INIT},
                   &Dispatcher::AsyncWait::Handler,
                   original_wait->object,
                   original_wait->trigger,
                   0},
      original_wait_(original_wait) {
  original_wait_->state.reserved[0] = reinterpret_cast<uintptr_t>(this);

  auto async_dispatcher = dispatcher.GetAsyncDispatcher();
  driver_runtime::Callback callback =
      [this, async_dispatcher](std::unique_ptr<driver_runtime::CallbackRequest> callback_request,
                               fdf_status_t status) {
        original_wait_->state.reserved[0] = 0;
        original_wait_->handler(async_dispatcher, original_wait_, status, &signal_packet_);
      };
  SetCallback(static_cast<fdf_dispatcher_t*>(&dispatcher), std::move(callback), original_wait_);
}

Dispatcher::AsyncWait::~AsyncWait() {
  // This shouldn't destruct until the wait was canceled or it has been completed.
  ZX_ASSERT(dispatcher_ref_ == nullptr);
}

// static
zx_status_t Dispatcher::AsyncWait::BeginWait(std::unique_ptr<AsyncWait> wait,
                                             Dispatcher& dispatcher) {
  // Purposefully create a cycle which is broken in Cancel or OnSignal.
  // This needs to be done ahead of starting the async wait in case another thread on the dispatcher
  // signals the dispatcher.
  auto dispatcher_ref = fbl::RefPtr(&dispatcher);
  wait->dispatcher_ref_ = fbl::ExportToRawPtr(&dispatcher_ref);
  auto* wait_ref = wait.get();
  dispatcher.AddWaitLocked(std::move(wait));

  zx_status_t status = async_begin_wait(dispatcher.process_shared_dispatcher_, wait_ref);
  if (status != ZX_OK) {
    dispatcher.RemoveWaitLocked(wait_ref);
    fbl::ImportFromRawPtr(wait_ref->dispatcher_ref_.exchange(nullptr));
    return status;
  }
  return ZX_OK;
}

bool Dispatcher::AsyncWait::Cancel() {
  // We do a load here rather than a exchange as OnSignal may still be triggered and we need to
  // avoid preventing it from accessing the |dispatcher_ref_|.
  auto* dispatcher_ref = dispatcher_ref_.load();
  if (dispatcher_ref == nullptr) {
    // OnSignal was triggered in another thread.
    return false;
  }
  auto dispatcher = fbl::RefPtr(dispatcher_ref);
  auto status = async_cancel_wait(dispatcher->process_shared_dispatcher_, this);
  if (status != ZX_OK) {
    // OnSignal was triggered in another thread, or is about to be.
    ZX_DEBUG_ASSERT(status == ZX_ERR_NOT_FOUND);
    return false;
  }
  // It is now safe to recover the dispatcher reference.
  dispatcher_ref = dispatcher_ref_.exchange(nullptr);
  ZX_DEBUG_ASSERT(dispatcher_ref != nullptr);
  fbl::ImportFromRawPtr(dispatcher_ref);

  return true;
}

// static
void Dispatcher::AsyncWait::Handler(async_dispatcher_t* dispatcher, async_wait_t* wait,
                                    zx_status_t status, const zx_packet_signal_t* signal) {
  static_cast<AsyncWait*>(wait)->OnSignal(dispatcher, status, signal);
}

void Dispatcher::AsyncWait::OnSignal(async_dispatcher_t* async_dispatcher, zx_status_t status,
                                     const zx_packet_signal_t* signal) {
  auto* dispatcher_ref = dispatcher_ref_.exchange(nullptr);
  ZX_DEBUG_ASSERT(dispatcher_ref != nullptr);
  auto dispatcher = fbl::ImportFromRawPtr(dispatcher_ref);

  signal_packet_ = *signal;

  dispatcher->QueueWait(this, status);
}

DispatcherCoordinator& GetDispatcherCoordinator() {
  static DispatcherCoordinator shared_loop;
  return shared_loop;
}

Dispatcher::Dispatcher(uint32_t options, bool unsynchronized, bool allow_sync_calls,
                       const void* owner, async_dispatcher_t* process_shared_dispatcher,
                       fdf_dispatcher_shutdown_observer_t* observer)
    : async_dispatcher_t{&g_dispatcher_ops},
      options_(options),
      unsynchronized_(unsynchronized),
      allow_sync_calls_(allow_sync_calls),
      owner_(owner),
      process_shared_dispatcher_(process_shared_dispatcher),
      timer_(this),
      shutdown_observer_(observer) {}

// static
fdf_status_t Dispatcher::CreateWithLoop(uint32_t options, const char* scheduler_role,
                                        size_t scheduler_role_len, const void* owner,
                                        async::Loop* loop,
                                        fdf_dispatcher_shutdown_observer_t* observer,
                                        Dispatcher** out_dispatcher) {
  ZX_DEBUG_ASSERT(out_dispatcher);

  bool unsynchronized = options & FDF_DISPATCHER_OPTION_UNSYNCHRONIZED;
  bool allow_sync_calls = options & FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS;
  if (unsynchronized && allow_sync_calls) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (!owner) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (allow_sync_calls) {
    static int number = 0;
    auto name = "fdf-dispatcher-thread-" + std::to_string(number++);
    zx_status_t status = loop->StartThread(name.c_str());
    if (status != ZX_OK) {
      return status;
    }
  }

  auto dispatcher = fbl::MakeRefCounted<Dispatcher>(options, unsynchronized, allow_sync_calls,
                                                    owner, loop->dispatcher(), observer);

  zx::event event;
  if (zx_status_t status = zx::event::create(0, &event); status != ZX_OK) {
    return status;
  }

  auto self = dispatcher.get();
  auto event_waiter = std::make_unique<EventWaiter>(
      std::move(event),
      [self](std::unique_ptr<EventWaiter> event_waiter, fbl::RefPtr<Dispatcher> dispatcher_ref) {
        self->DispatchCallbacks(std::move(event_waiter), std::move(dispatcher_ref));
      });
  dispatcher->event_waiter_ = event_waiter.get();
  zx_status_t status = EventWaiter::BeginWaitWithRef(std::move(event_waiter), dispatcher);
  if (status == ZX_ERR_BAD_STATE) {
    dispatcher->event_waiter_ = nullptr;
    return status;
  }

  // This may fail if the entire driver is being shut down by the driver host.
  status = GetDispatcherCoordinator().AddDispatcher(dispatcher);
  if (status != ZX_OK) {
    dispatcher->event_waiter_ = nullptr;
    return status;
  }

  // This reference will be recovered in |Destroy|.
  *out_dispatcher = fbl::ExportToRawPtr(&dispatcher);
  return ZX_OK;
}

// fdf_dispatcher_t implementation

// static
fdf_status_t Dispatcher::Create(uint32_t options, const char* scheduler_role,
                                size_t scheduler_role_len,
                                fdf_dispatcher_shutdown_observer_t* observer,
                                Dispatcher** out_dispatcher) {
  return CreateWithLoop(options, scheduler_role, scheduler_role_len,
                        driver_context::GetCurrentDriver(), GetDispatcherCoordinator().loop(),
                        observer, out_dispatcher);
}

// static
Dispatcher* Dispatcher::FromAsyncDispatcher(async_dispatcher_t* dispatcher) {
  auto ret = static_cast<Dispatcher*>(dispatcher);
  ret->canary_.Assert();
  return ret;
}

async_dispatcher_t* Dispatcher::GetAsyncDispatcher() {
  // Note: We inherit from async_t so we can upcast to it.
  return (async_dispatcher_t*)this;
}

void Dispatcher::ShutdownAsync() {
  {
    fbl::AutoLock lock(&callback_lock_);

    switch (state_) {
      case DispatcherState::kRunning:
        state_ = DispatcherState::kShuttingDown;
        break;
      case DispatcherState::kShuttingDown:
      case DispatcherState::kShutdown:
      case DispatcherState::kDestroyed:
        return;
      default:
        ZX_ASSERT_MSG(false, "Dispatcher::ShutdownAsync got unknown dispatcher state %d", state_);
    }

    // Move the requests into a separate queue so we will be able to enter an idle state.
    // This queue will be processed by |CompleteShutdown|.
    shutdown_queue_ = std::move(callback_queue_);
    shutdown_queue_.splice(shutdown_queue_.end(), registered_callbacks_);

    // Try to cancel all outstanding waits. Successfully canceled waits should be have their
    // callbacks triggered.
    auto waits = std::move(waits_);
    for (auto wait = waits.pop_front(); wait; wait = waits.pop_front()) {
      if (wait->Cancel()) {
        // We were successful. Lets queue this up to be processed by |CompleteDestroy|.
        shutdown_queue_.push_back(std::move(wait));
      } else {
        // We weren't successful, |wait| is being run or queued to run and will want to remove this
        // from the |waits_| list.
        waits.push_back(std::move(wait));
      }
    }

    timer_.Cancel();
    shutdown_queue_.splice(shutdown_queue_.end(), delayed_tasks_);

    // To avoid race conditions with attempting to cancel a wait that might be scheduled to
    // run, we will cancel the event waiter in the |CompleteShutdown| callback. This is as
    // |async::Wait::Cancel| is not thread safe.
  }

  auto dispatcher_ref = fbl::RefPtr<Dispatcher>(this);

  // The dispatcher shutdown API specifies that on shutdown, tasks and cancellation
  // callbacks should run serialized. Wait for all active threads to
  // complete before calling the cancellation callbacks.
  auto event = RegisterForCompleteShutdownEvent();
  ZX_ASSERT(event.status_value() == ZX_OK);

  // Don't use async::WaitOnce as it sets the handler in a thread unsafe way.
  auto wait = std::make_unique<async::Wait>(
      event->get(), ZX_EVENT_SIGNALED, 0,
      [dispatcher_ref = std::move(dispatcher_ref), event = std::move(*event)](
          async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
          const zx_packet_signal_t* signal) mutable {
        ZX_ASSERT(status == ZX_OK || status == ZX_ERR_CANCELED);
        dispatcher_ref->CompleteShutdown();
        delete wait;
      });
  ZX_ASSERT(wait->Begin(process_shared_dispatcher_) == ZX_OK);
  wait.release();  // This will be deleted by the wait handler once it is called.
}

void Dispatcher::CompleteShutdown() {
  fbl::DoublyLinkedList<std::unique_ptr<CallbackRequest>> to_cancel;
  {
    fbl::AutoLock lock(&callback_lock_);

    ZX_ASSERT(state_ == DispatcherState::kShuttingDown);
    ZX_ASSERT(IsIdleLocked() && !HasFutureOpsScheduledLocked());

    if (event_waiter_) {
      // Since the event waiter holds a reference to the dispatcher,
      // we need to cancel it to reclaim it.
      // This should always succeed, as there should be no other threads processing
      // tasks for this dispatcher, and we should have cleared |event_waiter_| if
      // the AsyncLoopOwned event waiter was dropped.
      ZX_ASSERT(event_waiter_->Cancel() != nullptr);
      event_waiter_ = nullptr;
    }
    to_cancel = std::move(shutdown_queue_);
  }

  // Call the callbacks outside the lock.
  while (!to_cancel.is_empty()) {
    auto callback_request = to_cancel.pop_front();
    ZX_ASSERT(callback_request);
    callback_request->Call(std::move(callback_request), ZX_ERR_CANCELED);
  }
  fdf_dispatcher_shutdown_observer_t* shutdown_observer = nullptr;
  {
    fbl::AutoLock lock(&callback_lock_);
    state_ = DispatcherState::kShutdown;
    shutdown_observer = shutdown_observer_;
  }
  GetDispatcherCoordinator().SetShutdown(*this);
  // We need to call the dispatcher shutdown handler before notifying the dispatcher coordinator.
  if (shutdown_observer) {
    shutdown_observer->handler(static_cast<fdf_dispatcher_t*>(this), shutdown_observer);
  }

  GetDispatcherCoordinator().NotifyShutdown(*this);
}

void Dispatcher::Destroy() {
  {
    fbl::AutoLock lock(&callback_lock_);
    ZX_ASSERT(state_ == DispatcherState::kShutdown);
    state_ = DispatcherState::kDestroyed;
  }
  // Recover the reference created in |CreateWithLoop|.
  auto dispatcher_ref = fbl::ImportFromRawPtr(this);
  GetDispatcherCoordinator().RemoveDispatcher(*this);
}

// async_dispatcher_t implementation

zx_time_t Dispatcher::GetTime() { return zx_clock_get_monotonic(); }

zx_status_t Dispatcher::BeginWait(async_wait_t* wait) {
  fbl::AutoLock lock(&callback_lock_);
  if (!IsRunningLocked()) {
    return ZX_ERR_BAD_STATE;
  }
  // TODO(92740): we should do something more efficient rather than creating a new
  // AsyncWait each time.
  auto async_wait = std::make_unique<AsyncWait>(wait, *this);
  return AsyncWait::BeginWait(std::move(async_wait), *this);
}

zx_status_t Dispatcher::CancelWait(async_wait_t* wait) {
  // TODO: This can currently fail when the async wait has been completed in another thread,
  // but has not yet had an callback request posted. We should try to make it not fail in that
  // scenario as it is not consistent with expected behavior. fidl::Client may assert that
  // this doesn't fail for instance.

  // First try to cancel the async wait from the shared dispatcher.
  auto* async_wait = reinterpret_cast<AsyncWait*>(wait->state.reserved[0]);
  if (async_wait != nullptr) {
    if (async_wait->Cancel()) {
      // We shouldn't have to worry about racing anyone if cancelation was successful.
      ZX_ASSERT(RemoveWait(async_wait) != nullptr);
      return ZX_OK;
    }
  }

  // Second try to cancel it from the  callback queue.
  auto callback_request = CancelAsyncOperation(wait);
  return callback_request ? ZX_OK : ZX_ERR_NOT_FOUND;
}

zx::time Dispatcher::GetNextTimeoutLocked() const {
  // Check delayed tasks only when callback_queue_ is empty. We will routinely check if delayed
  // tasks can be moved into the callback queue anyways and reset the timer whenever callback queue
  // is empty.
  if (callback_queue_.is_empty()) {
    if (delayed_tasks_.is_empty()) {
      return zx::time::infinite();
    }
    return static_cast<const DelayedTask*>(&delayed_tasks_.front())->deadline;
  }
  return zx::time::infinite();
}

void Dispatcher::ResetTimerLocked() {
  zx::time deadline = GetNextTimeoutLocked();
  if (deadline == zx::time::infinite()) {
    // Nothing is left on the queue to fire.
    timer_.Cancel();
    return;
  }

  // The tradeoff of using a task instead of a dedicated timer is that we need to cancel the task
  // every time a task with a shorter deadline comes in. This isn't really too bad, assuming there
  // is at least two delayed tasks scheduled, otherwise the timer will be canceled. If we used a
  // custom implementation for our shared process loop, then we could also have an
  // "UpdateTaskDeadline" method on tasks which would allow us to shift the deadline as necessary,
  // without risking the need to cancel the task.

  if (timer_.current_deadline() > deadline && timer_.Cancel() == ZX_OK) {
    timer_.BeginWait(process_shared_dispatcher_, deadline);
  }
}

void Dispatcher::InsertDelayedTaskSortedLocked(std::unique_ptr<DelayedTask> task) {
  // Find the first node that is bigger and insert before it. fbl::DoublyLinkedList handles all of
  // the edge cases for us.
  auto iter = delayed_tasks_.find_if([&](const CallbackRequest& other) {
    return static_cast<const DelayedTask*>(&other)->deadline > task->deadline;
  });
  delayed_tasks_.insert(iter, std::move(task));
}

void Dispatcher::CheckDelayedTasksLocked() {
  if (!IsRunningLocked()) {
    IdleCheckLocked();
    return;
  }
  zx::time now = zx::clock::get_monotonic();
  auto iter = delayed_tasks_.find_if([&](const CallbackRequest& task) {
    return static_cast<const DelayedTask*>(&task)->deadline > now;
  });
  if (iter != delayed_tasks_.begin()) {
    fbl::DoublyLinkedList<std::unique_ptr<CallbackRequest>> done_tasks;
    done_tasks = delayed_tasks_.split_after(--iter);
    // split_after removes the tasks which are *not* done, so we must swap the lists to get desired
    // result.
    std::swap(delayed_tasks_, done_tasks);
    callback_queue_.splice(callback_queue_.end(), done_tasks);
    if (event_waiter_ && !event_waiter_->signaled()) {
      event_waiter_->signal();
    }
  } else {
    ResetTimerLocked();
  }
}

void Dispatcher::CheckDelayedTasks() {
  fbl::AutoLock al(&callback_lock_);
  CheckDelayedTasksLocked();
}

void Dispatcher::Timer::Handler() {
  current_deadline_ = zx::time::infinite();
  dispatcher_->CheckDelayedTasks();
}

zx_status_t Dispatcher::PostTask(async_task_t* task) {
  driver_runtime::Callback callback =
      [this, task](std::unique_ptr<driver_runtime::CallbackRequest> callback_request,
                   fdf_status_t status) { task->handler(this, task, status); };

  const zx::time now = zx::clock::get_monotonic();
  if (zx::time(task->deadline) <= now) {
    // TODO(92740): we should do something more efficient rather than creating a new
    // callback request each time.
    auto callback_request =
        std::make_unique<driver_runtime::CallbackRequest>(CallbackRequest::RequestType::kTask);
    callback_request->SetCallback(static_cast<fdf_dispatcher_t*>(this), std::move(callback), task);
    CallbackRequest* callback_ptr = callback_request.get();
    // TODO(92878): handle task deadlines.
    callback_request = RegisterCallbackWithoutQueueing(std::move(callback_request));
    // Dispatcher returned callback request as queueing failed.
    if (callback_request) {
      return ZX_ERR_BAD_STATE;
    }
    QueueRegisteredCallback(callback_ptr, ZX_OK);
  } else {
    if (task->deadline == ZX_TIME_INFINITE) {
      // Tasks must complete.
      return ZX_ERR_INVALID_ARGS;
    }
    auto delayed_task = std::make_unique<DelayedTask>(zx::time(task->deadline));
    delayed_task->SetCallback(static_cast<fdf_dispatcher_t*>(this), std::move(callback), task);

    fbl::AutoLock al(&callback_lock_);
    InsertDelayedTaskSortedLocked(std::move(delayed_task));
    ResetTimerLocked();
  }
  return ZX_OK;
}

zx_status_t Dispatcher::CancelTask(async_task_t* task) {
  auto callback_request = CancelAsyncOperation(task);
  return callback_request ? ZX_OK : ZX_ERR_NOT_FOUND;
}

zx_status_t Dispatcher::QueuePacket(async_receiver_t* receiver, const zx_packet_user_t* data) {
  fbl::AutoLock lock(&callback_lock_);
  if (!IsRunningLocked()) {
    return ZX_ERR_BAD_STATE;
  }
  return async_queue_packet(process_shared_dispatcher_, receiver, data);
}

zx_status_t Dispatcher::BindIrq(async_irq_t* irq) {
  fbl::AutoLock lock(&callback_lock_);
  if (!IsRunningLocked()) {
    return ZX_ERR_BAD_STATE;
  }
  return async_bind_irq(process_shared_dispatcher_, irq);
}

zx_status_t Dispatcher::UnbindIrq(async_irq_t* irq) {
  return async_unbind_irq(process_shared_dispatcher_, irq);
}

std::unique_ptr<driver_runtime::CallbackRequest> Dispatcher::RegisterCallbackWithoutQueueing(
    std::unique_ptr<driver_runtime::CallbackRequest> callback_request) {
  fbl::AutoLock lock(&callback_lock_);
  if (!IsRunningLocked()) {
    return callback_request;
  }
  registered_callbacks_.push_back(std::move(callback_request));
  return nullptr;
}

void Dispatcher::QueueRegisteredCallback(driver_runtime::CallbackRequest* request,
                                         fdf_status_t callback_reason) {
  ZX_ASSERT(request);

  auto idle_check = fit::defer([this]() {
    fbl::AutoLock lock(&callback_lock_);
    ZX_ASSERT(num_active_threads_ > 0);
    num_active_threads_--;
    IdleCheckLocked();
  });

  // Whether we want to call the callback now, or queue it to be run on the async loop.
  bool direct_call = false;
  std::unique_ptr<driver_runtime::CallbackRequest> callback_request;
  {
    fbl::AutoLock lock(&callback_lock_);
    num_active_threads_++;
    if (!IsRunningLocked()) {
      return;
    }

    // Finding the callback request may fail if the request was cancelled in the meanwhile.
    // This is possible if the channel was about to queue the registered callback (in response
    // to a channel write or a peer channel closing), but the client cancelled the callback.
    if (!request->InContainer()) {
      return;
    }

    callback_request = registered_callbacks_.erase(*request);
    // The callback request should only be removed if queued, or on shutting down,
    // which we checked earlier.
    ZX_ASSERT(callback_request != nullptr);
    callback_request->SetCallbackReason(callback_reason);

    // Synchronous dispatchers do not allow parallel callbacks.
    // Blocking dispatchers are required to queue all callbacks onto the async loop.
    if (unsynchronized_ || (!dispatching_sync_ && !allow_sync_calls_)) {
      // Check if the call would be reentrant, in which case we will queue it up to be run
      // later.
      //
      // If it is unknown which driver is calling this function, it is considered
      // to be potentially reentrant.
      // The call stack may be empty if the user writes to a channel, or registers a
      // read callback on a thread not managed by the driver runtime.
      if (!driver_context::IsCallStackEmpty() && !driver_context::IsDriverInCallStack(owner_)) {
        direct_call = true;
        dispatching_sync_ = true;
      }
    }
    if (!direct_call) {
      callback_queue_.push_back(std::move(callback_request));
      if (event_waiter_ && !event_waiter_->signaled()) {
        event_waiter_->signal();
      }
      return;
    }
  }
  DispatchCallback(std::move(callback_request));

  fbl::AutoLock lock(&callback_lock_);
  dispatching_sync_ = false;
  if (!callback_queue_.is_empty() && event_waiter_ && !event_waiter_->signaled() &&
      IsRunningLocked()) {
    event_waiter_->signal();
  }
}

void Dispatcher::AddWaitLocked(std::unique_ptr<Dispatcher::AsyncWait> wait) {
  ZX_DEBUG_ASSERT(!fbl::InContainer<AsyncWaitTag>(*wait));
  waits_.push_back(std::move(wait));
}

std::unique_ptr<Dispatcher::AsyncWait> Dispatcher::RemoveWait(Dispatcher::AsyncWait* wait) {
  fbl::AutoLock al(&callback_lock_);
  return RemoveWaitLocked(wait);
}

std::unique_ptr<Dispatcher::AsyncWait> Dispatcher::RemoveWaitLocked(Dispatcher::AsyncWait* wait) {
  ZX_DEBUG_ASSERT(fbl::InContainer<AsyncWaitTag>(*wait));
  auto ret = waits_.erase(*wait);
  IdleCheckLocked();
  return ret;
}

void Dispatcher::QueueWait(Dispatcher::AsyncWait* wait, zx_status_t status) {
  fbl::AutoLock al(&callback_lock_);
  ZX_DEBUG_ASSERT(fbl::InContainer<AsyncWaitTag>(*wait));
  if (!IsRunningLocked()) {
    // We are waiting for all outstanding waits to be completed. They will be serviced in
    // CompleteDestroy.
    shutdown_queue_.push_back(waits_.erase(*wait));
    IdleCheckLocked();
  } else {
    registered_callbacks_.push_back(waits_.erase(*wait));
    al.release();
    QueueRegisteredCallback(wait, status);
  }
}

std::unique_ptr<CallbackRequest> Dispatcher::CancelCallback(CallbackRequest& request_to_cancel) {
  fbl::AutoLock lock(&callback_lock_);

  // The request can be in |registered_callbacks_|, |callback_queue_| or |shutdown_queue_|.
  if (request_to_cancel.InContainer()) {
    return request_to_cancel.RemoveFromContainer();
  }
  return nullptr;
}

bool Dispatcher::SetCallbackReason(CallbackRequest* callback_to_update,
                                   fdf_status_t callback_reason) {
  fbl::AutoLock lock(&callback_lock_);
  auto iter = callback_queue_.find_if(
      [callback_to_update](auto& callback) -> bool { return &callback == callback_to_update; });
  if (iter == callback_queue_.end()) {
    return false;
  }
  callback_to_update->SetCallbackReason(callback_reason);
  return true;
}

std::unique_ptr<CallbackRequest> Dispatcher::CancelAsyncOperation(void* operation) {
  fbl::AutoLock lock(&callback_lock_);
  auto iter = callback_queue_.erase_if([operation](const CallbackRequest& callback_request) {
    return callback_request.holds_async_operation(operation);
  });
  if (iter) {
    return iter;
  }
  iter = delayed_tasks_.erase_if([operation](const CallbackRequest& callback_request) {
    return callback_request.holds_async_operation(operation);
  });
  if (iter) {
    ResetTimerLocked();
  }
  return iter;
}

void Dispatcher::DispatchCallback(
    std::unique_ptr<driver_runtime::CallbackRequest> callback_request) {
  driver_context::PushDriver(owner_, this);
  auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });

  callback_request->Call(std::move(callback_request), ZX_OK);
}

void Dispatcher::DispatchCallbacks(std::unique_ptr<EventWaiter> event_waiter,
                                   fbl::RefPtr<Dispatcher> dispatcher_ref) {
  ZX_ASSERT(dispatcher_ref != nullptr);

  auto defer = fit::defer([&]() {
    fbl::AutoLock lock(&callback_lock_);

    if (event_waiter) {
      // We call |BeginWaitWithRef| even when shutting down so that the |event_waiter|
      // stays alive until the dispatcher is destroyed. This allows |IsIdleLocked| to
      // correctly check the state of the event waiter. |CompleteShutdown| will cancel
      // and drop the event waiter.
      zx_status_t status = event_waiter->BeginWaitWithRef(std::move(event_waiter), dispatcher_ref);
      if (status == ZX_ERR_BAD_STATE) {
        event_waiter_ = nullptr;
      }
    }
    ZX_ASSERT(num_active_threads_ > 0);
    num_active_threads_--;
    IdleCheckLocked();
  });

  fbl::DoublyLinkedList<std::unique_ptr<CallbackRequest>> to_call;
  {
    fbl::AutoLock lock(&callback_lock_);
    num_active_threads_++;

    // Parallel callbacks are not allowed in synchronized dispatchers.
    // We should not be scheduled to run on two different dispatcher threads,
    // but it's possible we could still get here if we are currently doing a
    // direct call into the driver. In this case, we should designal the event
    // waiter, and once the direct call completes it will signal it again.
    if ((!unsynchronized_ && dispatching_sync_) || !IsRunningLocked()) {
      event_waiter->designal();
      return;
    }
    dispatching_sync_ = true;

    // For synchronized dispatchers, cancellation of ChannelReads are guaranteed to succeed.
    // Since cancellation may be called from the ChannelRead, or from another async operation
    // (like a task), we need to make sure that if we are calling an async operation
    // that is the only callback request pulled from the callback queue.
    // This will guarantee that cancellation will always succeed without having to lock
    // |to_call|.
    bool has_async_op = false;
    uint32_t n = 0;
    while ((n < kBatchSize) && !callback_queue_.is_empty() && !has_async_op) {
      std::unique_ptr<CallbackRequest> callback_request = callback_queue_.pop_front();
      ZX_ASSERT(callback_request);
      has_async_op = !unsynchronized_ && callback_request->has_async_operation();
      // For synchronized dispatchers, an async operation should be the only member in
      // |to_call|.
      if (has_async_op && n > 0) {
        callback_queue_.push_front(std::move(callback_request));
        break;
      }
      to_call.push_back(std::move(callback_request));
      n++;
    }
    // Check if there are callbacks left to process and we should wake up an additional
    // thread. For synchronized dispatchers, parallel callbacks are disallowed.
    if (unsynchronized_ && !callback_queue_.is_empty()) {
      zx_status_t status = event_waiter->BeginWaitWithRef(std::move(event_waiter), dispatcher_ref);
      if (status == ZX_ERR_BAD_STATE) {
        event_waiter_ = nullptr;
      }
    }
  }

  // Call the callbacks outside of the lock.
  while (!to_call.is_empty()) {
    auto callback_request = to_call.pop_front();
    ZX_ASSERT(callback_request);
    DispatchCallback(std::move(callback_request));
  }

  {
    fbl::AutoLock lock(&callback_lock_);
    // If we woke up an additional thread, that thread will update the
    // event waiter signals as necessary.
    if (!event_waiter) {
      return;
    }
    dispatching_sync_ = false;
    ResetTimerLocked();
    if (callback_queue_.is_empty() && event_waiter->signaled()) {
      event_waiter->designal();
    }
  }
}

zx::status<zx::event> Dispatcher::RegisterForCompleteShutdownEvent() {
  fbl::AutoLock lock_(&callback_lock_);
  auto event = complete_shutdown_event_manager_.GetEvent();
  if (event.is_error()) {
    return event;
  }
  if (IsIdleLocked() && !HasFutureOpsScheduledLocked()) {
    zx_status_t status = complete_shutdown_event_manager_.Signal();
    if (status != ZX_OK) {
      return zx::error(status);
    }
  }
  return event;
}

void Dispatcher::WaitUntilIdle() {
  ZX_ASSERT(!IsRuntimeManagedThread());

  fbl::AutoLock lock_(&callback_lock_);
  if (IsIdleLocked()) {
    return;
  }
  idle_event_.Wait(&callback_lock_);
  return;
}

bool Dispatcher::IsIdleLocked() {
  // If the event waiter was signaled, the thread will be scheduled to run soon.
  return (num_active_threads_ == 0) && callback_queue_.is_empty() &&
         (!event_waiter_ || !event_waiter_->signaled());
}

bool Dispatcher::HasFutureOpsScheduledLocked() { return !waits_.is_empty() || timer_.is_armed(); }

void Dispatcher::IdleCheckLocked() {
  if (IsIdleLocked()) {
    idle_event_.Broadcast();
    if (!HasFutureOpsScheduledLocked()) {
      complete_shutdown_event_manager_.Signal();
    }
  }
}

bool Dispatcher::HasQueuedTasks() {
  fbl::AutoLock lock(&callback_lock_);

  for (auto& callback_request : callback_queue_) {
    if (callback_request.request_type() == CallbackRequest::RequestType::kTask) {
      return true;
    }
  }
  return false;
}

void Dispatcher::EventWaiter::HandleEvent(std::unique_ptr<EventWaiter> event_waiter,
                                          async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                          zx_status_t status, const zx_packet_signal_t* signal) {
  if (status == ZX_ERR_CANCELED) {
    LOGF(TRACE, "Dispatcher: event waiter shutting down\n");
    event_waiter->dispatcher_ref_->event_waiter_ = nullptr;
    event_waiter->dispatcher_ref_ = nullptr;
    return;
  } else if (status != ZX_OK) {
    LOGF(ERROR, "Dispatcher: event waiter error: %d\n", status);
    event_waiter->dispatcher_ref_->event_waiter_ = nullptr;
    event_waiter->dispatcher_ref_ = nullptr;
    return;
  }

  if (signal->observed & ZX_USER_SIGNAL_0) {
    // The callback is in charge of calling |BeginWaitWithRef| on the event waiter.
    fbl::RefPtr<Dispatcher> dispatcher_ref = std::move(event_waiter->dispatcher_ref_);
    event_waiter->InvokeCallback(std::move(event_waiter), dispatcher_ref);
  } else {
    LOGF(ERROR, "Dispatcher: event waiter got unexpected signals: %x\n", signal->observed);
  }
}

// static
zx_status_t Dispatcher::EventWaiter::BeginWaitWithRef(std::unique_ptr<EventWaiter> event,
                                                      fbl::RefPtr<Dispatcher> dispatcher) {
  ZX_ASSERT(dispatcher != nullptr);
  event->dispatcher_ref_ = dispatcher;
  return BeginWait(std::move(event), dispatcher->process_shared_dispatcher_);
}

zx::status<zx::event> Dispatcher::CompleteShutdownEventManager::GetEvent() {
  if (!event_.is_valid()) {
    // If this is the first waiter to register, we need to create the
    // idle event manager's event.
    zx_status_t status = zx::event::create(0, &event_);
    if (status != ZX_OK) {
      return zx::error(status);
    }
  }
  zx::event dup;
  zx_status_t status = event_.duplicate(ZX_RIGHTS_BASIC, &dup);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(dup));
}

zx_status_t Dispatcher::CompleteShutdownEventManager::Signal() {
  if (!event_.is_valid()) {
    return ZX_OK;  // No-one is waiting for idle events.
  }
  zx_status_t status = event_.signal(0u, ZX_EVENT_SIGNALED);
  event_.reset();
  return status;
}

// static
void DispatcherCoordinator::WaitUntilDispatchersIdle() {
  std::vector<fbl::RefPtr<Dispatcher>> dispatchers;
  {
    fbl::AutoLock lock(&(GetDispatcherCoordinator().lock_));
    for (auto& driver : GetDispatcherCoordinator().drivers_) {
      driver.GetDispatchers(dispatchers);
    }
  }
  for (auto& d : dispatchers) {
    d->WaitUntilIdle();
  }
}

// static
void DispatcherCoordinator::WaitUntilDispatchersDestroyed() {
  auto& coordinator = GetDispatcherCoordinator();
  fbl::AutoLock lock(&coordinator.lock_);
  if (coordinator.drivers_.size() == 0) {
    return;
  }
  coordinator.drivers_destroyed_event_.Wait(&coordinator.lock_);
}

// static
fdf_status_t DispatcherCoordinator::ShutdownDispatchersAsync(
    const void* driver, fdf_internal_driver_shutdown_observer_t* observer) {
  std::vector<fbl::RefPtr<Dispatcher>> dispatchers;

  {
    fbl::AutoLock lock(&(GetDispatcherCoordinator().lock_));
    auto driver_state = GetDispatcherCoordinator().drivers_.find(driver);
    if (!driver_state.IsValid()) {
      return ZX_ERR_INVALID_ARGS;
    }
    driver_state->GetDispatchers(dispatchers);
    if (!dispatchers.empty()) {
      auto status = driver_state->SetShutdownObserver(observer);
      if (status != ZX_OK) {
        return status;
      }
    }
  }
  for (auto& dispatcher : dispatchers) {
    dispatcher->ShutdownAsync();
  }
  if (dispatchers.empty()) {
    // The dispatchers have already been shutdown and no calls to |NotifyDispatcherShutdown|
    // will occur, so we need to schedule the handler to be called.
    async::PostTask(GetDispatcherCoordinator().loop()->dispatcher(),
                    [driver, observer]() { observer->handler(driver, observer); });
  }
  return ZX_OK;
}

// static
void DispatcherCoordinator::DestroyAllDispatchers() {
  std::vector<fbl::RefPtr<Dispatcher>> dispatchers;
  {
    fbl::AutoLock lock(&(GetDispatcherCoordinator().lock_));

    for (auto& driver_state : GetDispatcherCoordinator().drivers_) {
      // We should have already shutdown all dispatchers.
      ZX_ASSERT(driver_state.CompletedShutdown());
      driver_state.GetShutdownDispatchers(dispatchers);
    }
  }

  for (auto& dispatcher : dispatchers) {
    dispatcher->Destroy();
  }
}

fdf_status_t DispatcherCoordinator::AddDispatcher(fbl::RefPtr<Dispatcher> dispatcher) {
  fbl::AutoLock lock(&lock_);

  // Check if we already have a driver state object.
  auto driver_state = drivers_.find(dispatcher->owner());
  if (driver_state == drivers_.end()) {
    auto new_driver_state = std::make_unique<DriverState>(dispatcher->owner());
    drivers_.insert(std::move(new_driver_state));
    driver_state = drivers_.find(dispatcher->owner());
  } else {
    // If the driver is shutting down, we should not allow creating new dispatchers.
    if (driver_state->IsShuttingDown()) {
      return ZX_ERR_BAD_STATE;
    }
  }
  driver_state->AddDispatcher(dispatcher);
  return ZX_OK;
}

void DispatcherCoordinator::SetShutdown(Dispatcher& dispatcher) {
  fbl::AutoLock lock(&lock_);

  auto driver_state = drivers_.find(dispatcher.owner());
  ZX_ASSERT(driver_state != drivers_.end());
  driver_state->SetDispatcherShutdown(dispatcher);
}

void DispatcherCoordinator::NotifyShutdown(Dispatcher& dispatcher) {
  fdf_internal_driver_shutdown_observer_t* observer = nullptr;
  {
    fbl::AutoLock lock(&lock_);

    auto driver_state = drivers_.find(dispatcher.owner());
    if ((driver_state == drivers_.end()) || !driver_state->CompletedShutdown()) {
      return;
    }

    observer = driver_state->shutdown_observer();
    if (!observer) {
      // No one to notify. The driver state will be removed once all the dispatchers
      // are destroyed.
      return;
    }
    // We should not clear |observer| from the driver state yet, as that would make
    // it appear to other threads checking the driver state that the shutdown
    // observer had already completed.
  }

  observer->handler(dispatcher.owner(), observer);

  fbl::AutoLock lock(&lock_);

  // Since the driver state had a shutdown observer set, the driver state should
  // not have been removed from drivers_ in the meanwhile.
  auto driver_state = drivers_.find(dispatcher.owner());
  ZX_ASSERT(driver_state != drivers_.end());

  driver_state->ClearShutdownObserver();
  // If the driver has completely shutdown, and all dispatchers have been destroyed,
  // the driver state can also be destroyed.
  if (!driver_state->HasDispatchers() && !driver_state->IsShuttingDown()) {
    drivers_.erase(driver_state);
  }
  if (drivers_.size() == 0) {
    drivers_destroyed_event_.Broadcast();
  }
}

void DispatcherCoordinator::RemoveDispatcher(Dispatcher& dispatcher) {
  fbl::AutoLock lock(&lock_);

  auto driver_state = drivers_.find(dispatcher.owner());
  ZX_ASSERT(driver_state != drivers_.end());

  driver_state->RemoveDispatcher(dispatcher);
  // If the driver has completely shutdown, and all dispatchers have been destroyed,
  // the driver state can also be destroyed.
  if (!driver_state->HasDispatchers() && !driver_state->IsShuttingDown()) {
    drivers_.erase(driver_state);
  }
  if (drivers_.size() == 0) {
    drivers_destroyed_event_.Broadcast();
  }
}

}  // namespace driver_runtime

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dispatcher.h"

#include <lib/async/irq.h>
#include <lib/async/receiver.h>
#include <lib/async/task.h>
#include <lib/async/trap.h>
#include <lib/ddk/device.h>
#include <lib/fdf/dispatcher.h>
#include <lib/fit/defer.h>
#include <lib/sync/completion.h>
#include <stdlib.h>
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/listnode.h>
#include <zircon/syscalls.h>

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

ProcessSharedLoop& GetProcessSharedLoop() {
  static ProcessSharedLoop shared_loop;
  return shared_loop;
}

Dispatcher::Dispatcher(uint32_t options, bool unsynchronized, bool allow_sync_calls,
                       const void* owner, async_dispatcher_t* process_shared_dispatcher)
    : async_dispatcher_t{&g_dispatcher_ops},
      options_(options),
      unsynchronized_(unsynchronized),
      allow_sync_calls_(allow_sync_calls),
      owner_(owner),
      process_shared_dispatcher_(process_shared_dispatcher) {}

// static
fdf_status_t Dispatcher::CreateWithLoop(uint32_t options, const char* scheduler_role,
                                        size_t scheduler_role_len, const void* owner,
                                        async::Loop* loop, Dispatcher** out_dispatcher) {
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
    zx_status_t status = loop->StartThread();
    if (status != ZX_OK) {
      return status;
    }
  }

  auto dispatcher = fbl::MakeRefCounted<Dispatcher>(options, unsynchronized, allow_sync_calls,
                                                    owner, loop->dispatcher());

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
  EventWaiter::BeginWaitWithRef(std::move(event_waiter), dispatcher);

  // This reference will be recovered in |Destroy|.
  *out_dispatcher = fbl::ExportToRawPtr(&dispatcher);
  return ZX_OK;
}

// fdf_dispatcher_t implementation

// static
fdf_status_t Dispatcher::Create(uint32_t options, const char* scheduler_role,
                                size_t scheduler_role_len, Dispatcher** out_dispatcher) {
  return CreateWithLoop(options, scheduler_role, scheduler_role_len,
                        driver_context::GetCurrentDriver(), GetProcessSharedLoop().loop(),
                        out_dispatcher);
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

void Dispatcher::Destroy() {
  {
    fbl::AutoLock lock(&callback_lock_);

    shutting_down_ = true;

    // Since the event waiter holds a reference to the dispatcher,
    // we need to try cancelling it to reclaim it. This may fail if
    // the event waiter is currently dispatching a callback, in which
    // case the callback will own (and be in charge of droppping) the
    // reference.
    event_waiter_->Cancel();

    // TODO(fxbug.dev/87840): implement this.
  }

  // Recover the reference created in |CreateWithLoop|.
  __UNUSED auto ref = fbl::ImportFromRawPtr(this);
}

// async_dispatcher_t implementation

zx_time_t Dispatcher::GetTime() { return zx_clock_get_monotonic(); }

zx_status_t Dispatcher::BeginWait(async_wait_t* wait) {
  fbl::AutoLock lock(&callback_lock_);
  if (shutting_down_) {
    return ZX_ERR_BAD_STATE;
  }
  return async_begin_wait(process_shared_dispatcher_, wait);
}

zx_status_t Dispatcher::CancelWait(async_wait_t* wait) {
  return async_cancel_wait(process_shared_dispatcher_, wait);
}

zx_status_t Dispatcher::PostTask(async_task_t* task) {
  // TODO(92740): we should do something more efficient rather than creating a new
  // callback request each time.
  auto callback_request = std::make_unique<driver_runtime::CallbackRequest>();
  driver_runtime::Callback callback =
      [this, task](std::unique_ptr<driver_runtime::CallbackRequest> callback_request,
                   fdf_status_t status) { task->handler(this, task, status); };
  callback_request->SetCallback(static_cast<fdf_dispatcher_t*>(this), std::move(callback), task);
  CallbackRequest* callback_ptr = callback_request.get();
  // TODO(92878): handle task deadlines.
  callback_request = RegisterCallbackWithoutQueueing(std::move(callback_request));
  // Dispatcher returned callback request as queueing failed.
  if (callback_request) {
    return ZX_ERR_BAD_STATE;
  }
  QueueRegisteredCallback(callback_ptr, ZX_OK);
  return ZX_OK;
}

zx_status_t Dispatcher::CancelTask(async_task_t* task) {
  auto callback_request = CancelAsyncOperation(task);
  return callback_request ? ZX_OK : ZX_ERR_NOT_FOUND;
}

zx_status_t Dispatcher::QueuePacket(async_receiver_t* receiver, const zx_packet_user_t* data) {
  fbl::AutoLock lock(&callback_lock_);
  if (shutting_down_) {
    return ZX_ERR_BAD_STATE;
  }
  return async_queue_packet(process_shared_dispatcher_, receiver, data);
}

zx_status_t Dispatcher::BindIrq(async_irq_t* irq) {
  fbl::AutoLock lock(&callback_lock_);
  if (shutting_down_) {
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
  if (shutting_down_) {
    return callback_request;
  }
  registered_callbacks_.push_back(std::move(callback_request));
  return nullptr;
}

void Dispatcher::QueueRegisteredCallback(driver_runtime::CallbackRequest* request,
                                         fdf_status_t callback_reason) {
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
    if (shutting_down_) {
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
      // Check if the call would be reentrant, in which case we will queue it up to be run later.
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
      if (!event_waiter_->signaled()) {
        event_waiter_->signal();
      }
      return;
    }
  }
  DispatchCallback(std::move(callback_request));

  fbl::AutoLock lock(&callback_lock_);
  dispatching_sync_ = false;
  if (!callback_queue_.is_empty() && !event_waiter_->signaled() && !shutting_down_) {
    event_waiter_->signal();
  }
}

std::unique_ptr<CallbackRequest> Dispatcher::CancelCallback(CallbackRequest& callback_request) {
  fbl::AutoLock lock(&callback_lock_);
  auto req = callback_queue_.erase(callback_request);
  if (req) {
    return req;
  }
  return registered_callbacks_.erase(callback_request);
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
  return callback_queue_.erase_if([operation](const CallbackRequest& callback_request) {
    return callback_request.holds_async_operation(operation);
  });
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

  bool event_begin_wait = true;
  auto defer = fit::defer([&]() {
    fbl::AutoLock lock(&callback_lock_);

    if (event_waiter && !shutting_down_ && event_begin_wait) {
      event_waiter->BeginWaitWithRef(std::move(event_waiter), dispatcher_ref);
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
    if (!unsynchronized_ && dispatching_sync_) {
      event_waiter_->designal();
      return;
    }
    dispatching_sync_ = true;

    // For synchronized dispatchers, cancellation of ChannelReads are guaranteed to succeed.
    // Since cancellation may be called from the ChannelRead, or from another async operation
    // (like a task), we need to make sure that if we are calling an async operation
    // that is the only callback request pulled from the callback queue.
    // This will guarantee that cancellation will always succeed without having to lock |to_call|.
    bool has_async_op = false;
    uint32_t n = 0;
    while ((n < kBatchSize) && !callback_queue_.is_empty() && !has_async_op) {
      std::unique_ptr<CallbackRequest> callback_request = callback_queue_.pop_front();
      ZX_ASSERT(callback_request);
      has_async_op = !unsynchronized_ && callback_request->has_async_operation();
      // For synchronized dispatchers, an async operation should be the only member in |to_call|.
      if (has_async_op && n > 0) {
        callback_queue_.push_front(std::move(callback_request));
        break;
      }
      to_call.push_back(std::move(callback_request));
      n++;
    }
    // Check if there are callbacks left to process and we should wake up an additional thread.
    // For synchronized dispatchers, parallel callbacks are disallowed.
    if (unsynchronized_ && !callback_queue_.is_empty()) {
      event_waiter->BeginWaitWithRef(std::move(event_waiter), dispatcher_ref);
      event_begin_wait = false;
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
    if (callback_queue_.is_empty() && event_waiter->signaled()) {
      event_waiter_->designal();
    }
  }
}

zx::status<zx::event> Dispatcher::RegisterForIdleEvent() {
  fbl::AutoLock lock_(&callback_lock_);
  auto event = idle_event_manager_.GetIdleEvent();
  if (event.is_error()) {
    return event;
  }
  if (IsIdleLocked()) {
    zx_status_t status = idle_event_manager_.Signal();
    if (status != ZX_OK) {
      return zx::error(status);
    }
  }
  return event;
}

fdf_status_t Dispatcher::WaitUntilIdle() {
  ZX_ASSERT(!IsRuntimeManagedThread());
  auto event = RegisterForIdleEvent();
  if (event.is_error()) {
    return event.status_value();
  }
  return event->wait_one(ZX_EVENT_SIGNALED, zx::time::infinite(), nullptr);
}

bool Dispatcher::IsIdleLocked() { return (num_active_threads_ == 0) && callback_queue_.is_empty(); }

void Dispatcher::IdleCheckLocked() {
  if (IsIdleLocked()) {
    idle_event_manager_.Signal();
  }
}

void Dispatcher::EventWaiter::HandleEvent(std::unique_ptr<EventWaiter> event_waiter,
                                          async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                          zx_status_t status, const zx_packet_signal_t* signal) {
  if (status == ZX_ERR_CANCELED) {
    LOGF(TRACE, "Dispatcher: event waiter shutting down\n");
    event_waiter->dispatcher_ref_ = nullptr;
    return;
  } else if (status != ZX_OK) {
    LOGF(ERROR, "Dispatcher: event waiter error: %d\n", status);
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

zx::status<zx::event> Dispatcher::IdleEventManager::GetIdleEvent() {
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

zx_status_t Dispatcher::IdleEventManager::Signal() {
  if (!event_.is_valid()) {
    return ZX_OK;  // No-one is waiting for idle events.
  }
  zx_status_t status = event_.signal(0u, ZX_EVENT_SIGNALED);
  event_.reset();
  return status;
}

}  // namespace driver_runtime

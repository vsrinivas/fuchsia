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

Dispatcher::Dispatcher(bool unsynchronized, bool allow_sync_calls, const void* owner,
                       async_dispatcher_t* process_shared_dispatcher)
    : async_dispatcher_t{&g_dispatcher_ops},
      unsynchronized_(unsynchronized),
      allow_sync_calls_(allow_sync_calls),
      owner_(owner),
      process_shared_dispatcher_(process_shared_dispatcher) {}

// static
fdf_status_t Dispatcher::CreateWithLoop(uint32_t options, const char* scheduler_role,
                                        size_t scheduler_role_len, const void* owner,
                                        async::Loop* loop,
                                        std::unique_ptr<Dispatcher>* out_dispatcher) {
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

  auto dispatcher =
      std::make_unique<Dispatcher>(unsynchronized, allow_sync_calls, owner, loop->dispatcher());

  zx::event event;
  if (zx_status_t status = zx::event::create(0, &event); status != ZX_OK) {
    return status;
  }

  auto self = dispatcher.get();
  auto event_waiter = std::make_unique<EventWaiter>(
      std::move(event), [self](std::unique_ptr<EventWaiter> event_waiter) {
        self->DispatchCallbacks(std::move(event_waiter));
      });
  dispatcher->event_waiter_ = event_waiter.get();
  EventWaiter::BeginWait(std::move(event_waiter), loop->dispatcher());

  *out_dispatcher = std::move(dispatcher);
  return ZX_OK;
}

// fdf_dispatcher_t implementation

// static
fdf_status_t Dispatcher::Create(uint32_t options, const char* scheduler_role,
                                size_t scheduler_role_len,
                                std::unique_ptr<Dispatcher>* out_dispatcher) {
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
    // TODO(fxbug.dev/87840): implement this.
  }
  delete this;
}

// async_dispatcher_t implementation

zx_time_t Dispatcher::GetTime() { return zx_clock_get_monotonic(); }

zx_status_t Dispatcher::BeginWait(async_wait_t* wait) {
  return async_begin_wait(process_shared_dispatcher_, wait);
}

zx_status_t Dispatcher::CancelWait(async_wait_t* wait) {
  return async_cancel_wait(process_shared_dispatcher_, wait);
}

zx_status_t Dispatcher::PostTask(async_task_t* task) {
  return async_post_task(process_shared_dispatcher_, task);
}

zx_status_t Dispatcher::CancelTask(async_task_t* task) {
  return async_cancel_task(process_shared_dispatcher_, task);
}

zx_status_t Dispatcher::QueuePacket(async_receiver_t* receiver, const zx_packet_user_t* data) {
  return async_queue_packet(process_shared_dispatcher_, receiver, data);
}

zx_status_t Dispatcher::BindIrq(async_irq_t* irq) {
  return async_bind_irq(process_shared_dispatcher_, irq);
}

zx_status_t Dispatcher::UnbindIrq(async_irq_t* irq) {
  return async_unbind_irq(process_shared_dispatcher_, irq);
}

void Dispatcher::QueueCallback(std::unique_ptr<driver_runtime::CallbackRequest> callback_request) {
  // Whether we want to call the callback now, or queue it to be run on the async loop.
  bool direct_call = false;
  {
    fbl::AutoLock lock(&callback_lock_);
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
  }
  if (direct_call) {
    DispatchCallback(std::move(callback_request));

    fbl::AutoLock lock(&callback_lock_);
    dispatching_sync_ = false;
    if (!callback_queue_.is_empty() && !event_waiter_->signaled()) {
      event_waiter_->signal();
    }
    return;
  }

  fbl::AutoLock lock(&callback_lock_);
  callback_queue_.push_back(std::move(callback_request));
  if (!event_waiter_->signaled()) {
    event_waiter_->signal();
  }
}

std::unique_ptr<CallbackRequest> Dispatcher::CancelCallback(CallbackRequest& callback_request) {
  fbl::AutoLock lock(&callback_lock_);
  return callback_queue_.erase(callback_request);
}

void Dispatcher::DispatchCallback(
    std::unique_ptr<driver_runtime::CallbackRequest> callback_request) {
  driver_context::PushDriver(owner_, this);
  auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });

  callback_request->Call(std::move(callback_request), ZX_OK);
}

void Dispatcher::DispatchCallbacks(std::unique_ptr<EventWaiter> event_waiter) {
  auto event_begin_wait = fit::defer([&]() {
    if (event_waiter) {
      event_waiter->BeginWait(std::move(event_waiter), this);
    }
  });

  fbl::DoublyLinkedList<std::unique_ptr<CallbackRequest>> to_call;
  {
    fbl::AutoLock lock(&callback_lock_);

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

    uint32_t n = 0;
    while ((n < kBatchSize) && !callback_queue_.is_empty()) {
      std::unique_ptr<CallbackRequest> callback_request = callback_queue_.pop_front();
      ZX_ASSERT(callback_request);
      to_call.push_back(std::move(callback_request));
      n++;
    }
    // Check if there are callbacks left to process and we should wake up an additional thread.
    // For synchronized dispatchers, parallel callbacks are disallowed.
    if (unsynchronized_ && !callback_queue_.is_empty()) {
      event_waiter->BeginWait(std::move(event_waiter), this);
      event_begin_wait.cancel();
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

void Dispatcher::EventWaiter::HandleEvent(std::unique_ptr<EventWaiter> event_waiter,
                                          async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                          zx_status_t status, const zx_packet_signal_t* signal) {
  if (status == ZX_ERR_CANCELED) {
    LOGF(TRACE, "Dispatcher: event waiter shutting down\n");
    return;
  } else if (status != ZX_OK) {
    LOGF(ERROR, "Dispatcher: event waiter error: %d\n", status);
    return;
  }

  if (signal->observed & ZX_USER_SIGNAL_0) {
    // The callback is in charge of calling |BeginWait| on the event waiter.
    event_waiter->InvokeCallback(std::move(event_waiter));
  } else {
    LOGF(ERROR, "Dispatcher: event waiter got unexpected signals: %x\n", signal->observed);
  }
}

}  // namespace driver_runtime

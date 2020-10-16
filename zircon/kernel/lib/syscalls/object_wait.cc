// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <lib/ktrace.h>
#include <lib/user_copy/user_ptr.h>
#include <platform.h>
#include <trace.h>
#include <zircon/types.h>

#include <fbl/inline_array.h>
#include <fbl/ref_ptr.h>
#include <kernel/event.h>
#include <kernel/lockdep.h>
#include <kernel/thread.h>
#include <object/dispatcher.h>
#include <object/handle.h>
#include <object/port_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/wait_signal_observer.h>

#include "priv.h"

#define LOCAL_TRACE 0

constexpr uint32_t kMaxWaitHandleCount = ZX_WAIT_MANY_MAX_ITEMS;

static_assert(ZX_WAIT_MANY_MAX_ITEMS == ZX_CHANNEL_MAX_MSG_HANDLES);

// zx_status_t zx_object_wait_one
zx_status_t sys_object_wait_one(zx_handle_t handle_value, zx_signals_t signals, zx_time_t deadline,
                                user_out_ptr<zx_signals_t> observed) {
  LTRACEF("handle %x\n", handle_value);

  Event event;

  zx_status_t result;
  WaitSignalObserver wait_signal_observer;
  uint32_t koid;

  auto up = ProcessDispatcher::GetCurrent();
  {
    Guard<BrwLockPi, BrwLockPi::Reader> guard{up->handle_table().handle_table_lock()};

    Handle* handle = up->handle_table().GetHandleLocked(handle_value);
    if (!handle)
      return ZX_ERR_BAD_HANDLE;
    if (!handle->HasRights(ZX_RIGHT_WAIT))
      return ZX_ERR_ACCESS_DENIED;

    result = wait_signal_observer.Begin(&event, handle, signals);
    if (result != ZX_OK)
      return result;

    koid = static_cast<uint32_t>(handle->dispatcher()->get_koid());
  }

  ktrace(TAG_WAIT_ONE, koid, signals, (uint32_t)deadline, (uint32_t)(deadline >> 32));

  const TimerSlack slack = up->GetTimerSlackPolicy();
  const Deadline slackDeadline(deadline, slack);

  // Event::Wait() will return ZX_OK if already signaled,
  // even if the deadline has passed.  It will return ZX_ERR_TIMED_OUT
  // after the deadline passes if the event has not been
  // signaled.
  {
    ThreadDispatcher::AutoBlocked by(ThreadDispatcher::Blocked::WAIT_ONE);
    result = event.Wait(slackDeadline);
  }

  // Regardless of wait outcome, we must call End().
  auto signals_state = wait_signal_observer.End();

  ktrace(TAG_WAIT_ONE_DONE, koid, signals_state, result, 0);

  if (observed) {
    zx_status_t status = observed.copy_to_user(signals_state);
    if (status != ZX_OK)
      return status;
  }

  if (signals_state & ZX_SIGNAL_HANDLE_CLOSED)
    return ZX_ERR_CANCELED;

  return result;
}

// zx_status_t zx_object_wait_many
zx_status_t sys_object_wait_many(user_inout_ptr<zx_wait_item_t> user_items, size_t count,
                                 zx_time_t deadline) {
  LTRACEF("count %zu\n", count);

  const auto up = ProcessDispatcher::GetCurrent();
  const Deadline slackDeadline(deadline, up->GetTimerSlackPolicy());

  if (!count) {
    const zx_time_t now = current_time();
    {
      ThreadDispatcher::AutoBlocked by(ThreadDispatcher::Blocked::WAIT_MANY);
      zx_status_t result = Thread::Current::SleepEtc(slackDeadline, Interruptible::Yes, now);
      if (result != ZX_OK) {
        return result;
      }
    }
    return ZX_ERR_TIMED_OUT;
  }

  if (count > kMaxWaitHandleCount)
    return ZX_ERR_OUT_OF_RANGE;

  zx_wait_item_t items[kMaxWaitHandleCount];
  if (user_items.copy_array_from_user(items, count) != ZX_OK)
    return ZX_ERR_INVALID_ARGS;

  // WaitSignalObserver is heavier than it looks so make sure we know how
  // much stack InlineArray is going to use, given limited kernel stack size.
  static_assert(sizeof(WaitSignalObserver) * 8 < 640);

  fbl::AllocChecker ac;
  fbl::InlineArray<WaitSignalObserver, 8u> wait_signal_observers(&ac, count);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  Event event;

  // We may need to unwind (which can be done outside the lock).
  zx_status_t result = ZX_OK;
  size_t num_added = 0;
  {
    Guard<BrwLockPi, BrwLockPi::Reader> guard{up->handle_table().handle_table_lock()};

    for (; num_added != count; ++num_added) {
      Handle* handle = up->handle_table().GetHandleLocked(items[num_added].handle);
      if (!handle) {
        result = ZX_ERR_BAD_HANDLE;
        break;
      }
      if (!handle->HasRights(ZX_RIGHT_WAIT)) {
        result = ZX_ERR_ACCESS_DENIED;
        break;
      }

      result = wait_signal_observers[num_added].Begin(&event, handle, items[num_added].waitfor);
      if (result != ZX_OK)
        break;
    }
  }
  if (result != ZX_OK) {
    for (size_t ix = 0; ix < num_added; ++ix)
      wait_signal_observers[ix].End();
    return result;
  }

  // Event::Wait() will return ZX_OK if already signaled,
  // even if deadline has passed.  It will return ZX_ERR_TIMED_OUT
  // after the deadline passes if the event has not been
  // signaled.
  {
    ThreadDispatcher::AutoBlocked by(ThreadDispatcher::Blocked::WAIT_MANY);
    result = event.Wait(slackDeadline);
  }

  // Regardless of wait outcome, we must call End().
  zx_signals_t combined = 0;
  for (size_t ix = 0; ix != count; ++ix) {
    combined |= (items[ix].pending = wait_signal_observers[ix].End());
  }

  if (user_items.copy_array_to_user(items, count) != ZX_OK)
    return ZX_ERR_INVALID_ARGS;

  if (combined & ZX_SIGNAL_HANDLE_CLOSED)
    return ZX_ERR_CANCELED;

  return result;
}

// zx_status_t zx_object_wait_async
zx_status_t sys_object_wait_async(zx_handle_t handle_value, zx_handle_t port_handle_value,
                                  uint64_t key, zx_signals_t signals, uint32_t options) {
  LTRACEF("handle %x\n", handle_value);

  if ((options != 0u) && (options != ZX_WAIT_ASYNC_TIMESTAMP)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Allocate space for a |PortObserver| before taking any locks.
  //
  // |PortDispatcher::MakeObserver| is responsible for constructing the |PortObserver|, however, it
  // must be called while holding the process's handle table lock and don't want to perform a
  // potentially blocking allocation while holding that lock.  Allocate a special placeholder that
  // we'll pass in to |MakeObserver|.
  fbl::AllocChecker ac;
  auto placeholder = ktl::make_unique<PortDispatcher::PortObserverPlaceholder>(&ac);

  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  auto up = ProcessDispatcher::GetCurrent();

  {
    Guard<BrwLockPi, BrwLockPi::Reader> guard{up->handle_table().handle_table_lock()};

    // Note, we're doing this all while holding the handle table lock for two reasons.
    //
    // First, this thread may be racing with another thread that's closing the last handle to
    // the port. By holding the lock we can ensure that this syscall behaves as if the port was
    // closed just *before* the syscall started or closed just *after* it has completed.
    //
    // Second, MakeObserver takes a Handle. By holding the lock we ensure the Handle isn't
    // destroyed out from under it.

    Handle* port_handle = up->handle_table().GetHandleLocked(port_handle_value);
    if (!port_handle) {
      return ZX_ERR_BAD_HANDLE;
    }
    fbl::RefPtr<Dispatcher> disp = port_handle->dispatcher();
    fbl::RefPtr<PortDispatcher> port = DownCastDispatcher<PortDispatcher>(&disp);
    if (!port) {
      return ZX_ERR_WRONG_TYPE;
    }
    if (!port_handle->HasRights(ZX_RIGHT_WRITE)) {
      return ZX_ERR_ACCESS_DENIED;
    }

    Handle* handle = up->handle_table().GetHandleLocked(handle_value);
    if (!handle) {
      return ZX_ERR_BAD_HANDLE;
    }
    if (!handle->HasRights(ZX_RIGHT_WAIT)) {
      return ZX_ERR_ACCESS_DENIED;
    }

    return port->MakeObserver(ktl::move(placeholder), options, handle, key, signals);
  }
}

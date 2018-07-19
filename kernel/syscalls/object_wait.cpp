// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <kernel/event.h>
#include <kernel/lockdep.h>
#include <kernel/thread.h>
#include <platform.h>

#include <lib/ktrace.h>
#include <lib/user_copy/user_ptr.h>

#include <object/handle.h>
#include <object/port_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/wait_state_observer.h>

#include <fbl/inline_array.h>
#include <fbl/ref_ptr.h>

#include <zircon/types.h>

#include "priv.h"

#define LOCAL_TRACE 0

// TODO(ZX-1349) Re-lower this to 8.
constexpr uint32_t kMaxWaitHandleCount = 16u;

// ensure public headers agree
static_assert(ZX_WAIT_MANY_MAX_ITEMS == kMaxWaitHandleCount, "");

zx_status_t sys_object_wait_one(zx_handle_t handle_value,
                                zx_signals_t signals,
                                zx_time_t deadline,
                                user_out_ptr<zx_signals_t> observed) {
    LTRACEF("handle %x\n", handle_value);

    Event event;

    zx_status_t result;
    WaitStateObserver wait_state_observer;

    auto up = ProcessDispatcher::GetCurrent();
    {
        Guard<fbl::Mutex> guard{up->handle_table_lock()};

        Handle* handle = up->GetHandleLocked(handle_value);
        if (!handle)
            return ZX_ERR_BAD_HANDLE;
        if (!handle->HasRights(ZX_RIGHT_WAIT))
            return ZX_ERR_ACCESS_DENIED;

        result = wait_state_observer.Begin(&event, handle, signals);
        if (result != ZX_OK)
            return result;
    }

#if WITH_LIB_KTRACE
    auto koid = static_cast<uint32_t>(up->GetKoidForHandle(handle_value));
    ktrace(TAG_WAIT_ONE, koid, signals, (uint32_t)deadline, (uint32_t)(deadline >> 32));
#endif

    // event_wait() will return ZX_OK if already signaled,
    // even if the deadline has passed.  It will return ZX_ERR_TIMED_OUT
    // after the deadline passes if the event has not been
    // signaled.
    {
        ThreadDispatcher::AutoBlocked by(ThreadDispatcher::Blocked::WAIT_ONE);
        result = event.Wait(deadline);
    }

    // Regardless of wait outcome, we must call End().
    auto signals_state = wait_state_observer.End();

#if WITH_LIB_KTRACE
    ktrace(TAG_WAIT_ONE_DONE, koid, signals_state, result, 0);
#endif

    if (observed) {
        zx_status_t status = observed.copy_to_user(signals_state);
        if (status != ZX_OK)
            return status;
    }

    if (signals_state & ZX_SIGNAL_HANDLE_CLOSED)
        return ZX_ERR_CANCELED;

    return result;
}

zx_status_t sys_object_wait_many(user_inout_ptr<zx_wait_item_t> user_items, size_t count, zx_time_t deadline) {
    LTRACEF("count %zu\n", count);

    if (!count) {
        zx_status_t result = thread_sleep_etc(deadline, /*interruptable=*/true);
        if (result != ZX_OK)
            return result;
        return ZX_ERR_TIMED_OUT;
    }

    if (count > kMaxWaitHandleCount)
        return ZX_ERR_OUT_OF_RANGE;

    zx_wait_item_t items[kMaxWaitHandleCount];
    if (user_items.copy_array_from_user(items, count) != ZX_OK)
        return ZX_ERR_INVALID_ARGS;

    WaitStateObserver wait_state_observers[kMaxWaitHandleCount];
    Event event;

    // We may need to unwind (which can be done outside the lock).
    zx_status_t result = ZX_OK;
    size_t num_added = 0;
    {
        auto up = ProcessDispatcher::GetCurrent();
        Guard<fbl::Mutex> guard{up->handle_table_lock()};

        for (; num_added != count; ++num_added) {
            Handle* handle = up->GetHandleLocked(items[num_added].handle);
            if (!handle) {
                result = ZX_ERR_BAD_HANDLE;
                break;
            }
            if (!handle->HasRights(ZX_RIGHT_WAIT)) {
                result = ZX_ERR_ACCESS_DENIED;
                break;
            }

            result = wait_state_observers[num_added].Begin(&event, handle, items[num_added].waitfor);
            if (result != ZX_OK)
                break;
        }
    }
    if (result != ZX_OK) {
        for (size_t ix = 0; ix < num_added; ++ix)
            wait_state_observers[ix].End();
        return result;
    }

    // event_wait() will return ZX_OK if already signaled,
    // even if deadline has passed.  It will return ZX_ERR_TIMED_OUT
    // after the deadline passes if the event has not been
    // signaled.
    {
        ThreadDispatcher::AutoBlocked by(ThreadDispatcher::Blocked::WAIT_MANY);
        result = event.Wait(deadline);
    }

    // Regardless of wait outcome, we must call End().
    zx_signals_t combined = 0;
    for (size_t ix = 0; ix != count; ++ix) {
        combined |= (items[ix].pending = wait_state_observers[ix].End());
    }

    if (user_items.copy_array_to_user(items, count) != ZX_OK)
        return ZX_ERR_INVALID_ARGS;

    if (combined & ZX_SIGNAL_HANDLE_CLOSED)
        return ZX_ERR_CANCELED;

    return result;
}

zx_status_t sys_object_wait_async(zx_handle_t handle_value, zx_handle_t port_handle,
                                  uint64_t key, zx_signals_t signals, uint32_t options) {
    LTRACEF("handle %x\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<PortDispatcher> port;
    auto status = up->GetDispatcherWithRights(port_handle, ZX_RIGHT_WRITE, &port);
    if (status != ZX_OK)
        return status;

    {
        Guard<fbl::Mutex> guard{up->handle_table_lock()};
        Handle* handle = up->GetHandleLocked(handle_value);
        if (!handle)
            return ZX_ERR_BAD_HANDLE;
        if (!handle->HasRights(ZX_RIGHT_WAIT))
            return ZX_ERR_ACCESS_DENIED;

        return port->MakeObserver(options, handle, key, signals);
    }
}

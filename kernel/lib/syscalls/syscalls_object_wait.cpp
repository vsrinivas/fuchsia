// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <kernel/event.h>
#include <kernel/thread.h>
#include <platform.h>

#include <lib/ktrace.h>
#include <lib/user_copy/user_ptr.h>

#include <object/handles.h>
#include <object/port_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/wait_state_observer.h>

#include <fbl/auto_lock.h>
#include <fbl/inline_array.h>
#include <fbl/ref_ptr.h>

#include "syscalls_priv.h"

using fbl::AutoLock;

#define LOCAL_TRACE 0

constexpr uint32_t kMaxWaitHandleCount = 1024u;

// Note: This is used for quite a few InlineArrays (simultaneously) in sys_handle_wait_many.
constexpr size_t kWaitManyInlineCount = 8u;

mx_status_t sys_object_wait_one(mx_handle_t handle_value,
                                mx_signals_t signals,
                                mx_time_t deadline,
                                user_ptr<mx_signals_t> _observed) {
    LTRACEF("handle %x\n", handle_value);

    Event event;

    mx_status_t result;
    WaitStateObserver wait_state_observer;

    auto up = ProcessDispatcher::GetCurrent();
    {
        AutoLock lock(up->handle_table_lock());

        Handle* handle = up->GetHandleLocked(handle_value);
        if (!handle)
            return MX_ERR_BAD_HANDLE;
        if (!handle->HasRights(MX_RIGHT_READ))
            return MX_ERR_ACCESS_DENIED;

        result = wait_state_observer.Begin(&event, handle, signals);
        if (result != MX_OK)
            return result;
    }

#if WITH_LIB_KTRACE
    auto koid = static_cast<uint32_t>(up->GetKoidForHandle(handle_value));
    ktrace(TAG_WAIT_ONE, koid, signals, (uint32_t)deadline, (uint32_t)(deadline >> 32));
#endif

    // event_wait() will return MX_OK if already signaled,
    // even if the deadline has passed.  It will return MX_ERR_TIMED_OUT
    // after the deadline passes if the event has not been
    // signaled.
    result = event.Wait(deadline);

    // Regardless of wait outcome, we must call End().
    auto signals_state = wait_state_observer.End();

#if WITH_LIB_KTRACE
    ktrace(TAG_WAIT_ONE_DONE, koid, signals_state, result, 0);
#endif

    if (_observed) {
        if (_observed.copy_to_user(signals_state) != MX_OK)
            return MX_ERR_INVALID_ARGS;
    }

    if (signals_state & MX_SIGNAL_HANDLE_CLOSED)
        return MX_ERR_CANCELED;

    return result;
}

mx_status_t sys_object_wait_many(user_ptr<mx_wait_item_t> _items, uint32_t count, mx_time_t deadline) {
    LTRACEF("count %u\n", count);

    if (!count) {
        mx_status_t result = thread_sleep_etc(deadline, /*interruptable=*/true);
        if (result != MX_OK)
            return result;
        return MX_ERR_TIMED_OUT;
    }

    if (!_items)
        return MX_ERR_INVALID_ARGS;
    if (count > kMaxWaitHandleCount)
        return MX_ERR_INVALID_ARGS;

    fbl::AllocChecker ac;
    fbl::InlineArray<mx_wait_item_t, kWaitManyInlineCount> items(&ac, count);
    if (!ac.check())
        return MX_ERR_NO_MEMORY;
    if (_items.copy_array_from_user(items.get(), count) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    fbl::InlineArray<WaitStateObserver, kWaitManyInlineCount> wait_state_observers(&ac, count);
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    Event event;

    // We may need to unwind (which can be done outside the lock).
    mx_status_t result = MX_OK;
    size_t num_added = 0;
    {
        auto up = ProcessDispatcher::GetCurrent();
        AutoLock lock(up->handle_table_lock());

        for (; num_added != count; ++num_added) {
            Handle* handle = up->GetHandleLocked(items[num_added].handle);
            if (!handle) {
                result = MX_ERR_BAD_HANDLE;
                break;
            }
            if (!handle->HasRights(MX_RIGHT_READ)) {
                result = MX_ERR_ACCESS_DENIED;
                break;
            }

            result = wait_state_observers[num_added].Begin(&event, handle, items[num_added].waitfor);
            if (result != MX_OK)
                break;
        }
    }
    if (result != MX_OK) {
        for (size_t ix = 0; ix < num_added; ++ix)
            wait_state_observers[ix].End();
        return result;
    }

    // event_wait() will return MX_OK if already signaled,
    // even if deadline has passed.  It will return MX_ERR_TIMED_OUT
    // after the deadline passes if the event has not been
    // signaled.
    result = event.Wait(deadline);

    // Regardless of wait outcome, we must call End().
    mx_signals_t combined = 0;
    for (size_t ix = 0; ix != count; ++ix) {
        combined |= (items[ix].pending = wait_state_observers[ix].End());
    }

    if (_items.copy_array_to_user(items.get(), count) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    if (combined & MX_SIGNAL_HANDLE_CLOSED)
        return MX_ERR_CANCELED;

    return result;
}

mx_status_t sys_object_wait_async(mx_handle_t handle_value, mx_handle_t port_handle,
                                  uint64_t key, mx_signals_t signals, uint32_t options) {
    LTRACEF("handle %x\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<PortDispatcher> port;
    auto status = up->GetDispatcherWithRights(port_handle, MX_RIGHT_WRITE, &port);
    if (status != MX_OK)
        return status;

    {
        AutoLock lock(up->handle_table_lock());
        Handle* handle = up->GetHandleLocked(handle_value);
        if (!handle)
            return MX_ERR_BAD_HANDLE;
        if (!handle->HasRights(MX_RIGHT_READ))
            return MX_ERR_ACCESS_DENIED;

        return port->MakeObserver(options, handle, key, signals);
    }
}

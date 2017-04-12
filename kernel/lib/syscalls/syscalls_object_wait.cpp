// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <kernel/auto_lock.h>
#include <platform.h>

#include <lib/ktrace.h>
#include <lib/user_copy/user_ptr.h>

#include <magenta/magenta.h>
#include <magenta/port_dispatcher_v2.h>
#include <magenta/process_dispatcher.h>
#include <magenta/wait_event.h>
#include <magenta/wait_state_observer.h>

#include <mxtl/inline_array.h>
#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

constexpr uint32_t kMaxWaitHandleCount = 1024u;

// Note: This is used for quite a few InlineArrays (simultaneously) in sys_handle_wait_many.
constexpr size_t kWaitManyInlineCount = 8u;

mx_status_t sys_object_wait_one(mx_handle_t handle_value,
                                mx_signals_t signals,
                                mx_time_t deadline,
                                user_ptr<mx_signals_t> _observed) {
    LTRACEF("handle %d\n", handle_value);

    WaitEvent event;

    status_t result;
    WaitStateObserver wait_state_observer;

    auto up = ProcessDispatcher::GetCurrent();
    {
        AutoLock lock(up->handle_table_lock());

        Handle* handle = up->GetHandleLocked(handle_value);
        if (!handle)
            return ERR_BAD_HANDLE;
        if (!magenta_rights_check(handle, MX_RIGHT_READ))
            return ERR_ACCESS_DENIED;

        result = wait_state_observer.Begin(&event, handle, signals);
        if (result != NO_ERROR)
            return result;
    }

#if WITH_LIB_KTRACE
    auto koid = static_cast<uint32_t>(up->GetKoidForHandle(handle_value));
    ktrace(TAG_WAIT_ONE, koid, signals, (uint32_t)deadline, (uint32_t)(deadline >> 32));
#endif

    // event_wait() will return NO_ERROR if already signaled,
    // even if the deadline has passed.  It will return ERR_TIMED_OUT
    // after the deadline passes if the event has not been
    // signaled.
    result = event.Wait(deadline);

    // Regardless of wait outcome, we must call End().
    auto signals_state = wait_state_observer.End();

#if WITH_LIB_KTRACE
    ktrace(TAG_WAIT_ONE_DONE, koid, signals_state, result, 0);
#endif

    if (_observed) {
        if (_observed.copy_to_user(signals_state) != NO_ERROR)
            return ERR_INVALID_ARGS;
    }

    if (signals_state & MX_SIGNAL_HANDLE_CLOSED)
        return ERR_CANCELED;

    return result;
}

mx_status_t sys_object_wait_many(user_ptr<mx_wait_item_t> _items, uint32_t count, mx_time_t deadline) {
    LTRACEF("count %u\n", count);

    if (!count) {
        mx_status_t result = magenta_sleep(deadline);
        if (result != NO_ERROR)
            return result;
        return ERR_TIMED_OUT;
    }

    if (!_items)
        return ERR_INVALID_ARGS;
    if (count > kMaxWaitHandleCount)
        return ERR_INVALID_ARGS;

    AllocChecker ac;
    mxtl::InlineArray<mx_wait_item_t, kWaitManyInlineCount> items(&ac, count);
    if (!ac.check())
        return ERR_NO_MEMORY;
    if (_items.copy_array_from_user(items.get(), count) != NO_ERROR)
        return ERR_INVALID_ARGS;

    mxtl::InlineArray<WaitStateObserver, kWaitManyInlineCount> wait_state_observers(&ac, count);
    if (!ac.check())
        return ERR_NO_MEMORY;

    WaitEvent event;

    // We may need to unwind (which can be done outside the lock).
    status_t result = NO_ERROR;
    size_t num_added = 0;
    {
        auto up = ProcessDispatcher::GetCurrent();
        AutoLock lock(up->handle_table_lock());

        for (; num_added != count; ++num_added) {
            Handle* handle = up->GetHandleLocked(items[num_added].handle);
            if (!handle) {
                result = ERR_BAD_HANDLE;
                break;
            }
            if (!magenta_rights_check(handle, MX_RIGHT_READ)) {
                result = ERR_ACCESS_DENIED;
                break;
            }

            result = wait_state_observers[num_added].Begin(&event, handle, items[num_added].waitfor);
            if (result != NO_ERROR)
                break;
        }
    }
    if (result != NO_ERROR) {
        for (size_t ix = 0; ix < num_added; ++ix)
            wait_state_observers[ix].End();
        return result;
    }

    // event_wait() will return NO_ERROR if already signaled,
    // even if deadline has passed.  It will return ERR_TIMED_OUT
    // after the deadline passes if the event has not been
    // signaled.
    result = event.Wait(deadline);

    // Regardless of wait outcome, we must call End().
    mx_signals_t combined = 0;
    for (size_t ix = 0; ix != count; ++ix) {
        combined |= (items[ix].pending = wait_state_observers[ix].End());
    }

    if (_items.copy_array_to_user(items.get(), count) != NO_ERROR)
        return ERR_INVALID_ARGS;

    if (combined & MX_SIGNAL_HANDLE_CLOSED)
        return ERR_CANCELED;

    return result;
}

mx_status_t sys_object_wait_async(mx_handle_t handle_value, mx_handle_t port_handle,
                                  uint64_t key, mx_signals_t signals, uint32_t options) {
    LTRACEF("handle %d\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PortDispatcherV2> port;
    auto status = up->GetDispatcherWithRights(port_handle, MX_RIGHT_WRITE, &port);
    if (status != NO_ERROR)
        return status;

    {
        AutoLock lock(up->handle_table_lock());
        Handle* handle = up->GetHandleLocked(handle_value);
        if (!handle)
            return ERR_BAD_HANDLE;
        if (!magenta_rights_check(handle, MX_RIGHT_READ))
            return ERR_ACCESS_DENIED;

        return port->MakeObservers(options, handle, key, signals);
    }
}

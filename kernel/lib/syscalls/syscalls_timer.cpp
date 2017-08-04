// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <lib/ktrace.h>

#include <magenta/handle_owner.h>
#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>
#include <magenta/timer_dispatcher.h>
#include <magenta/user_copy.h>

#include <mxtl/alloc_checker.h>
#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

mx_status_t sys_timer_create(uint32_t options, uint32_t clock_id, user_ptr<mx_handle_t> _out) {
    if (options != 0u)
        return MX_ERR_INVALID_ARGS;
    if (clock_id != MX_CLOCK_MONOTONIC)
        return MX_ERR_INVALID_ARGS;

    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;

    mx_status_t result = TimerDispatcher::Create(options, &dispatcher, &rights);

    if (result != MX_OK)
        return result;

    HandleOwner handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return MX_ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();
    mx_handle_t hv = up->MapHandleToValue(handle);

    if (_out.copy_to_user(hv) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(handle));
    return MX_OK;
}

// This syscall is deprecated. The clients should transiton to sys_timer_set().
mx_status_t sys_timer_start(
    mx_handle_t handle, mx_time_t deadline, mx_duration_t period, mx_duration_t slack) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<TimerDispatcher> timer;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_WRITE, &timer);
    if (status != MX_OK)
        return status;

    return timer->Set(deadline, 0u, period);
}

mx_status_t sys_timer_set(
    mx_handle_t handle, mx_time_t deadline, mx_duration_t slack) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<TimerDispatcher> timer;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_WRITE, &timer);
    if (status != MX_OK)
        return status;

    return timer->Set(deadline, slack, 0u);
}


mx_status_t sys_timer_cancel(mx_handle_t handle) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<TimerDispatcher> timer;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_WRITE, &timer);
    if (status != MX_OK)
        return status;

    return timer->Cancel();
}

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

#include <mxalloc/new.h>
#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

mx_status_t sys_timer_create(uint32_t options, user_ptr<mx_handle_t> _out) {
    // Currently, the only allowed option is to switch on PortsV2.
    if (options != 0u)
        return ERR_INVALID_ARGS;

    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;

    mx_status_t result = TimerDispatcher::Create(options, &dispatcher, &rights);

    if (result != NO_ERROR)
        return result;

    HandleOwner handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();
    mx_handle_t hv = up->MapHandleToValue(handle);

    if (_out.copy_to_user(hv) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(handle));
    return NO_ERROR;
}

mx_status_t sys_timer_start(
    mx_handle_t handle, mx_time_t deadline, mx_duration_t period, mx_duration_t slack) {
    // TODO(cpu): we might want to support a 0 deadline. It would mean "signal now"
    // but this might cause problems if the object is understood as marking trusted time.
    if (deadline == 0u)
        return ERR_INVALID_ARGS;

    // TODO(cpu): support timer coalescing (aka slack).
    // TODO(cpu): support periodic timers.
    if (period)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<TimerDispatcher> timer;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_WRITE, &timer);
    if (status != NO_ERROR)
        return status;

    return timer->SetOneShot(deadline);
}

mx_status_t sys_timer_cancel(mx_handle_t handle) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<TimerDispatcher> timer;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_WRITE, &timer);
    if (status != NO_ERROR)
        return status;

    return timer->CancelOneShot();
}


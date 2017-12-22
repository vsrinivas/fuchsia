// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <lib/ktrace.h>

#include <object/handle.h>
#include <object/process_dispatcher.h>
#include <object/timer_dispatcher.h>

#include <fbl/alloc_checker.h>
#include <fbl/ref_ptr.h>

#include <zircon/types.h>

#include "priv.h"

zx_status_t sys_timer_create(uint32_t options, uint32_t clock_id,
                             user_out_handle* out) {
    if (clock_id != ZX_CLOCK_MONOTONIC)
        return ZX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    zx_status_t result = up->QueryPolicy(ZX_POL_NEW_TIMER);
    if (result != ZX_OK)
        return result;

    fbl::RefPtr<Dispatcher> dispatcher;
    zx_rights_t rights;

    result = TimerDispatcher::Create(options, &dispatcher, &rights);

    if (result == ZX_OK)
        result = out->make(fbl::move(dispatcher), rights);
    return result;
}

zx_status_t sys_timer_set(
    zx_handle_t handle, zx_time_t deadline, zx_duration_t slack) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<TimerDispatcher> timer;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_WRITE, &timer);
    if (status != ZX_OK)
        return status;

    return timer->Set(deadline, slack);
}


zx_status_t sys_timer_cancel(zx_handle_t handle) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<TimerDispatcher> timer;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_WRITE, &timer);
    if (status != ZX_OK)
        return status;

    return timer->Cancel();
}

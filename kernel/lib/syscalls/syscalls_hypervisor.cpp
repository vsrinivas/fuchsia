// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/handle_owner.h>
#include <magenta/hypervisor_dispatcher.h>
#include <magenta/process_dispatcher.h>

#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

mx_status_t sys_hypervisor_create(mx_handle_t opt_handle, uint32_t options, mx_handle_t* out) {
    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    mx_status_t status = HypervisorDispatcher::Create(&dispatcher, &rights);
    if (status != NO_ERROR)
        return status;

    HandleOwner handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();
    if (make_user_ptr(out).copy_to_user(up->MapHandleToValue(handle)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(handle));
    return NO_ERROR;
}

// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <platform.h>
#include <stdint.h>
#include <stdlib.h>
#include <trace.h>

#include <lib/user_copy.h>
#include <lib/user_copy/user_ptr.h>

#include <magenta/fifo_dispatcher.h>
#include <magenta/handle_owner.h>
#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>
#include <magenta/syscalls/policy.h>
#include <magenta/user_copy.h>

#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

mx_status_t sys_fifo_create(uint32_t count, uint32_t elemsize, uint32_t options,
                            user_ptr<mx_handle_t> _out0, user_ptr<mx_handle_t> _out1) {
    auto up = ProcessDispatcher::GetCurrent();
    mx_status_t res = up->QueryPolicy(MX_POL_NEW_FIFO);
    if (res < 0)
        return res;

    mxtl::RefPtr<Dispatcher> dispatcher0;
    mxtl::RefPtr<Dispatcher> dispatcher1;
    mx_rights_t rights;
    mx_status_t result = FifoDispatcher::Create(count, elemsize, options,
                                                &dispatcher0, &dispatcher1, &rights);
    if (result != NO_ERROR)
        return result;

    HandleOwner handle0(MakeHandle(mxtl::move(dispatcher0), rights));
    if (!handle0)
        return ERR_NO_MEMORY;
    HandleOwner handle1(MakeHandle(mxtl::move(dispatcher1), rights));
    if (!handle1)
        return ERR_NO_MEMORY;

    if (_out0.copy_to_user(up->MapHandleToValue(handle0)) != NO_ERROR)
        return ERR_INVALID_ARGS;
    if (_out1.copy_to_user(up->MapHandleToValue(handle1)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(handle0));
    up->AddHandle(mxtl::move(handle1));

    return NO_ERROR;
}

mx_status_t sys_fifo_write(mx_handle_t handle, user_ptr<const void> entries,
        size_t len, user_ptr<uint32_t> _actual) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<FifoDispatcher> fifo;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_WRITE, &fifo);
    if (status != NO_ERROR)
        return status;

    uint32_t actual;
    // TODO(andymutton): Change FifoDispatcher to accept user_ptr
    status = fifo->WriteFromUser(reinterpret_cast<const uint8_t*>(entries.get()), len, &actual);
    if (status != NO_ERROR)
        return status;

    if (_actual.copy_to_user(actual) != NO_ERROR)
        return ERR_INVALID_ARGS;

    return NO_ERROR;
}

mx_status_t sys_fifo_read(mx_handle_t handle, user_ptr<void> entries, size_t len, user_ptr<uint32_t> _actual) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<FifoDispatcher> fifo;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_READ, &fifo);
    if (status != NO_ERROR)
        return status;

    uint32_t actual;
    // TODO(andymutton): Change FifoDispatcher to accept user_ptr
    status = fifo->ReadToUser(reinterpret_cast<uint8_t*>(entries.get()), len, &actual);
    if (status != NO_ERROR)
        return status;

    if (_actual.copy_to_user(actual) != NO_ERROR)
        return ERR_INVALID_ARGS;

    return NO_ERROR;
}

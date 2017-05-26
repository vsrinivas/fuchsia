// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include <lib/user_copy.h>
#include <lib/user_copy/user_ptr.h>

#include <magenta/handle_owner.h>
#include <magenta/process_dispatcher.h>
#include <magenta/socket_dispatcher.h>

#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

mx_status_t sys_socket_create(uint32_t options, user_ptr<mx_handle_t> _out0, user_ptr<mx_handle_t> _out1) {
    LTRACEF("entry out_handles %p, %p\n", _out0.get(), _out1.get());

    if (options != 0u)
        return ERR_INVALID_ARGS;

    mxtl::RefPtr<Dispatcher> socket0, socket1;
    mx_rights_t rights;
    status_t result = SocketDispatcher::Create(options, &socket0, &socket1, &rights);
    if (result != NO_ERROR)
        return result;

    HandleOwner h0(MakeHandle(mxtl::move(socket0), rights));
    if (!h0)
        return ERR_NO_MEMORY;

    HandleOwner h1(MakeHandle(mxtl::move(socket1), rights));
    if (!h1)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();

    if (_out0.copy_to_user(up->MapHandleToValue(h0)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    if (_out1.copy_to_user(up->MapHandleToValue(h1)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(h0));
    up->AddHandle(mxtl::move(h1));

    return NO_ERROR;
}

mx_status_t sys_socket_write(mx_handle_t handle, uint32_t options,
                             user_ptr<const void> _buffer, size_t size,
                             user_ptr<size_t> _actual) {
    LTRACEF("handle %d\n", handle);

    if ((size > 0u) && !_buffer)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<SocketDispatcher> socket;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_WRITE, &socket);
    if (status != NO_ERROR)
        return status;

    switch (options) {
    case 0: {
        size_t nwritten;
        status = socket->Write(_buffer, size, &nwritten);

        // Caller may ignore results if desired.
        if (status == NO_ERROR && _actual)
            status = _actual.copy_to_user(nwritten);

        return status;
    }
    case MX_SOCKET_HALF_CLOSE:
        if (size == 0)
            return socket->HalfClose();
    // fall thru if size != 0.
    default:
        return ERR_INVALID_ARGS;
    }
}

mx_status_t sys_socket_read(mx_handle_t handle, uint32_t options,
                            user_ptr<void> _buffer, size_t size,
                            user_ptr<size_t> _actual) {
    LTRACEF("handle %d\n", handle);

    if (options)
        return ERR_INVALID_ARGS;

    if (!_buffer && size > 0)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<SocketDispatcher> socket;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_READ, &socket);
    if (status != NO_ERROR)
        return status;

    size_t nread;
    status = socket->Read(_buffer, size, &nread);

    // Caller may ignore results if desired.
    if (status == NO_ERROR && _actual)
        status = _actual.copy_to_user(nread);

    return status;
}

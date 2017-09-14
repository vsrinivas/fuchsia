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

#include <lib/user_copy/user_ptr.h>
#include <object/handle_owner.h>
#include <object/handles.h>
#include <object/process_dispatcher.h>
#include <object/socket_dispatcher.h>

#include <zircon/syscalls/policy.h>
#include <fbl/ref_ptr.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

zx_status_t sys_socket_create(uint32_t options, user_ptr<zx_handle_t> _out0, user_ptr<zx_handle_t> _out1) {
    LTRACEF("entry out_handles %p, %p\n", _out0.get(), _out1.get());

    auto up = ProcessDispatcher::GetCurrent();
    zx_status_t res = up->QueryPolicy(ZX_POL_NEW_SOCKET);
    if (res != ZX_OK)
        return res;

    fbl::RefPtr<Dispatcher> socket0, socket1;
    zx_rights_t rights;
    zx_status_t result = SocketDispatcher::Create(options, &socket0, &socket1, &rights);
    if (result != ZX_OK)
        return result;

    HandleOwner h0(MakeHandle(fbl::move(socket0), rights));
    if (!h0)
        return ZX_ERR_NO_MEMORY;

    HandleOwner h1(MakeHandle(fbl::move(socket1), rights));
    if (!h1)
        return ZX_ERR_NO_MEMORY;

    if (_out0.copy_to_user(up->MapHandleToValue(h0)) != ZX_OK)
        return ZX_ERR_INVALID_ARGS;

    if (_out1.copy_to_user(up->MapHandleToValue(h1)) != ZX_OK)
        return ZX_ERR_INVALID_ARGS;

    up->AddHandle(fbl::move(h0));
    up->AddHandle(fbl::move(h1));

    return ZX_OK;
}

zx_status_t sys_socket_write(zx_handle_t handle, uint32_t options,
                             user_ptr<const void> _buffer, size_t size,
                             user_ptr<size_t> _actual) {
    LTRACEF("handle %x\n", handle);

    if ((size > 0u) && !_buffer)
        return ZX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<SocketDispatcher> socket;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_WRITE, &socket);
    if (status != ZX_OK)
        return status;

    size_t nwritten;
    switch (options) {
    case 0:
        status = socket->Write(_buffer, size, &nwritten);
        break;
    case ZX_SOCKET_CONTROL:
        status = socket->WriteControl(_buffer, size);
        if (status == ZX_OK)
            nwritten = size;
        break;
    case ZX_SOCKET_SHUTDOWN_WRITE:
    case ZX_SOCKET_SHUTDOWN_READ:
    case ZX_SOCKET_SHUTDOWN_READ | ZX_SOCKET_SHUTDOWN_WRITE:
        if (size == 0)
            return socket->Shutdown(options & ZX_SOCKET_SHUTDOWN_MASK);
        // fallthrough
    default:
        return ZX_ERR_INVALID_ARGS;
    }

    // Caller may ignore results if desired.
    if (status == ZX_OK && _actual)
        status = _actual.copy_to_user(nwritten);

    return status;
}

zx_status_t sys_socket_read(zx_handle_t handle, uint32_t options,
                            user_ptr<void> _buffer, size_t size,
                            user_ptr<size_t> _actual) {
    LTRACEF("handle %x\n", handle);

    if (options)

    if (!_buffer && size > 0)
        return ZX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<SocketDispatcher> socket;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_READ, &socket);
    if (status != ZX_OK)
        return status;

    size_t nread;

    switch (options) {
    case 0:
        status = socket->Read(_buffer, size, &nread);
        break;
    case ZX_SOCKET_CONTROL:
        status = socket->ReadControl(_buffer, size, &nread);
        break;
    default:
        return ZX_ERR_INVALID_ARGS;
    }

    // Caller may ignore results if desired.
    if (status == ZX_OK && _actual)
        status = _actual.copy_to_user(nread);

    return status;
}

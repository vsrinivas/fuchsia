// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <kernel/auto_lock.h>

#include <lib/ktrace.h>

#include <magenta/port_dispatcher.h>
#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>

#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

mx_status_t sys_port_create(uint32_t options, user_ptr<mx_handle_t> out) {
    LTRACEF("options %u\n", options);

    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    mx_status_t result = PortDispatcher::Create(options, &dispatcher, &rights);
    if (result != NO_ERROR)
        return result;

    uint32_t koid = (uint32_t)dispatcher->get_koid();

    HandleUniquePtr handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();

    mx_handle_t hv = up->MapHandleToValue(handle.get());

    if (out.copy_to_user(hv) != NO_ERROR)
        return ERR_INVALID_ARGS;
    up->AddHandle(mxtl::move(handle));

    ktrace(TAG_PORT_CREATE, koid, 0, 0, 0);
    return NO_ERROR;
}

mx_status_t sys_port_queue(mx_handle_t handle, user_ptr<const void> packet, mx_size_t size) {
    LTRACEF("handle %d\n", handle);

    if (size > MX_PORT_MAX_PKT_SIZE)
        return ERR_BUFFER_TOO_SMALL;

    if (size < sizeof(mx_packet_header_t))
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PortDispatcher> ioport;
    mx_status_t status = up->GetDispatcher(handle, &ioport, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    auto iopk = IOP_Packet::MakeFromUser(packet.get(), size);
    if (!iopk)
        return ERR_NO_MEMORY;

    ktrace(TAG_PORT_QUEUE, (uint32_t)ioport->get_koid(), (uint32_t)size, 0, 0);

    return ioport->Queue(iopk);
}

mx_status_t sys_port_wait(mx_handle_t handle, user_ptr<void> packet, mx_size_t size) {
    LTRACEF("handle %d\n", handle);

    if (!packet)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PortDispatcher> ioport;
    mx_status_t status = up->GetDispatcher(handle, &ioport, MX_RIGHT_READ);
    if (status != NO_ERROR)
        return status;

    ktrace(TAG_PORT_WAIT, (uint32_t)ioport->get_koid(), 0, 0, 0);

    IOP_Packet* iopk = nullptr;
    status = ioport->Wait(&iopk);
    ktrace(TAG_PORT_WAIT_DONE, (uint32_t)ioport->get_koid(), status, 0, 0);
    if (status < 0)
        return status;

    if (!iopk->CopyToUser(packet.get(), &size))
        return ERR_INVALID_ARGS;

    IOP_Packet::Delete(iopk);
    return NO_ERROR;
}

mx_status_t sys_port_bind(mx_handle_t handle, uint64_t key,
                          mx_handle_t source, mx_signals_t signals) {
    LTRACEF("handle %d source %d\n", handle, source);

    if (!signals)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PortDispatcher> ioport;
    mx_status_t status = up->GetDispatcher(handle, &ioport, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    mxtl::RefPtr<Dispatcher> source_disp;
    uint32_t rights;
    if (!up->GetDispatcher(source, &source_disp, &rights))
        return up->BadHandle(source, ERR_BAD_HANDLE);

    if (!magenta_rights_check(rights, MX_RIGHT_READ))
        return ERR_ACCESS_DENIED;

    AllocChecker ac;
    mxtl::unique_ptr<PortClient> client(
        new (&ac) PortClient(mxtl::move(ioport), key, signals));
    if (!ac.check())
        return ERR_NO_MEMORY;

    return source_disp->set_port_client(mxtl::move(client));
}

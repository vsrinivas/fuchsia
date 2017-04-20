// Copyright 2016 The Fuchsia Authors
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
#include <magenta/port_dispatcher.h>
#include <magenta/port_dispatcher_v2.h>
#include <magenta/process_dispatcher.h>
#include <magenta/syscalls/policy.h>
#include <magenta/user_copy.h>

#include <mxalloc/new.h>
#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

mx_status_t sys_port_create(uint32_t options, user_ptr<mx_handle_t> _out) {
    LTRACEF("options %u\n", options);

    // Currently, the only allowed option is to switch on PortsV2.
    if (options & ~MX_PORT_OPT_V2)
        return MX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    mx_status_t res = up->QueryPolicy(MX_POL_NEW_PORT);
    if (res < 0)
        return res;

    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;

    mx_status_t result = (options == MX_PORT_OPT_V2) ?
        PortDispatcherV2::Create(options, &dispatcher, &rights):
        PortDispatcher::Create(options, &dispatcher, &rights);

    if (result != MX_OK)
        return result;

    uint32_t koid = (uint32_t)dispatcher->get_koid();

    HandleOwner handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return MX_ERR_NO_MEMORY;

    mx_handle_t hv = up->MapHandleToValue(handle);

    if (_out.copy_to_user(hv) != MX_OK)
        return MX_ERR_INVALID_ARGS;
    up->AddHandle(mxtl::move(handle));

    ktrace(TAG_PORT_CREATE, koid, 0, 0, 0);
    return MX_OK;
}

static mx_status_t sys_port_queue2(mx_handle_t handle, user_ptr<const void> _packet) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PortDispatcherV2> port;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_WRITE, &port);
    if (status != MX_OK)
        return status;

    mx_port_packet_t packet;
    if (_packet.copy_array_from_user(&packet, sizeof(packet)) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    return port->QueueUser(packet);
}

mx_status_t sys_port_queue(mx_handle_t handle, user_ptr<const void> _packet, size_t size) {
    LTRACEF("handle %d\n", handle);

    if (size > MX_PORT_MAX_PKT_SIZE)
        return MX_ERR_BUFFER_TOO_SMALL;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PortDispatcher> port;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_WRITE, &port);
    if (status != MX_OK) {
        return (size == 0u) ? sys_port_queue2(handle, _packet) : status;
    }

    if (size < sizeof(mx_packet_header_t))
        return MX_ERR_INVALID_ARGS;

    // TODO(andymutton): Change MakeFromUser to accept a user_ptr
    auto iopk = IOP_Packet::MakeFromUser(_packet.get(), size);
    if (!iopk)
        return MX_ERR_NO_MEMORY;

    ktrace(TAG_PORT_QUEUE, (uint32_t)port->get_koid(), (uint32_t)size, 0, 0);

    return port->Queue(iopk);
}

mx_status_t sys_port_wait2(mx_handle_t handle, mx_time_t deadline, user_ptr<void> _packet) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PortDispatcherV2> port;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_WRITE, &port);
    if (status != MX_OK)
        return status;

    mx_port_packet_t pp;
    mx_status_t st = port->DeQueue(deadline, &pp);
    if (st != MX_OK)
        return st;

    if (_packet.copy_array_to_user(&pp, sizeof(pp)) != MX_OK)
        return MX_ERR_INVALID_ARGS;
    return MX_OK;
}

mx_status_t sys_port_wait(mx_handle_t handle, mx_time_t deadline,
                          user_ptr<void> _packet, size_t size) {
    LTRACEF("handle %d\n", handle);

    if (!_packet)
        return MX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PortDispatcher> port;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_READ, &port);
    if (status != MX_OK) {
        return (size == 0u) ? sys_port_wait2(handle, deadline, _packet) : status;
    }

    ktrace(TAG_PORT_WAIT, (uint32_t)port->get_koid(), 0, 0, 0);

    IOP_Packet* iopk = nullptr;
    status = port->Wait(deadline, &iopk);

    ktrace(TAG_PORT_WAIT_DONE, (uint32_t)port->get_koid(), status, 0, 0);
    if (status < 0)
        return status;

    // TODO(andymutton): Change CopyToUser to use a user_ptr
    if (!iopk->CopyToUser(_packet.get(), &size))
        return MX_ERR_INVALID_ARGS;

    IOP_Packet::Delete(iopk);
    return MX_OK;
}

mx_status_t sys_port_cancel(mx_handle_t handle, mx_handle_t source, uint64_t key) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PortDispatcherV2> port;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_WRITE, &port);
    if (status != MX_OK)
        return status;

    {
        AutoLock lock(up->handle_table_lock());
        Handle* watched = up->GetHandleLocked(source);
        if (!watched)
            return MX_ERR_BAD_HANDLE;
        if (!magenta_rights_check(watched, MX_RIGHT_READ))
            return MX_ERR_ACCESS_DENIED;

        auto state_tracker = watched->dispatcher()->get_state_tracker();
        if (!state_tracker)
            return MX_ERR_NOT_SUPPORTED;

        bool had_observer = state_tracker->CancelByKey(watched, port.get(), key);
        bool packet_removed = port->CancelQueued(watched, key);
        return (had_observer || packet_removed) ? MX_OK : MX_ERR_NOT_FOUND;
    }
}

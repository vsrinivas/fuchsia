// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/syscalls/hypervisor.h>

#include <object/guest_dispatcher.h>
#include <object/handle_owner.h>
#include <object/handles.h>
#include <object/port_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/resources.h>
#include <object/vcpu_dispatcher.h>
#include <object/vm_object_dispatcher.h>

#include <fbl/ref_ptr.h>

#include "syscalls_priv.h"

mx_status_t sys_guest_create(mx_handle_t resource, uint32_t options, mx_handle_t physmem_vmo,
                             user_ptr<mx_handle_t> out) {
    if (options != 0u)
        return MX_ERR_INVALID_ARGS;

    mx_status_t status = validate_resource(resource, MX_RSRC_KIND_HYPERVISOR);
    if (status != MX_OK)
        return status;

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<VmObjectDispatcher> physmem;
    mx_rights_t rights = MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_EXECUTE;
    status = up->GetDispatcherWithRights(physmem_vmo, rights, &physmem);
    if (status != MX_OK)
        return status;

    fbl::RefPtr<Dispatcher> dispatcher;
    status = GuestDispatcher::Create(physmem->vmo(), &dispatcher, &rights);
    if (status != MX_OK)
        return status;

    HandleOwner handle(MakeHandle(fbl::move(dispatcher), rights));
    if (!handle)
        return MX_ERR_NO_MEMORY;
    status = out.copy_to_user(up->MapHandleToValue(handle));
    if (status != MX_OK)
        return MX_ERR_INVALID_ARGS;

    up->AddHandle(fbl::move(handle));
    return MX_OK;
}

mx_status_t sys_guest_set_trap(mx_handle_t guest_handle, uint32_t kind, mx_vaddr_t addr, size_t len,
                               mx_handle_t port_handle, uint64_t key) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<GuestDispatcher> guest;
    mx_status_t status = up->GetDispatcherWithRights(guest_handle, MX_RIGHT_WRITE, &guest);
    if (status != MX_OK)
        return status;

    fbl::RefPtr<PortDispatcher> port;
    if (port_handle != MX_HANDLE_INVALID) {
        status = up->GetDispatcherWithRights(port_handle, MX_RIGHT_WRITE, &port);
        if (status != MX_OK)
            return status;
    }

    return guest->SetTrap(kind, addr, len, fbl::move(port), key);
}

mx_status_t sys_vcpu_create(mx_handle_t guest_handle, uint32_t options,
                            user_ptr<const mx_vcpu_create_args_t> _args,
                            user_ptr<mx_handle_t> out) {
    if (options != 0u)
        return MX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<GuestDispatcher> guest;
    mx_status_t status = up->GetDispatcherWithRights(guest_handle, MX_RIGHT_WRITE, &guest);
    if (status != MX_OK)
        return status;

    mx_vcpu_create_args_t args;
    status = _args.copy_from_user(&args);
    if (status != MX_OK)
        return status;

#if ARCH_X86_64
    fbl::RefPtr<VmObjectDispatcher> apic;
    status = up->GetDispatcherWithRights(args.apic_vmo, MX_RIGHT_READ | MX_RIGHT_WRITE, &apic);
    if (status != MX_OK)
        return status;

    fbl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    status = VcpuDispatcher::Create(guest, args.ip, args.cr3, apic->vmo(), &dispatcher, &rights);
    if (status != MX_OK)
        return status;

    HandleOwner handle(MakeHandle(fbl::move(dispatcher), rights));
    if (!handle)
        return MX_ERR_NO_MEMORY;
    status = out.copy_to_user(up->MapHandleToValue(handle));
    if (status != MX_OK)
        return MX_ERR_INVALID_ARGS;

    up->AddHandle(fbl::move(handle));
    return MX_OK;
#else // ARCH_X86_64
    return MX_ERR_NOT_SUPPORTED;
#endif
}

mx_status_t sys_vcpu_resume(mx_handle_t vcpu_handle, user_ptr<mx_port_packet_t> _packet) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<VcpuDispatcher> vcpu;
    mx_status_t status = up->GetDispatcherWithRights(vcpu_handle, MX_RIGHT_EXECUTE, &vcpu);
    if (status != MX_OK)
        return status;

    mx_port_packet packet;
    status = vcpu->Resume(&packet);
    if (status != MX_OK)
        return status;

    status = _packet.copy_to_user(packet);
    if (status != MX_OK)
        return MX_ERR_INVALID_ARGS;

    return MX_OK;
}

mx_status_t sys_vcpu_interrupt(mx_handle_t vcpu_handle, uint32_t vector) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<VcpuDispatcher> vcpu;
    mx_status_t status = up->GetDispatcherWithRights(vcpu_handle, MX_RIGHT_SIGNAL, &vcpu);
    if (status != MX_OK)
        return status;

    return vcpu->Interrupt(vector);
}

mx_status_t sys_vcpu_read_state(mx_handle_t vcpu_handle, uint32_t kind,
                                user_ptr<void> _buffer, uint32_t len) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<VcpuDispatcher> vcpu;
    mx_status_t status = up->GetDispatcherWithRights(vcpu_handle, MX_RIGHT_READ, &vcpu);
    if (status != MX_OK)
        return status;

    alignas(alignof(mx_vcpu_state_t)) uint8_t buffer[sizeof(mx_vcpu_state_t)];
    if (len > sizeof(buffer))
        return MX_ERR_INVALID_ARGS;
    status = vcpu->ReadState(kind, buffer, len);
    if (status != MX_OK)
        return status;
    status = _buffer.copy_array_to_user(buffer, len);
    if (status != MX_OK)
        return MX_ERR_INVALID_ARGS;
    return MX_OK;
}

mx_status_t sys_vcpu_write_state(mx_handle_t vcpu_handle, uint32_t kind,
                                 user_ptr<const void> _buffer, uint32_t len) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<VcpuDispatcher> vcpu;
    mx_status_t status = up->GetDispatcherWithRights(vcpu_handle, MX_RIGHT_WRITE, &vcpu);
    if (status != MX_OK)
        return status;

    uint8_t buffer[sizeof(mx_vcpu_state_t)];
    if (len > sizeof(buffer))
        return MX_ERR_INVALID_ARGS;
    status = _buffer.copy_array_from_user(buffer, len);
    if (status != MX_OK)
        return MX_ERR_INVALID_ARGS;
    return vcpu->WriteState(kind, buffer, len);
}

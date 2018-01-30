// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <zircon/syscalls/hypervisor.h>

#include <object/guest_dispatcher.h>
#include <object/handle.h>
#include <object/port_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/resources.h>
#include <object/vcpu_dispatcher.h>
#include <object/vm_object_dispatcher.h>

#include <fbl/ref_ptr.h>

#include "priv.h"

zx_status_t sys_guest_create(zx_handle_t resource, uint32_t options, zx_handle_t physmem_vmo,
                             user_out_handle* out) {
    if (options != 0u)
        return ZX_ERR_INVALID_ARGS;

    zx_status_t status = validate_resource(resource, ZX_RSRC_KIND_HYPERVISOR);
    if (status != ZX_OK)
        return status;

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<VmObjectDispatcher> physmem;
    zx_rights_t rights = ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_EXECUTE;
    status = up->GetDispatcherWithRights(physmem_vmo, rights, &physmem);
    if (status != ZX_OK)
        return status;

    fbl::RefPtr<Dispatcher> dispatcher;
    status = GuestDispatcher::Create(physmem->vmo(), &dispatcher, &rights);
    if (status != ZX_OK)
        return status;

    return out->make(fbl::move(dispatcher), rights);
}

zx_status_t sys_guest_set_trap(zx_handle_t guest_handle, uint32_t kind, zx_vaddr_t addr, size_t len,
                               zx_handle_t port_handle, uint64_t key) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<GuestDispatcher> guest;
    zx_status_t status = up->GetDispatcherWithRights(guest_handle, ZX_RIGHT_WRITE, &guest);
    if (status != ZX_OK)
        return status;

    fbl::RefPtr<PortDispatcher> port;
    if (port_handle != ZX_HANDLE_INVALID) {
        status = up->GetDispatcherWithRights(port_handle, ZX_RIGHT_WRITE, &port);
        if (status != ZX_OK)
            return status;
    }

    return guest->SetTrap(kind, addr, len, fbl::move(port), key);
}

zx_status_t sys_vcpu_create(zx_handle_t guest_handle, uint32_t options,
                            zx_vaddr_t entry, user_out_handle* out) {
    if (options != 0u)
        return ZX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<GuestDispatcher> guest;
    zx_status_t status = up->GetDispatcherWithRights(guest_handle, ZX_RIGHT_WRITE, &guest);
    if (status != ZX_OK)
        return status;

    fbl::RefPtr<Dispatcher> dispatcher;
    zx_rights_t rights;
    status = VcpuDispatcher::Create(guest, entry, &dispatcher, &rights);
    if (status != ZX_OK)
        return status;

    return out->make(fbl::move(dispatcher), rights);
}

zx_status_t sys_vcpu_resume(zx_handle_t vcpu_handle, user_out_ptr<zx_port_packet_t> user_packet) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<VcpuDispatcher> vcpu;
    zx_status_t status = up->GetDispatcherWithRights(vcpu_handle, ZX_RIGHT_EXECUTE, &vcpu);
    if (status != ZX_OK)
        return status;

    zx_port_packet packet;
    status = vcpu->Resume(&packet);
    if (status != ZX_OK)
        return status;

    status = user_packet.copy_to_user(packet);
    if (status != ZX_OK)
        return status;

    return ZX_OK;
}

zx_status_t sys_vcpu_interrupt(zx_handle_t vcpu_handle, uint32_t vector) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<VcpuDispatcher> vcpu;
    zx_status_t status = up->GetDispatcherWithRights(vcpu_handle, ZX_RIGHT_SIGNAL, &vcpu);
    if (status != ZX_OK)
        return status;

    return vcpu->Interrupt(vector);
}

zx_status_t sys_vcpu_read_state(zx_handle_t vcpu_handle, uint32_t kind,
                                user_out_ptr<void> user_buffer, uint32_t len) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<VcpuDispatcher> vcpu;
    zx_status_t status = up->GetDispatcherWithRights(vcpu_handle, ZX_RIGHT_READ, &vcpu);
    if (status != ZX_OK)
        return status;

    alignas(alignof(zx_vcpu_state_t)) uint8_t buffer[sizeof(zx_vcpu_state_t)];
    if (len > sizeof(buffer))
        return ZX_ERR_INVALID_ARGS;
    status = vcpu->ReadState(kind, buffer, len);
    if (status != ZX_OK)
        return status;
    status = user_buffer.copy_array_to_user(buffer, len);
    if (status != ZX_OK)
        return ZX_ERR_INVALID_ARGS;
    return ZX_OK;
}

zx_status_t sys_vcpu_write_state(zx_handle_t vcpu_handle, uint32_t kind,
                                 user_in_ptr<const void> user_buffer, uint32_t len) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<VcpuDispatcher> vcpu;
    zx_status_t status = up->GetDispatcherWithRights(vcpu_handle, ZX_RIGHT_WRITE, &vcpu);
    if (status != ZX_OK)
        return status;

    alignas(alignof(zx_vcpu_state_t)) uint8_t buffer[sizeof(zx_vcpu_state_t)];
    if (len > sizeof(buffer))
        return ZX_ERR_INVALID_ARGS;
    status = user_buffer.copy_array_from_user(buffer, len);
    if (status != ZX_OK)
        return ZX_ERR_INVALID_ARGS;
    return vcpu->WriteState(kind, buffer, len);
}

// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/fifo_dispatcher.h>
#include <magenta/guest_dispatcher.h>
#include <magenta/handle_owner.h>
#include <magenta/process_dispatcher.h>
#include <magenta/syscalls/hypervisor.h>
#include <magenta/vcpu_dispatcher.h>
#include <magenta/vm_object_dispatcher.h>

#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

mx_status_t sys_hypervisor_create(mx_handle_t opt_handle, uint32_t options,
                                  user_ptr<mx_handle_t> out) {
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t guest_create(uint32_t options, mx_handle_t physmem_vmo, mx_handle_t* out) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<VmObjectDispatcher> physmem;
    mx_rights_t rights = MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_EXECUTE;
    mx_status_t status = up->GetDispatcherWithRights(physmem_vmo, rights, &physmem);
    if (status != MX_OK)
        return status;

    mxtl::RefPtr<Dispatcher> dispatcher;
    status = GuestDispatcher::Create(physmem->vmo(), &dispatcher, &rights);
    if (status != MX_OK)
        return status;

    HandleOwner handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return MX_ERR_NO_MEMORY;

    *out = up->MapHandleToValue(handle);
    up->AddHandle(mxtl::move(handle));
    return MX_OK;
}

static mx_status_t guest_set_trap(mx_handle_t guest_handle, mx_trap_address_space_t aspace,
                                  mx_vaddr_t addr, size_t len, mx_handle_t fifo_handle) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<GuestDispatcher> guest;
    mx_status_t status = up->GetDispatcherWithRights(guest_handle, MX_RIGHT_WRITE, &guest);
    if (status != MX_OK)
        return status;

    mxtl::RefPtr<FifoDispatcher> fifo;
    if (fifo_handle != MX_HANDLE_INVALID) {
        status = up->GetDispatcherWithRights(fifo_handle, MX_RIGHT_WRITE, &fifo);
        if (status != MX_OK)
            return status;
    }

    return guest->SetTrap(aspace, addr, len, fifo);
}

static mx_status_t vcpu_create(uint32_t options, mx_handle_t guest_handle,
                               const mx_vcpu_create_args_t* args, mx_handle_t* out) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<GuestDispatcher> guest;
    mx_status_t status = up->GetDispatcherWithRights(guest_handle, MX_RIGHT_WRITE, &guest);
    if (status != MX_OK)
        return status;

#if ARCH_X86_64
    mxtl::RefPtr<VmObjectDispatcher> apic;
    status = up->GetDispatcherWithRights(args->apic_vmo, MX_RIGHT_READ | MX_RIGHT_WRITE, &apic);
    if (status != MX_OK)
        return status;

    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    status = VcpuDispatcher::Create(guest, args->ip, args->cr3, apic->vmo(), &dispatcher, &rights);
    if (status != MX_OK)
        return status;

    HandleOwner handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return MX_ERR_NO_MEMORY;

    *out = up->MapHandleToValue(handle);
    up->AddHandle(mxtl::move(handle));
    return MX_OK;
#else // ARCH_X86_64
    return MX_ERR_NOT_SUPPORTED;
#endif
}

static mx_status_t vcpu_resume(mx_handle_t vcpu_handle, mx_guest_packet_t* packet) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<VcpuDispatcher> vcpu;
    mx_status_t status = up->GetDispatcherWithRights(vcpu_handle, MX_RIGHT_EXECUTE, &vcpu);
    if (status != MX_OK)
        return status;

    return vcpu->Resume(packet);
}

static mx_status_t vcpu_interrupt(mx_handle_t vcpu_handle, uint32_t vector) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<VcpuDispatcher> vcpu;
    mx_status_t status = up->GetDispatcherWithRights(vcpu_handle, MX_RIGHT_WRITE /* MX_RIGHT_SIGNAL */, &vcpu);
    if (status != MX_OK)
        return status;

    return vcpu->Interrupt(vector);
}

static mx_status_t vcpu_read_state(mx_handle_t vcpu_handle, mx_vcpu_state_t* vcpu_state) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<VcpuDispatcher> vcpu;
    mx_status_t status = up->GetDispatcherWithRights(vcpu_handle, MX_RIGHT_READ, &vcpu);
    if (status != MX_OK)
        return status;

    return vcpu->ReadState(vcpu_state);
}

static mx_status_t vcpu_write_state(mx_handle_t vcpu_handle, const mx_vcpu_state_t& vcpu_state) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<VcpuDispatcher> vcpu;
    mx_status_t status = up->GetDispatcherWithRights(vcpu_handle, MX_RIGHT_WRITE, &vcpu);
    if (status != MX_OK)
        return status;

    return vcpu->WriteState(vcpu_state);
}

 mx_status_t sys_hypervisor_op(mx_handle_t handle, uint32_t opcode, user_ptr<const void> args,
                               uint32_t args_len, user_ptr<void> result, uint32_t result_len) {
    switch (opcode) {
    case MX_HYPERVISOR_OP_GUEST_CREATE: {
        struct {
            uint32_t options;
            mx_handle_t physmem_vmo;
        } create_args;
        if (args_len != sizeof(create_args))
            return MX_ERR_INVALID_ARGS;
        if (args.copy_array_from_user(&create_args, sizeof(create_args)) != MX_OK)
            return MX_ERR_INVALID_ARGS;
        mx_handle_t out;
        if (result_len != sizeof(out))
            return MX_ERR_INVALID_ARGS;
        mx_status_t status = guest_create(create_args.options, create_args.physmem_vmo, &out);
        if (status != MX_OK)
            return status;
        if (result.copy_array_to_user(&out, sizeof(out)) != MX_OK)
            return MX_ERR_INVALID_ARGS;
        return MX_OK;
    }
    case MX_HYPERVISOR_OP_GUEST_SET_TRAP: {
        struct {
            mx_trap_address_space_t aspace;
            mx_vaddr_t addr;
            size_t len;
            mx_handle_t fifo;
        } trap_args;
        if (args_len != sizeof(trap_args))
            return MX_ERR_INVALID_ARGS;
        if (args.copy_array_from_user(&trap_args, sizeof(trap_args)) != MX_OK)
            return MX_ERR_INVALID_ARGS;
        return guest_set_trap(handle, trap_args.aspace, trap_args.addr, trap_args.len,
                              trap_args.fifo);
    }
    case MX_HYPERVISOR_OP_VCPU_CREATE: {
        struct {
            uint32_t options;
            mx_vcpu_create_args_t args;
        } create_args;
        if (args_len != sizeof(create_args))
            return MX_ERR_INVALID_ARGS;
        if (args.copy_array_from_user(&create_args, sizeof(create_args)) != MX_OK)
            return MX_ERR_INVALID_ARGS;
        mx_handle_t out;
        if (result_len != sizeof(out))
            return MX_ERR_INVALID_ARGS;
        mx_status_t status = vcpu_create(create_args.options, handle, &create_args.args, &out);
        if (status != MX_OK)
            return status;
        if (result.copy_array_to_user(&out, sizeof(out)) != MX_OK)
            return MX_ERR_INVALID_ARGS;
        return MX_OK;
    }
    case MX_HYPERVISOR_OP_VCPU_RESUME: {
        mx_guest_packet_t packet;
        if (result_len != sizeof(packet))
            return MX_ERR_INVALID_ARGS;
        mx_status_t status = vcpu_resume(handle, &packet);
        if (status != MX_OK)
            return status;
        if (result.copy_array_to_user(&packet, sizeof(packet)) != MX_OK)
            return MX_ERR_INVALID_ARGS;
        return MX_OK;
    }
    case MX_HYPERVISOR_OP_VCPU_INTERRUPT: {
        uint32_t vector;
        if (args_len != sizeof(vector))
            return MX_ERR_INVALID_ARGS;
        if (args.copy_array_from_user(&vector, sizeof(vector)) != MX_OK)
            return MX_ERR_INVALID_ARGS;
        return vcpu_interrupt(handle, vector);
    }
    case MX_HYPERVISOR_OP_VCPU_READ_STATE: {
        mx_vcpu_state_t vcpu_state;
        if (result_len != sizeof(vcpu_state))
            return MX_ERR_INVALID_ARGS;
        mx_status_t status = vcpu_read_state(handle, &vcpu_state);
        if (status != MX_OK)
            return status;
        if (result.copy_array_to_user(&vcpu_state, sizeof(vcpu_state)) != MX_OK)
            return MX_ERR_INVALID_ARGS;
        return MX_OK;
    }
    case MX_HYPERVISOR_OP_VCPU_WRITE_STATE: {
        mx_vcpu_state_t vcpu_state;
        if (args_len != sizeof(vcpu_state))
            return MX_ERR_INVALID_ARGS;
        if (args.copy_array_from_user(&vcpu_state, sizeof(vcpu_state)) != MX_OK)
            return MX_ERR_INVALID_ARGS;
        return vcpu_write_state(handle, vcpu_state);
    }
    default:
        return MX_ERR_INVALID_ARGS;
    }
}

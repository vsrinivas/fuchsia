// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/fifo_dispatcher.h>
#include <magenta/guest_dispatcher.h>
#include <magenta/handle_owner.h>
#include <magenta/hypervisor_dispatcher.h>
#include <magenta/process_dispatcher.h>
#include <magenta/syscalls/hypervisor.h>
#include <magenta/vm_object_dispatcher.h>

#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

mx_status_t sys_hypervisor_create(mx_handle_t opt_handle, uint32_t options, user_ptr<mx_handle_t> out) {
    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    mx_status_t status = HypervisorDispatcher::Create(&dispatcher, &rights);
    if (status != NO_ERROR)
        return status;

    HandleOwner handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();
    if (out.copy_to_user(up->MapHandleToValue(handle)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(handle));
    return NO_ERROR;
}

static mx_status_t guest_create(mx_handle_t hypervisor_handle,
                                mx_handle_t phys_mem_handle,
                                mx_handle_t ctl_fifo_handle,
                                mx_handle_t* out) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<HypervisorDispatcher> hypervisor;
    mx_status_t status =
        up->GetDispatcherWithRights(hypervisor_handle, MX_RIGHT_EXECUTE, &hypervisor);
    if (status != NO_ERROR)
        return status;

    mxtl::RefPtr<VmObjectDispatcher> phys_mem;
    status = up->GetDispatcherWithRights(
        phys_mem_handle, MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_EXECUTE, &phys_mem);
    if (status != NO_ERROR)
        return status;

    mxtl::RefPtr<FifoDispatcher> ctl_fifo;
    status = up->GetDispatcherWithRights(
        ctl_fifo_handle, MX_RIGHT_READ | MX_RIGHT_WRITE, &ctl_fifo);
    if (status != NO_ERROR)
        return status;

    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    status = GuestDispatcher::Create(
        hypervisor, phys_mem->vmo(), ctl_fifo, &dispatcher, &rights);
    if (status != NO_ERROR)
        return status;

    HandleOwner handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    *out = up->MapHandleToValue(handle);
    up->AddHandle(mxtl::move(handle));
    return NO_ERROR;
}

static mx_status_t guest_enter(mx_handle_t handle) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<GuestDispatcher> guest;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_EXECUTE, &guest);
    if (status != NO_ERROR)
        return status;

    return guest->Enter();
}

static mx_status_t guest_mem_trap(mx_handle_t handle, mx_vaddr_t guest_paddr, size_t size) {
    auto up = ProcessDispatcher::GetCurrent();

    if (!IS_PAGE_ALIGNED(guest_paddr))
        return ERR_INVALID_ARGS;
    if (size % PAGE_SIZE != 0)
        return ERR_INVALID_ARGS;

    mxtl::RefPtr<GuestDispatcher> guest;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_EXECUTE, &guest);
    if (status != NO_ERROR)
        return status;

    return guest->MemTrap(guest_paddr, size);
}

static mx_status_t guest_set_gpr(mx_handle_t handle, const mx_guest_gpr_t& guest_gpr) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<GuestDispatcher> guest;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_WRITE, &guest);
    if (status != NO_ERROR)
        return status;

    return guest->SetGpr(guest_gpr);
}

static mx_status_t guest_get_gpr(mx_handle_t handle, mx_guest_gpr_t* guest_gpr) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<GuestDispatcher> guest;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_READ, &guest);
    if (status != NO_ERROR)
        return status;

    return guest->GetGpr(guest_gpr);
}

static mx_status_t guest_set_ip(mx_handle_t handle, uintptr_t guest_ip) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<GuestDispatcher> guest;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_WRITE, &guest);
    if (status != NO_ERROR)
        return status;

    return guest->set_ip(guest_ip);
}

#if ARCH_X86_64
static mx_status_t guest_set_cr3(mx_handle_t handle, uintptr_t guest_cr3) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<GuestDispatcher> guest;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_WRITE, &guest);
    if (status != NO_ERROR)
        return status;

    return guest->set_cr3(guest_cr3);
}

static mx_status_t guest_set_apic_mem(mx_handle_t handle, mx_handle_t apic_mem_handle) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<GuestDispatcher> guest;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_WRITE, &guest);
    if (status != NO_ERROR)
        return status;

    mxtl::RefPtr<VmObjectDispatcher> apic_mem;
    status = up->GetDispatcherWithRights(
        apic_mem_handle, MX_RIGHT_READ | MX_RIGHT_WRITE, &apic_mem);
    if (status != NO_ERROR)
        return status;

    return guest->SetApicMem(apic_mem->vmo());
}
#endif

 mx_status_t sys_hypervisor_op(mx_handle_t handle, uint32_t opcode, user_ptr<const void> args,
                               uint32_t args_len, user_ptr<void> result, uint32_t result_len) {
    switch (opcode) {
    case MX_HYPERVISOR_OP_GUEST_CREATE: {
        mx_handle_t create_args[2] /* = { phys_mem, ctl_fifo } */;
        if (args_len != sizeof(create_args))
            return ERR_INVALID_ARGS;
        if (args.copy_array_from_user(create_args, sizeof(create_args)) != NO_ERROR)
            return ERR_INVALID_ARGS;
        mx_handle_t out;
        if (result_len != sizeof(out))
            return ERR_INVALID_ARGS;
        mx_status_t status = guest_create(handle, create_args[0], create_args[1], &out);
        if (status != NO_ERROR)
            return status;
        if (result.copy_array_to_user(&out, sizeof(out)) != NO_ERROR)
            return ERR_INVALID_ARGS;
        return NO_ERROR;
    }
    case MX_HYPERVISOR_OP_GUEST_ENTER:
        return guest_enter(handle);
    case MX_HYPERVISOR_OP_GUEST_MEM_TRAP: {
        uint64_t mem_trap_args[2] /* = { mx_vaddr_t guest_paddr, size_t size } */;
        if (args_len != sizeof(mem_trap_args))
            return ERR_INVALID_ARGS;
        if (args.copy_array_from_user(mem_trap_args, sizeof(mem_trap_args)) != NO_ERROR)
            return ERR_INVALID_ARGS;
        return guest_mem_trap(handle, mem_trap_args[0], mem_trap_args[1]);
    }
    case MX_HYPERVISOR_OP_GUEST_SET_GPR: {
        mx_guest_gpr_t guest_gpr;
        if (args_len != sizeof(guest_gpr))
            return ERR_INVALID_ARGS;
        if (args.copy_array_from_user(&guest_gpr, sizeof(guest_gpr)) != NO_ERROR)
            return ERR_INVALID_ARGS;
        return guest_set_gpr(handle, guest_gpr);
    }
    case MX_HYPERVISOR_OP_GUEST_GET_GPR: {
        mx_guest_gpr_t guest_gpr;
        if (result_len != sizeof(guest_gpr))
            return ERR_INVALID_ARGS;
        mx_status_t status = guest_get_gpr(handle, &guest_gpr);
        if (status != NO_ERROR)
            return status;
        if (result.copy_array_to_user(&guest_gpr, sizeof(guest_gpr)) != NO_ERROR)
            return ERR_INVALID_ARGS;
        return NO_ERROR;
    }
    case MX_HYPERVISOR_OP_GUEST_SET_ENTRY_IP: {
        uintptr_t guest_ip;
        if (args_len != sizeof(guest_ip))
            return ERR_INVALID_ARGS;
        if (args.copy_array_from_user(&guest_ip, sizeof(guest_ip)) != NO_ERROR)
            return ERR_INVALID_ARGS;
        return guest_set_ip(handle, guest_ip);
    }
#if ARCH_X86_64
    case MX_HYPERVISOR_OP_GUEST_SET_ENTRY_CR3: {
        uintptr_t guest_cr3;
        if (args_len != sizeof(guest_cr3))
            return ERR_INVALID_ARGS;
        if (args.copy_array_from_user(&guest_cr3, sizeof(guest_cr3)) != NO_ERROR)
            return ERR_INVALID_ARGS;
        return guest_set_cr3(handle, guest_cr3);
    }
    case MX_HYPERVISOR_OP_GUEST_SET_APIC_MEM: {
        mx_handle_t apic_mem;
        if (args_len != sizeof(apic_mem))
            return ERR_INVALID_ARGS;
        if (args.copy_array_from_user(&apic_mem, sizeof(apic_mem)) != NO_ERROR)
            return ERR_INVALID_ARGS;
        return guest_set_apic_mem(handle, apic_mem);
    }
#endif // ARCH_X86_64
    default:
        return ERR_INVALID_ARGS;
    }
}

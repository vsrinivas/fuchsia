// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <kernel/vm/vm_object.h>
#include <kernel/vm/vm_region.h>

#include <lib/user_copy.h>
#include <lib/user_copy/user_ptr.h>

#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>
#include <magenta/user_copy.h>
#include <magenta/vm_object_dispatcher.h>

#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

mx_handle_t sys_vmo_create(uint64_t size) {
    LTRACEF("size %#" PRIx64 "\n", size);

    // create a vm object
    mxtl::RefPtr<VmObject> vmo = VmObject::Create(0, size);
    if (!vmo)
        return ERR_NO_MEMORY;

    // create a Vm Object dispatcher
    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    mx_status_t result = VmObjectDispatcher::Create(mxtl::move(vmo), &dispatcher, &rights);
    if (result != NO_ERROR)
        return result;

    // create a handle and attach the dispatcher to it
    HandleUniquePtr handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();

    mx_handle_t hv = up->MapHandleToValue(handle.get());
    up->AddHandle(mxtl::move(handle));

    return hv;
}

mx_ssize_t sys_vmo_read(mx_handle_t handle, user_ptr<void> data, uint64_t offset, mx_size_t len) {
    LTRACEF("handle %d, data %p, offset %#" PRIx64 ", len %#" PRIxPTR "\n",
            handle, data.get(), offset, len);

    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    mxtl::RefPtr<VmObjectDispatcher> vmo;
    mx_status_t status = up->GetDispatcher(handle, &vmo, MX_RIGHT_READ);
    if (status != NO_ERROR)
        return status;

    // do the read operation
    return vmo->Read(data, len, offset);
}

mx_ssize_t sys_vmo_write(mx_handle_t handle, user_ptr<const void> data, uint64_t offset, mx_size_t len) {
    LTRACEF("handle %d, data %p, offset %#" PRIx64 ", len %#" PRIxPTR "\n",
            handle, data.get(), offset, len);

    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    mxtl::RefPtr<VmObjectDispatcher> vmo;
    mx_status_t status = up->GetDispatcher(handle, &vmo, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    // do the write operation
    return vmo->Write(data, len, offset);
}

mx_status_t sys_vmo_get_size(mx_handle_t handle, user_ptr<uint64_t> _size) {
    LTRACEF("handle %d, sizep %p\n", handle, _size.get());

    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    mxtl::RefPtr<VmObjectDispatcher> vmo;
    mx_status_t status = up->GetDispatcher(handle, &vmo);
    if (status != NO_ERROR)
        return status;

    // no rights check, anyone should be able to get the size

    // do the operation
    uint64_t size = 0;
    status = vmo->GetSize(&size);

    // copy the size back, even if it failed
    if (_size.copy_to_user(size) != NO_ERROR)
        return ERR_INVALID_ARGS;

    return status;
}

mx_status_t sys_vmo_set_size(mx_handle_t handle, uint64_t size) {
    LTRACEF("handle %d, size %#" PRIx64 "\n", handle, size);

    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    mxtl::RefPtr<VmObjectDispatcher> vmo;
    mx_status_t status = up->GetDispatcher(handle, &vmo, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    // do the operation
    return vmo->SetSize(size);
}

mx_status_t sys_vmo_op_range(mx_handle_t handle, uint32_t op, uint64_t offset, uint64_t size,
                             user_ptr<void> buffer, mx_size_t buffer_size) {
    LTRACEF("handle %d op %u offset %#" PRIx64 " size %#" PRIx64
            " buffer %p buffer_size %" PRIuPTR "\n",
             handle, op, offset, size, buffer.get(), buffer_size);

    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    mxtl::RefPtr<VmObjectDispatcher> vmo;
    mx_rights_t vmo_rights;
    mx_status_t status = up->GetDispatcher(handle, &vmo, &vmo_rights);
    if (status != NO_ERROR)
        return status;

    return vmo->RangeOp(op, offset, size, buffer, buffer_size, vmo_rights);
}

mx_status_t sys_process_map_vm(mx_handle_t proc_handle, mx_handle_t vmo_handle,
                               uint64_t offset, mx_size_t len, user_ptr<uintptr_t> user_ptr,
                               uint32_t flags) {

    LTRACEF("proc handle %d, vmo handle %d, offset %#" PRIx64
            ", len %#" PRIxPTR ", user_ptr %p, flags %#x\n",
            proc_handle, vmo_handle, offset, len, user_ptr.get(), flags);

    // current process
    auto up = ProcessDispatcher::GetCurrent();

    // get the vmo dispatcher
    mxtl::RefPtr<VmObjectDispatcher> vmo;
    uint32_t vmo_rights;
    mx_status_t status = up->GetDispatcher(vmo_handle, &vmo, &vmo_rights);
    if (status != NO_ERROR)
        return status;

    // get process dispatcher
    mxtl::RefPtr<ProcessDispatcher> process;
    status = get_process(up, proc_handle, &process);
    if (status != NO_ERROR)
        return status;

    // copy the user pointer in
    uintptr_t ptr;
    if (user_ptr.copy_from_user(&ptr) != NO_ERROR)
        return ERR_INVALID_ARGS;

    // do the map call
    status = process->Map(mxtl::move(vmo), vmo_rights,
                          offset, len, &ptr, flags);
    if (status != NO_ERROR)
        return status;

    // copy the user pointer back
    if (user_ptr.copy_to_user(ptr) != NO_ERROR)
        return ERR_INVALID_ARGS;

    return NO_ERROR;
}

mx_status_t sys_process_unmap_vm(mx_handle_t proc_handle, uintptr_t address, mx_size_t len) {
    LTRACEF("proc handle %d, address %#" PRIxPTR ", len %#" PRIxPTR "\n",
            proc_handle, address, len);

    // current process
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<ProcessDispatcher> proc;
    auto status = get_process(up, proc_handle, &proc);
    if (status != NO_ERROR)
        return status;

    return proc->Unmap(address, len);
}

mx_status_t sys_process_protect_vm(mx_handle_t proc_handle, uintptr_t address, mx_size_t len,
                                   uint32_t prot) {
    LTRACEF("proc handle %d, address %#" PRIxPTR ", len %#" PRIxPTR
            ", prot %#x\n", proc_handle, address, len, prot);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<ProcessDispatcher> process;
    mx_status_t status = get_process(up, proc_handle, &process);
    if (status != NO_ERROR)
        return status;

    // get a reffed pointer to the address space in the target process
    mxtl::RefPtr<VmAspace> aspace = process->aspace();
    if (!aspace)
        return ERR_INVALID_ARGS;

    // TODO: support range protect
    // at the moment only support protecting what is at a given address, signaled with len = 0
    if (len != 0)
        return ERR_INVALID_ARGS;

    auto r = aspace->FindRegion(address);
    if (!r)
        return ERR_INVALID_ARGS;

    uint arch_mmu_flags = ARCH_MMU_FLAG_PERM_USER;
    switch (prot & (MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE)) {
    case MX_VM_FLAG_PERM_READ:
        arch_mmu_flags |= ARCH_MMU_FLAG_PERM_READ;
        break;
    case MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE:
        arch_mmu_flags |= ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;
        break;
    case 0: // no way to express no permissions
    case MX_VM_FLAG_PERM_WRITE:
        // no way to express write only
        return ERR_INVALID_ARGS;
    }

    if (prot & MX_VM_FLAG_PERM_EXECUTE) {
        arch_mmu_flags |= ARCH_MMU_FLAG_PERM_EXECUTE;
    }

    return r->Protect(arch_mmu_flags);
}

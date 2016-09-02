// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/vm_object_dispatcher.h>

#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object.h>

#include <assert.h>
#include <new.h>
#include <err.h>
#include <inttypes.h>
#include <trace.h>

#define LOCAL_TRACE 0

constexpr mx_rights_t kDefaultVmoRights =
    MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_EXECUTE | MX_RIGHT_MAP;

status_t VmObjectDispatcher::Create(mxtl::RefPtr<VmObject> vmo,
                                    mxtl::RefPtr<Dispatcher>* dispatcher,
                                    mx_rights_t* rights) {
    AllocChecker ac;
    auto disp = new (&ac) VmObjectDispatcher(mxtl::move(vmo));
    if (!ac.check())
        return ERR_NO_MEMORY;

    *rights = kDefaultVmoRights;
    *dispatcher = mxtl::AdoptRef<Dispatcher>(disp);
    return NO_ERROR;
}

VmObjectDispatcher::VmObjectDispatcher(mxtl::RefPtr<VmObject> vmo)
    : vmo_(vmo) {}

VmObjectDispatcher::~VmObjectDispatcher() {}

mx_ssize_t VmObjectDispatcher::Read(user_ptr<void> user_data, mx_size_t length, uint64_t offset) {

    size_t bytes_read;
    status_t err = vmo_->ReadUser(user_data, offset, length, &bytes_read);
    if (err < 0)
        return err;

    return bytes_read;
}

mx_ssize_t VmObjectDispatcher::Write(user_ptr<const void> user_data, mx_size_t length, uint64_t offset) {

    size_t bytes_written;
    status_t err = vmo_->WriteUser(user_data, offset, length, &bytes_written);
    if (err < 0)
        return err;

    return bytes_written;
}

mx_status_t VmObjectDispatcher::SetSize(uint64_t size) {
    return vmo_->Resize(size);
}

mx_status_t VmObjectDispatcher::GetSize(uint64_t* size) {
    *size = vmo_->size();

    return NO_ERROR;
}

mx_status_t VmObjectDispatcher::RangeOp(uint32_t op, uint64_t offset, uint64_t size,
                                        user_ptr<void> buffer, size_t buffer_size, mx_rights_t rights) {
    LTRACEF("op %u offset %#" PRIx64 " size %#" PRIx64
            " buffer %p buffer_size %zu rights %#x\n",
            op, offset, size, buffer.get(), buffer_size, rights);

    // TODO: test rights

    switch (op) {
        case MX_VMO_OP_COMMIT: {
            auto committed = vmo_->CommitRange(offset, size);
            if (committed < 0)
                return static_cast<mx_status_t>(committed);

            // TODO: handle partial commits
            return NO_ERROR;
        }
        case MX_VMO_OP_DECOMMIT:
            // TODO: handle
            return ERR_NOT_SUPPORTED;
        case MX_VMO_OP_LOCK:
        case MX_VMO_OP_UNLOCK:
            // TODO: handle
            return ERR_NOT_SUPPORTED;
        case MX_VMO_OP_LOOKUP:
            // we will be using the user pointer
            if (!buffer)
                return ERR_INVALID_ARGS;

            // make sure that mx_paddr_t doesn't drift from paddr_t, which the VM uses internally
            static_assert(sizeof(mx_paddr_t) == sizeof(paddr_t));

            return vmo_->Lookup(offset, size, buffer.reinterpret<paddr_t>(), buffer_size);
        case MX_VMO_OP_CACHE_SYNC:
            // TODO: handle
            return ERR_NOT_SUPPORTED;
        default:
            return ERR_INVALID_ARGS;
    }
}

mx_status_t VmObjectDispatcher::Map(mxtl::RefPtr<VmAspace> aspace, uint32_t vmo_rights, uint64_t offset, mx_size_t len,
                                    uintptr_t* _ptr, uint32_t flags) {
    LTRACEF("vmo_rights 0x%x flags 0x%x\n", vmo_rights, flags);

    // test to see if we should even be able to map this
    if (!(vmo_rights & MX_RIGHT_MAP)) {
        return ERR_ACCESS_DENIED;
    }

    // add magenta vm flags, test against rights, and convert to vmm flags
    uint vmm_flags = 0;
    if (flags & MX_VM_FLAG_FIXED) {
        // TODO: test against right
        vmm_flags |= VMM_FLAG_VALLOC_SPECIFIC;
    }

    // convert MX level mapping flags to internal VM flags
    uint arch_mmu_flags = ARCH_MMU_FLAG_PERM_USER;
    switch (flags & (MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE)) {
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

    // add the execute bit
    if (flags & MX_VM_FLAG_PERM_EXECUTE) {
        arch_mmu_flags |= ARCH_MMU_FLAG_PERM_EXECUTE;
    }

    // test against READ/WRITE/EXECUTE rights
    if ((flags & MX_VM_FLAG_PERM_READ) && !(vmo_rights & MX_RIGHT_READ)) {
        return ERR_ACCESS_DENIED;
    }
    if ((flags & MX_VM_FLAG_PERM_WRITE) && !(vmo_rights & MX_RIGHT_WRITE)) {
        return ERR_ACCESS_DENIED;
    }
    if ((flags & MX_VM_FLAG_PERM_EXECUTE) && !(vmo_rights & MX_RIGHT_EXECUTE)) {
        return ERR_ACCESS_DENIED;
    }

    auto status = aspace->MapObject(vmo_, "unnamed", offset, len, reinterpret_cast<void**>(_ptr), 0, 0,
                                    vmm_flags, arch_mmu_flags);
    if (status < 0)
        return status;

    return status;
}

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
#include <trace.h>

constexpr mx_rights_t kDefaultVmoRights =
    MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_EXECUTE;

status_t VmObjectDispatcher::Create(utils::RefPtr<VmObject> vmo,
                                    utils::RefPtr<Dispatcher>* dispatcher,
                                    mx_rights_t* rights) {
    AllocChecker ac;
    auto disp = new (&ac) VmObjectDispatcher(utils::move(vmo));
    if (!ac.check())
        return ERR_NO_MEMORY;

    *rights = kDefaultVmoRights;
    *dispatcher = utils::AdoptRef<Dispatcher>(disp);
    return NO_ERROR;
}

VmObjectDispatcher::VmObjectDispatcher(utils::RefPtr<VmObject> vmo)
    : vmo_(vmo) {}

VmObjectDispatcher::~VmObjectDispatcher() {}

mx_ssize_t VmObjectDispatcher::Read(void* user_data, mx_size_t length, uint64_t offset) {

    size_t bytes_read;
    status_t err = vmo_->ReadUser(user_data, offset, length, &bytes_read);
    if (err < 0)
        return err;

    return bytes_read;
}

mx_ssize_t VmObjectDispatcher::Write(const void* user_data, mx_size_t length, uint64_t offset) {

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

mx_status_t VmObjectDispatcher::Map(utils::RefPtr<VmAspace> aspace, uint32_t vmo_rights, uint64_t offset, mx_size_t len,
                                    uintptr_t* _ptr, uint32_t flags) {
    DEBUG_ASSERT(aspace);

    // add magenta vm flags, test against rights, and convert to vmm flags
    uint vmm_flags = 0;
    if (flags & MX_VM_FLAG_FIXED) {
        // TODO: test against right
        vmm_flags |= VMM_FLAG_VALLOC_SPECIFIC;
    }

    // TODO: test the following against rights on the process and vmo handle
    uint arch_mmu_flags = ARCH_MMU_FLAG_PERM_USER;
    switch (flags & (MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE)) {
    case MX_VM_FLAG_PERM_READ:
        arch_mmu_flags |= ARCH_MMU_FLAG_PERM_RO;
        break;
    case MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE:
        // default flags
        break;
    case 0: // no way to express no permissions
    case MX_VM_FLAG_PERM_WRITE:
        // no way to express write only
        return ERR_INVALID_ARGS;
    }

    if ((flags & MX_VM_FLAG_PERM_EXECUTE) == 0) {
        arch_mmu_flags |= ARCH_MMU_FLAG_PERM_NO_EXECUTE;
    }

    // TODO(teisenbe): Remove this when we have more symbolic debugging working.
    // This is a hack to make it easier to decode crash addresses
    const uint min_align_log2 = 20;

    auto status = aspace->MapObject(vmo_, "unnamed", offset, len, reinterpret_cast<void**>(_ptr), min_align_log2,
                                    vmm_flags, arch_mmu_flags);
    if (status < 0)
        return status;

    return status;
}

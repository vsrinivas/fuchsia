// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/vm_address_region_dispatcher.h>

#include <kernel/vm/vm_address_region.h>
#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object.h>

#include <mxalloc/new.h>

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <trace.h>

#define LOCAL_TRACE 0

namespace {

// Split out the syscall flags into vmar flags and mmu flags.  Note that this
// does not validate that the requested protections in *flags* are valid.  For
// that use is_valid_mapping_protection()
status_t split_syscall_flags(uint32_t flags, uint32_t* vmar_flags, uint* arch_mmu_flags) {
    // Figure out arch_mmu_flags
    uint mmu_flags = ARCH_MMU_FLAG_PERM_USER;
    switch (flags & (MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE)) {
        case MX_VM_FLAG_PERM_READ:
            mmu_flags |= ARCH_MMU_FLAG_PERM_READ;
            break;
        case MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE:
            mmu_flags |= ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;
            break;
    }

    if (flags & MX_VM_FLAG_PERM_EXECUTE) {
        mmu_flags |= ARCH_MMU_FLAG_PERM_EXECUTE;
    }

    // Mask out arch_mmu_flags options
    flags &= ~(MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_PERM_EXECUTE);

    // Figure out vmar flags
    uint32_t vmar = 0;
    if (flags & MX_VM_FLAG_COMPACT) {
        vmar |= VMAR_FLAG_COMPACT;
        flags &= ~MX_VM_FLAG_COMPACT;
    }
    if (flags & MX_VM_FLAG_SPECIFIC) {
        vmar |= VMAR_FLAG_SPECIFIC;
        flags &= ~MX_VM_FLAG_SPECIFIC;
    }
    if (flags & MX_VM_FLAG_SPECIFIC_OVERWRITE) {
        vmar |= VMAR_FLAG_SPECIFIC_OVERWRITE;
        flags &= ~MX_VM_FLAG_SPECIFIC_OVERWRITE;
    }
    if (flags & MX_VM_FLAG_CAN_MAP_SPECIFIC) {
        vmar |= VMAR_FLAG_CAN_MAP_SPECIFIC;
        flags &= ~MX_VM_FLAG_CAN_MAP_SPECIFIC;
    }
    if (flags & MX_VM_FLAG_CAN_MAP_READ) {
        vmar |= VMAR_FLAG_CAN_MAP_READ;
        flags &= ~MX_VM_FLAG_CAN_MAP_READ;
    }
    if (flags & MX_VM_FLAG_CAN_MAP_WRITE) {
        vmar |= VMAR_FLAG_CAN_MAP_WRITE;
        flags &= ~MX_VM_FLAG_CAN_MAP_WRITE;
    }
    if (flags & MX_VM_FLAG_CAN_MAP_EXECUTE) {
        vmar |= VMAR_FLAG_CAN_MAP_EXECUTE;
        flags &= ~MX_VM_FLAG_CAN_MAP_EXECUTE;
    }

    if (flags != 0)
        return ERR_INVALID_ARGS;

    *vmar_flags = vmar;
    *arch_mmu_flags = mmu_flags;
    return NO_ERROR;
}

} // namespace

constexpr mx_rights_t kDefaultVmarRights =
    MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER;

status_t VmAddressRegionDispatcher::Create(mxtl::RefPtr<VmAddressRegion> vmar,
                                    mxtl::RefPtr<Dispatcher>* dispatcher,
                                    mx_rights_t* rights) {

    // The initial rights should match the VMAR's creation permissions
    mx_rights_t vmar_rights = kDefaultVmarRights;
    uint32_t vmar_flags = vmar->flags();
    if (vmar_flags & VMAR_FLAG_CAN_MAP_READ) {
        vmar_rights |= MX_RIGHT_READ;
    }
    if (vmar_flags & VMAR_FLAG_CAN_MAP_WRITE) {
        vmar_rights |= MX_RIGHT_WRITE;
    }
    if (vmar_flags & VMAR_FLAG_CAN_MAP_EXECUTE) {
        vmar_rights |= MX_RIGHT_EXECUTE;
    }

    AllocChecker ac;
    auto disp = new (&ac) VmAddressRegionDispatcher(mxtl::move(vmar));
    if (!ac.check())
        return ERR_NO_MEMORY;

    *rights = vmar_rights;
    *dispatcher = mxtl::AdoptRef<Dispatcher>(disp);
    return NO_ERROR;
}

VmAddressRegionDispatcher::VmAddressRegionDispatcher(mxtl::RefPtr<VmAddressRegion> vmar)
    : vmar_(mxtl::move(vmar)) {}

VmAddressRegionDispatcher::~VmAddressRegionDispatcher() {}

mx_status_t VmAddressRegionDispatcher::Allocate(
    size_t offset, size_t size, uint32_t flags,
    mxtl::RefPtr<VmAddressRegionDispatcher>* new_dispatcher,
    mx_rights_t* new_rights) {

    canary_.Assert();

    uint32_t vmar_flags;
    uint arch_mmu_flags;
    mx_status_t status = split_syscall_flags(flags, &vmar_flags, &arch_mmu_flags);
    if (status != NO_ERROR)
        return status;

    // Check if any MMU-related flags were requested (USER is always implied)
    if (arch_mmu_flags != ARCH_MMU_FLAG_PERM_USER) {
        return ERR_INVALID_ARGS;
    }

    mxtl::RefPtr<VmAddressRegion> new_vmar;
    status = vmar_->CreateSubVmar(offset, size, /* align_pow2 */ 0 , vmar_flags,
                                  "useralloc", &new_vmar);
    if (status != NO_ERROR)
        return status;

    // Create the dispatcher.
    mxtl::RefPtr<Dispatcher> dispatcher;
    status = VmAddressRegionDispatcher::Create(mxtl::move(new_vmar),
                                               &dispatcher, new_rights);
    if (status != NO_ERROR)
        return status;

    *new_dispatcher =
        DownCastDispatcher<VmAddressRegionDispatcher>(&dispatcher);
    return NO_ERROR;
}

mx_status_t VmAddressRegionDispatcher::Destroy() {
    canary_.Assert();

    return vmar_->Destroy();
}

mx_status_t VmAddressRegionDispatcher::Map(size_t vmar_offset, mxtl::RefPtr<VmObject> vmo,
                                           uint64_t vmo_offset, size_t len, uint32_t flags,
                                           mxtl::RefPtr<VmMapping>* out) {
    canary_.Assert();

    if (!is_valid_mapping_protection(flags))
        return ERR_INVALID_ARGS;

    // Split flags into vmar_flags and arch_mmu_flags
    uint32_t vmar_flags;
    uint arch_mmu_flags;
    mx_status_t status = split_syscall_flags(flags, &vmar_flags, &arch_mmu_flags);
    if (status != NO_ERROR)
        return status;

    mxtl::RefPtr<VmMapping> result(nullptr);
    status = vmar_->CreateVmMapping(vmar_offset, len, /* align_pow2 */ 0,
                                    vmar_flags, mxtl::move(vmo), vmo_offset,
                                    arch_mmu_flags, "useralloc",
                                    &result);
    if (status != NO_ERROR) {
        return status;
    }

    *out = mxtl::move(result);
    return NO_ERROR;
}

mx_status_t VmAddressRegionDispatcher::Protect(vaddr_t base, size_t len, uint32_t flags) {
    canary_.Assert();

    if (!IS_PAGE_ALIGNED(base)) {
        return ERR_INVALID_ARGS;
    }

    if (!is_valid_mapping_protection(flags))
        return ERR_INVALID_ARGS;

    uint32_t vmar_flags;
    uint arch_mmu_flags;
    mx_status_t status = split_syscall_flags(flags, &vmar_flags, &arch_mmu_flags);
    if (status != NO_ERROR)
        return status;

    // This request does not allow any VMAR flags to be set
    if (vmar_flags)
        return ERR_INVALID_ARGS;

    return vmar_->Protect(base, len, arch_mmu_flags);
}

mx_status_t VmAddressRegionDispatcher::Unmap(vaddr_t base, size_t len) {
    canary_.Assert();

    if (!IS_PAGE_ALIGNED(base)) {
        return ERR_INVALID_ARGS;
    }

    return vmar_->Unmap(base, len);
}

bool VmAddressRegionDispatcher::is_valid_mapping_protection(uint32_t flags) {
    switch (flags & (MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE)) {
        case 0: // no way to express no permissions
        case MX_VM_FLAG_PERM_WRITE:
            // no way to express write only
            return false;
        default: return true;
    }
}

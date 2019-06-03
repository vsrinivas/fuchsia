// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/vm_address_region_dispatcher.h>

#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>

#include <zircon/rights.h>

#include <fbl/alloc_checker.h>
#include <lib/counters.h>

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <trace.h>

#define LOCAL_TRACE 0

KCOUNTER(dispatcher_vmar_create_count, "dispatcher.vmar.create")
KCOUNTER(dispatcher_vmar_destroy_count, "dispatcher.vmar.destroy")

namespace {

// Split out the syscall flags into vmar flags and mmu flags.  Note that this
// does not validate that the requested protections in *flags* are valid.  For
// that use is_valid_mapping_protection()
zx_status_t split_syscall_flags(
    uint32_t flags, uint32_t* vmar_flags, uint* arch_mmu_flags, uint8_t* align_pow2) {
    // Figure out arch_mmu_flags
    uint mmu_flags = 0;
    switch (flags & (ZX_VM_PERM_READ | ZX_VM_PERM_WRITE)) {
        case ZX_VM_PERM_READ:
            mmu_flags |= ARCH_MMU_FLAG_PERM_READ;
            break;
        case ZX_VM_PERM_READ | ZX_VM_PERM_WRITE:
            mmu_flags |= ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;
            break;
    }

    if (flags & ZX_VM_PERM_EXECUTE) {
        mmu_flags |= ARCH_MMU_FLAG_PERM_EXECUTE;
    }

    // Mask out arch_mmu_flags options
    flags &= ~(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_PERM_EXECUTE);

    // Figure out vmar flags
    uint32_t vmar = 0;
    if (flags & ZX_VM_COMPACT) {
        vmar |= VMAR_FLAG_COMPACT;
        flags &= ~ZX_VM_COMPACT;
    }
    if (flags & ZX_VM_SPECIFIC) {
        vmar |= VMAR_FLAG_SPECIFIC;
        flags &= ~ZX_VM_SPECIFIC;
    }
    if (flags & ZX_VM_SPECIFIC_OVERWRITE) {
        vmar |= VMAR_FLAG_SPECIFIC_OVERWRITE;
        flags &= ~ZX_VM_SPECIFIC_OVERWRITE;
    }
    if (flags & ZX_VM_CAN_MAP_SPECIFIC) {
        vmar |= VMAR_FLAG_CAN_MAP_SPECIFIC;
        flags &= ~ZX_VM_CAN_MAP_SPECIFIC;
    }
    if (flags & ZX_VM_CAN_MAP_READ) {
        vmar |= VMAR_FLAG_CAN_MAP_READ;
        flags &= ~ZX_VM_CAN_MAP_READ;
    }
    if (flags & ZX_VM_CAN_MAP_WRITE) {
        vmar |= VMAR_FLAG_CAN_MAP_WRITE;
        flags &= ~ZX_VM_CAN_MAP_WRITE;
    }
    if (flags & ZX_VM_CAN_MAP_EXECUTE) {
        vmar |= VMAR_FLAG_CAN_MAP_EXECUTE;
        flags &= ~ZX_VM_CAN_MAP_EXECUTE;
    }
    if (flags & ZX_VM_REQUIRE_NON_RESIZABLE) {
        vmar |= VMAR_FLAG_REQUIRE_NON_RESIZABLE;
        flags &= ~ZX_VM_REQUIRE_NON_RESIZABLE;
    }

    if (flags & ((1u << ZX_VM_ALIGN_BASE) - 1u)) {
        return ZX_ERR_INVALID_ARGS;
    }

    // Figure out alignment.
    uint8_t alignment = static_cast<uint8_t>(flags >> ZX_VM_ALIGN_BASE);

    if (((alignment < 10) && (alignment != 0)) || (alignment > 32)) {
        return ZX_ERR_INVALID_ARGS;
    }

    *vmar_flags = vmar;
    *arch_mmu_flags |= mmu_flags;
    *align_pow2 = alignment;
    return ZX_OK;
}

} // namespace

zx_status_t VmAddressRegionDispatcher::Create(fbl::RefPtr<VmAddressRegion> vmar,
                                              uint base_arch_mmu_flags,
                                              KernelHandle<VmAddressRegionDispatcher>* handle,
                                              zx_rights_t* rights) {

    // The initial rights should match the VMAR's creation permissions
    zx_rights_t vmar_rights = default_rights();
    uint32_t vmar_flags = vmar->flags();
    if (vmar_flags & VMAR_FLAG_CAN_MAP_READ) {
        vmar_rights |= ZX_RIGHT_READ;
    }
    if (vmar_flags & VMAR_FLAG_CAN_MAP_WRITE) {
        vmar_rights |= ZX_RIGHT_WRITE;
    }
    if (vmar_flags & VMAR_FLAG_CAN_MAP_EXECUTE) {
        vmar_rights |= ZX_RIGHT_EXECUTE;
    }

    fbl::AllocChecker ac;
    KernelHandle new_handle(
        fbl::AdoptRef(new (&ac) VmAddressRegionDispatcher(ktl::move(vmar), base_arch_mmu_flags)));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    *rights = vmar_rights;
    *handle = ktl::move(new_handle);
    return ZX_OK;
}

VmAddressRegionDispatcher::VmAddressRegionDispatcher(fbl::RefPtr<VmAddressRegion> vmar,
                                                     uint base_arch_mmu_flags)
    : vmar_(ktl::move(vmar)), base_arch_mmu_flags_(base_arch_mmu_flags) {
    kcounter_add(dispatcher_vmar_create_count, 1);
}

VmAddressRegionDispatcher::~VmAddressRegionDispatcher() {
    kcounter_add(dispatcher_vmar_destroy_count, 1);
}

zx_status_t VmAddressRegionDispatcher::Allocate(
    size_t offset, size_t size, uint32_t flags,
    KernelHandle<VmAddressRegionDispatcher>* handle,
    zx_rights_t* new_rights) {

    canary_.Assert();

    uint32_t vmar_flags;
    uint arch_mmu_flags = 0;
    uint8_t alignment = 0;
    zx_status_t status = split_syscall_flags(flags, &vmar_flags, &arch_mmu_flags, &alignment);
    if (status != ZX_OK)
        return status;

    // Check if any MMU-related flags were requested.
    if (arch_mmu_flags != 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::RefPtr<VmAddressRegion> new_vmar;
    status = vmar_->CreateSubVmar(offset, size, alignment , vmar_flags,
                                  "useralloc", &new_vmar);
    if (status != ZX_OK)
        return status;

    return VmAddressRegionDispatcher::Create(ktl::move(new_vmar), base_arch_mmu_flags_, handle,
                                             new_rights);
}

zx_status_t VmAddressRegionDispatcher::Destroy() {
    canary_.Assert();

    return vmar_->Destroy();
}

zx_status_t VmAddressRegionDispatcher::Map(size_t vmar_offset, fbl::RefPtr<VmObject> vmo,
                                           uint64_t vmo_offset, size_t len, uint32_t flags,
                                           fbl::RefPtr<VmMapping>* out) {
    canary_.Assert();

    if (!is_valid_mapping_protection(flags))
        return ZX_ERR_INVALID_ARGS;

    // Split flags into vmar_flags and arch_mmu_flags
    uint32_t vmar_flags;
    uint arch_mmu_flags = base_arch_mmu_flags_;
    uint8_t alignment = 0;
    zx_status_t status = split_syscall_flags(flags, &vmar_flags, &arch_mmu_flags, &alignment);
    if (status != ZX_OK)
        return status;

    if (vmar_flags & VMAR_FLAG_REQUIRE_NON_RESIZABLE) {
        vmar_flags &= ~VMAR_FLAG_REQUIRE_NON_RESIZABLE;
        if (vmo->is_resizable())
            return ZX_ERR_NOT_SUPPORTED;
    }

    fbl::RefPtr<VmMapping> result(nullptr);
    status = vmar_->CreateVmMapping(vmar_offset, len, alignment,
                                    vmar_flags, ktl::move(vmo), vmo_offset,
                                    arch_mmu_flags, "useralloc",
                                    &result);
    if (status != ZX_OK) {
        return status;
    }

    *out = ktl::move(result);
    return ZX_OK;
}

zx_status_t VmAddressRegionDispatcher::Protect(vaddr_t base, size_t len, uint32_t flags) {
    canary_.Assert();

    if (!IS_PAGE_ALIGNED(base)) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (!is_valid_mapping_protection(flags))
        return ZX_ERR_INVALID_ARGS;

    uint32_t vmar_flags;
    uint arch_mmu_flags = base_arch_mmu_flags_;
    uint8_t alignment = 0;
    zx_status_t status = split_syscall_flags(flags, &vmar_flags, &arch_mmu_flags, &alignment);
    if (status != ZX_OK)
        return status;

    // This request does not allow any VMAR flags or alignment flags to be set.
    if (vmar_flags || (alignment != 0))
        return ZX_ERR_INVALID_ARGS;

    return vmar_->Protect(base, len, arch_mmu_flags);
}

zx_status_t VmAddressRegionDispatcher::Unmap(vaddr_t base, size_t len) {
    canary_.Assert();

    if (!IS_PAGE_ALIGNED(base)) {
        return ZX_ERR_INVALID_ARGS;
    }

    return vmar_->Unmap(base, len);
}

bool VmAddressRegionDispatcher::is_valid_mapping_protection(uint32_t flags) {
    if (!(flags & ZX_VM_PERM_READ)) {
        // No way to express non-readable mappings that are also writeable or
        // executable.
        if (flags & (ZX_VM_PERM_WRITE | ZX_VM_PERM_EXECUTE)) {
            return false;
        }
    }
    return true;
}

// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/vm_address_region_dispatcher.h>

#include <kernel/vm/vm_address_region.h>
#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object.h>

#include <assert.h>
#include <new.h>
#include <err.h>
#include <inttypes.h>
#include <trace.h>

#define LOCAL_TRACE 0

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
        size_t offset, size_t size, uint32_t flags, mxtl::RefPtr<VmAddressRegion>* out) {

    return vmar_->CreateSubVmar(offset, size, /* align_pow2 */ 0 , flags, "useralloc", out);
}

mx_status_t VmAddressRegionDispatcher::Destroy() {
    return vmar_->Destroy();
}

mx_status_t VmAddressRegionDispatcher::Map(size_t vmar_offset, mxtl::RefPtr<VmObject> vmo,
                                           uint64_t vmo_offset, size_t len, uint32_t vmar_flags,
                                           uint arch_mmu_flags, mxtl::RefPtr<VmMapping>* out) {

    mxtl::RefPtr<VmMapping> result(nullptr);

    status_t status = vmar_->CreateVmMapping(vmar_offset, len, /* align_pow2 */ 0,
                                             vmar_flags, mxtl::move(vmo), vmo_offset,
                                             arch_mmu_flags, "useralloc",
                                             &result);
    if (status != NO_ERROR) {
        return status;
    }

    *out = mxtl::move(result);
    return NO_ERROR;
}

mx_status_t VmAddressRegionDispatcher::Protect(size_t offset, size_t len, uint arch_mmu_flags) {
    // TODO(teisenbe): Consider supporting splitting mappings; it's unclear if
    // there's a usecase for that versus just creating multiple mappings to
    // start with.

    mxtl::RefPtr<VmMapping> mapping(nullptr);
    {
        mxtl::RefPtr<VmAddressRegionOrMapping> child = vmar_->FindRegion(vmar_->base() + offset);
        if (!child) {
            return ERR_NOT_FOUND;
        }
        mapping = child->as_vm_mapping();
    }

    if (!mapping) {
        return ERR_NOT_FOUND;
    }

    // For now, require that the request be for an entire VmMapping.
    // Additionally, special case len=0 to mean the whole region.
    // TODO(teisenbe): Remove this
    DEBUG_ASSERT(mapping->base() >= vmar_->base());
    size_t mapping_offset = mapping->base() - vmar_->base();
    if (len != 0) {
        if (mapping_offset != offset || mapping->size() != len) {
            return ERR_INVALID_ARGS;
        }
    }

    return mapping->Protect(arch_mmu_flags);
}

mx_status_t VmAddressRegionDispatcher::Unmap(size_t offset, size_t len) {
    // TODO(teisenbe): This should actually allow spanning multiple regions.
    mxtl::RefPtr<VmMapping> mapping(nullptr);
    {
        mxtl::RefPtr<VmAddressRegionOrMapping> child = vmar_->FindRegion(vmar_->base() + offset);
        if (!child) {
            return ERR_NOT_FOUND;
        }
        mapping = child->as_vm_mapping();
    }

    if (!mapping) {
        return ERR_NOT_FOUND;
    }

    // For now, require that the request be for an entire
    // VmMapping/VmAddressRegion, and if len=0, interpret as a request
    // to unmap the entire entry that contains the offset
    // TODO(teisenbe): Remove this
    DEBUG_ASSERT(mapping->base() >= vmar_->base());
    size_t mapping_offset = mapping->base() - vmar_->base();
    if (len != 0) {
        if (mapping_offset != offset || mapping->size() != len) {
            return ERR_INVALID_ARGS;
        }
    }

    return mapping->Unmap();
}

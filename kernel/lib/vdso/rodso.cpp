// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/rodso.h>

#include <inttypes.h>
#include <kernel/vm/vm_address_region.h>
#include <kernel/vm/vm_object.h>
#include <magenta/vm_address_region_dispatcher.h>
#include <magenta/vm_object_dispatcher.h>

RoDso::RoDso(const char* name, const void* image, size_t size,
             uintptr_t code_start)
    : name_(name), code_start_(code_start), size_(size) {
    DEBUG_ASSERT(IS_PAGE_ALIGNED(size));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(code_start));
    DEBUG_ASSERT(code_start > 0);
    DEBUG_ASSERT(code_start < size);
    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_status_t status = VmObjectDispatcher::Create(
        VmObjectPaged::CreateFromROData(image, size),
        &dispatcher, &vmo_rights_);
    ASSERT(status == NO_ERROR);
    vmo_ = DownCastDispatcher<VmObjectDispatcher>(mxtl::move(dispatcher));
    vmo_rights_ &= ~MX_RIGHT_WRITE;
}

HandleUniquePtr RoDso::vmo_handle() {
    return HandleUniquePtr(MakeHandle(vmo_, vmo_rights_));
}

// Map one segment from our VM object.
// If *mapped_addr is zero to begin with, it can go anywhere.
mx_status_t RoDso::MapSegment(mxtl::RefPtr<VmAddressRegionDispatcher> vmar,
                              bool code,
                              uintptr_t start_offset,
                              uintptr_t end_offset,
                              uintptr_t* mapped_addr) {

    uint32_t flags = MX_VM_FLAG_PERM_READ;
    size_t target_offset = 0;
    if (code)
        flags |= MX_VM_FLAG_PERM_EXECUTE;
    if (*mapped_addr != 0) {
        flags |= MX_VM_FLAG_SPECIFIC;
        if (*mapped_addr < vmar->vmar()->base()) {
            return ERR_INVALID_ARGS;
        }
        target_offset = *mapped_addr - vmar->vmar()->base();
    }

    size_t len = end_offset - start_offset;

    mxtl::RefPtr<VmMapping> mapping;
    mx_status_t status = vmar->Map(target_offset,
        vmo_->vmo(), start_offset, len, flags, &mapping);

    const char* segment_name = code ? "code" : "rodata";
    if (status != NO_ERROR) {
        dprintf(CRITICAL,
                "userboot: %s %s mapping %#" PRIxPTR " @ %#" PRIxPTR
                " size %#zx failed %d\n",
                name_, segment_name,
                start_offset, *mapped_addr, len, status);
    } else {
        *mapped_addr = mapping->base();
        dprintf(SPEW, "userboot: %-8s %-6s %#7" PRIxPTR " @ [%#" PRIxPTR
                ",%#" PRIxPTR ")\n", name_, segment_name, start_offset,
                *mapped_addr, *mapped_addr + len);
    }

    return status;
}

mx_status_t RoDso::Map(mxtl::RefPtr<VmAddressRegionDispatcher> vmar,
                       uintptr_t *start_addr) {
    // TODO(teisenbe): Change this to create a sub-VMAR with two child mappings
    // instead.
    mx_status_t status = MapSegment(vmar, false, 0, code_start_,
                                    start_addr);
    if (status == NO_ERROR) {
        uintptr_t code_address = *start_addr + code_start_;
        status = MapSegment(mxtl::move(vmar), true, code_start_, size_,
                            &code_address);
    }
    return status;
}

mx_status_t RoDso::MapAnywhere(mxtl::RefPtr<VmAddressRegionDispatcher> vmar,
                               uintptr_t *start_addr) {
    *start_addr = 0;
    return Map(mxtl::move(vmar), start_addr);
}

mx_status_t RoDso::MapFixed(mxtl::RefPtr<VmAddressRegionDispatcher> vmar,
                            uintptr_t start_addr) {
    ASSERT(start_addr != 0);
    return Map(mxtl::move(vmar), &start_addr);
}

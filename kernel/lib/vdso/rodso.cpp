// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/rodso.h>

#include <inttypes.h>
#include <kernel/vm/vm_address_region.h>
#include <kernel/vm/vm_object.h>
#include <kernel/vm/vm_object_paged.h>
#include <magenta/handle_owner.h>
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
    status = dispatcher->set_name(name, strlen(name));
    ASSERT(status == NO_ERROR);
    vmo_ = DownCastDispatcher<VmObjectDispatcher>(&dispatcher);
    vmo_rights_ &= ~MX_RIGHT_WRITE;
}

HandleOwner RoDso::vmo_handle() const {
    return HandleOwner(MakeHandle(vmo_, vmo_rights_));
}

// Map one segment from our VM object.
mx_status_t RoDso::MapSegment(mxtl::RefPtr<VmAddressRegionDispatcher> vmar,
                              bool code,
                              size_t vmar_offset,
                              size_t start_offset,
                              size_t end_offset) const {

    uint32_t flags = MX_VM_FLAG_SPECIFIC | MX_VM_FLAG_PERM_READ;
    if (code)
        flags |= MX_VM_FLAG_PERM_EXECUTE;

    size_t len = end_offset - start_offset;

    mxtl::RefPtr<VmMapping> mapping;
    mx_status_t status = vmar->Map(vmar_offset, vmo_->vmo(),
                                   start_offset, len, flags, &mapping);

    const char* segment_name = code ? "code" : "rodata";
    if (status != NO_ERROR) {
        dprintf(CRITICAL,
                "userboot: %s %s mapping %#zx @ %#" PRIxPTR
                " size %#zx failed %d\n",
                name_, segment_name, start_offset,
                vmar->vmar()->base() + vmar_offset, len, status);
    } else {
        DEBUG_ASSERT(mapping->base() == vmar->vmar()->base() + vmar_offset);
        dprintf(SPEW, "userboot: %-8s %-6s %#7zx @ [%#" PRIxPTR
                ",%#" PRIxPTR ")\n", name_, segment_name, start_offset,
                mapping->base(), mapping->base() + len);
    }

    return status;
}

mx_status_t RoDso::Map(mxtl::RefPtr<VmAddressRegionDispatcher> vmar,
                       size_t offset) const {
    mx_status_t status = MapSegment(vmar, false, offset, 0, code_start_);
    if (status == NO_ERROR)
        status = MapSegment(mxtl::move(vmar), true,
                            offset + code_start_, code_start_, size_);
    return status;
}

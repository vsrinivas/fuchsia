// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/rodso.h>

#include <inttypes.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>
#include <vm/vm_object_paged.h>
#include <object/handle.h>
#include <object/vm_address_region_dispatcher.h>
#include <object/vm_object_dispatcher.h>

RoDso::RoDso(const char* name, const void* image, size_t size,
             uintptr_t code_start)
    : name_(name), code_start_(code_start), size_(size) {
    DEBUG_ASSERT(IS_PAGE_ALIGNED(size));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(code_start));
    DEBUG_ASSERT(code_start > 0);
    DEBUG_ASSERT(code_start < size);
    fbl::RefPtr<Dispatcher> dispatcher;

    // create vmo out of ro data mapped in kernel space
    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = VmObjectPaged::CreateFromROData(image, size, &vmo);
    ASSERT(status == ZX_OK);

    // build and point a dispatcher at it
    status = VmObjectDispatcher::Create(
        fbl::move(vmo),
        &dispatcher, &vmo_rights_);
    ASSERT(status == ZX_OK);

    status = dispatcher->set_name(name, strlen(name));
    ASSERT(status == ZX_OK);
    vmo_ = DownCastDispatcher<VmObjectDispatcher>(&dispatcher);
    vmo_rights_ &= ~ZX_RIGHT_WRITE;

    // unmap it from the kernel
    // NOTE: this means the image can no longer be referenced from original pointer
    status = VmAspace::kernel_aspace()->arch_aspace().Unmap(
            reinterpret_cast<vaddr_t>(image),
            size / PAGE_SIZE, nullptr);
    ASSERT(status == ZX_OK);
}

HandleOwner RoDso::vmo_handle() const {
    return Handle::Make(vmo_, vmo_rights_);
}

// Map one segment from our VM object.
zx_status_t RoDso::MapSegment(fbl::RefPtr<VmAddressRegionDispatcher> vmar,
                              bool code,
                              size_t vmar_offset,
                              size_t start_offset,
                              size_t end_offset) const {

    uint32_t flags = ZX_VM_FLAG_SPECIFIC | ZX_VM_FLAG_PERM_READ;
    if (code)
        flags |= ZX_VM_FLAG_PERM_EXECUTE;

    size_t len = end_offset - start_offset;

    fbl::RefPtr<VmMapping> mapping;
    zx_status_t status = vmar->Map(vmar_offset, vmo_->vmo(),
                                   start_offset, len, flags, &mapping);

    const char* segment_name = code ? "code" : "rodata";
    if (status != ZX_OK) {
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

zx_status_t RoDso::Map(fbl::RefPtr<VmAddressRegionDispatcher> vmar,
                       size_t offset) const {
    zx_status_t status = MapSegment(vmar, false, offset, 0, code_start_);
    if (status == ZX_OK)
        status = MapSegment(fbl::move(vmar), true,
                            offset + code_start_, code_start_, size_);
    return status;
}

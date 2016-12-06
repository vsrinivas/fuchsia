// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/rodso.h>

#include <inttypes.h>
#include <kernel/vm/vm_object.h>
#include <magenta/process_dispatcher.h>
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
// If *mapped_address is zero to begin with, it can go anywhere.
mx_status_t RoDso::MapSegment(mxtl::RefPtr<ProcessDispatcher> process,
                              bool code,
                              uintptr_t start_offset,
                              uintptr_t end_offset,
                              uintptr_t* mapped_address) {
    uint32_t flags = MX_VM_FLAG_PERM_READ;
    if (code)
        flags |= MX_VM_FLAG_PERM_EXECUTE;
    if (*mapped_address != 0)
        flags |= MX_VM_FLAG_FIXED;

    size_t len = end_offset - start_offset;

    mx_status_t status = process->Map(
        vmo_, MX_RIGHT_READ | MX_RIGHT_EXECUTE | MX_RIGHT_MAP,
        start_offset, len, mapped_address, flags);

    const char* segment_name = code ? "code" : "rodata";
    if (status != NO_ERROR) {
        dprintf(CRITICAL,
                "userboot: %s %s mapping %#" PRIxPTR " @ %#" PRIxPTR
                " size %#zx failed %d\n",
                name_, segment_name,
                start_offset, *mapped_address, len, status);
    } else {
        dprintf(SPEW, "userboot: %-8s %-6s %#7" PRIxPTR " @ [%#" PRIxPTR
                ",%#" PRIxPTR ")\n", name_, segment_name, start_offset,
                *mapped_address, *mapped_address + len);
    }

    return status;
}

mx_status_t RoDso::Map(mxtl::RefPtr<ProcessDispatcher> process,
                       uintptr_t *start_address) {
    mx_status_t status = MapSegment(process, false, 0, code_start_,
                                    start_address);
    if (status == NO_ERROR) {
        uintptr_t code_address = *start_address + code_start_;
        status = MapSegment(mxtl::move(process), true, code_start_, size_,
                            &code_address);
    }
    return status;
}

mx_status_t RoDso::MapAnywhere(mxtl::RefPtr<ProcessDispatcher> process,
                               uintptr_t *start_address) {
    *start_address = 0;
    return Map(mxtl::move(process), start_address);
}

mx_status_t RoDso::MapFixed(mxtl::RefPtr<ProcessDispatcher> process,
                            uintptr_t start_address) {
    ASSERT(start_address != 0);
    return Map(mxtl::move(process), &start_address);
}

// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <vm/vm_aspace.h>
#include <vm/vm_object.h>
#include <fbl/unique_ptr.h>

class GuestPhysicalAddressSpace {
public:
    static zx_status_t Create(fbl::RefPtr<VmObject> guest_phys_mem,
#ifdef ARCH_ARM64
                              uint8_t vmid,
#endif
                              fbl::unique_ptr<GuestPhysicalAddressSpace>* gpas);

    ~GuestPhysicalAddressSpace();

    size_t size() const { return paspace_->size(); }
    const fbl::RefPtr<VmAspace>& aspace() const { return paspace_; }
    zx_paddr_t table_phys() const { return paspace_->arch_aspace().arch_table_phys(); }

    zx_status_t MapInterruptController(vaddr_t guest_paddr, paddr_t host_paddr, size_t size);
    zx_status_t UnmapRange(vaddr_t guest_paddr, size_t size);
    zx_status_t GetPage(vaddr_t guest_paddr, paddr_t* host_paddr);

private:
    fbl::RefPtr<VmAspace> paspace_;
    fbl::RefPtr<VmObject> guest_phys_mem_;

    explicit GuestPhysicalAddressSpace(fbl::RefPtr<VmObject> guest_phys_mem);
};

static inline zx_status_t guest_lookup_page(void* context, size_t offset, size_t index,
                                            paddr_t pa) {
    *static_cast<paddr_t*>(context) = pa;
    return ZX_OK;
}

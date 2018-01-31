// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <vm/vm_aspace.h>
#include <vm/vm_object.h>
#include <fbl/limits.h>
#include <fbl/unique_ptr.h>

// RAII object that holds a mapping of guest physical address space to the host kernel
// virtual address space. Can be used to map a frequently accessed portion of guest
// physical memory to the host kernel address space for faster access.
class GuestMapping {
public:
    static zx_status_t Create(fbl::RefPtr<VmObject> guest_phys_mem, zx_vaddr_t guest_base,
                              size_t size, const char* name,
                              fbl::unique_ptr<GuestMapping>* guest_mapping);
    ~GuestMapping() {
        if (mapping_)
            mapping_->Destroy();
    }

    // All pointers returned from the function become invalid when GuestMapping destroyed.
    template <typename T>
    zx_status_t GetHostPtr(zx_paddr_t guest_paddr, T** host_ptr) {
        if (guest_paddr > fbl::numeric_limits<zx_paddr_t>::max() - sizeof(T))
            return ZX_ERR_INVALID_ARGS;

        if (guest_paddr < guest_base_ || guest_paddr + sizeof(T) > guest_base_ + size_)
            return ZX_ERR_INVALID_ARGS;

        zx_vaddr_t host_vaddr = mapping_->base() + (guest_paddr - guest_base_);
        *host_ptr = reinterpret_cast<T*>(host_vaddr);
        return ZX_OK;
    }

private:
    GuestMapping() = default;

    fbl::RefPtr<VmMapping> mapping_;
    zx_vaddr_t guest_base_;
    size_t size_;
};

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

    zx_status_t MapToHost(zx_vaddr_t guest_paddr, size_t size, const char* name,
                          fbl::unique_ptr<GuestMapping>* guest_mapping) {
        return GuestMapping::Create(guest_phys_mem_, guest_paddr, size, name, guest_mapping);
    }

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

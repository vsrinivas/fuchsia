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

namespace hypervisor {

// RAII object that holds a mapping of guest physical address space to the host
// kernel virtual address space. Can be used to map a frequently accessed
// portion of guest physical memory for faster access.
class GuestPtr {
public:
    GuestPtr() = default;
    GuestPtr(fbl::RefPtr<VmMapping> mapping, zx_vaddr_t offset)
        : mapping_(fbl::move(mapping)), offset_(offset) {}
    GuestPtr(GuestPtr&& o)
        : mapping_(fbl::move(o.mapping_)), offset_(o.offset_) {}
    ~GuestPtr() { reset(); }
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(GuestPtr);

    GuestPtr& operator=(GuestPtr&& o) {
        mapping_ = fbl::move(o.mapping_);
        offset_ = o.offset_;
        return *this;
    }

    void reset() {
        if (mapping_) {
            mapping_->Destroy();
            mapping_.reset();
        }
    }

    template<typename T>
    T* as() const {
        if (offset_ + sizeof(T) > mapping_->size()) {
            return nullptr;
        }
        return reinterpret_cast<T*>(mapping_->base() + offset_);
    }

private:
    fbl::RefPtr<VmMapping> mapping_;
    zx_vaddr_t offset_;
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

    zx_status_t MapInterruptController(vaddr_t guest_paddr, paddr_t host_paddr, size_t size);
    zx_status_t UnmapRange(vaddr_t guest_paddr, size_t size);
    zx_status_t GetPage(vaddr_t guest_paddr, paddr_t* host_paddr);
    zx_status_t CreateGuestPtr(zx_vaddr_t guest_paddr, size_t size, const char* name,
                               GuestPtr* guest_ptr);

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

} // namespace hypervisor

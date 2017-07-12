// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object.h>
#include <mxtl/unique_ptr.h>

class GuestPhysicalAddressSpace {
public:
    static status_t Create(mxtl::RefPtr<VmObject> guest_phys_mem,
                           mxtl::unique_ptr<GuestPhysicalAddressSpace>* gpas);

    ~GuestPhysicalAddressSpace();

    size_t size() const { return paspace_->size(); }
    mxtl::RefPtr<VmAspace> aspace() const { return paspace_; }

    status_t Init(mxtl::RefPtr<VmObject> root_vmo);
#if ARCH_X86_64
    paddr_t Pml4Address() { return paspace_->arch_aspace().pt_phys(); }
    status_t MapApicPage(vaddr_t guest_paddr, paddr_t host_paddr);
#endif
    status_t UnmapRange(vaddr_t guest_paddr, size_t size);
    status_t GetPage(vaddr_t guest_paddr, paddr_t* host_paddr);

private:
    mxtl::RefPtr<VmAspace> paspace_;
    mxtl::RefPtr<VmObject> guest_phys_mem_;

    explicit GuestPhysicalAddressSpace(mxtl::RefPtr<VmObject> guest_phys_mem);
};

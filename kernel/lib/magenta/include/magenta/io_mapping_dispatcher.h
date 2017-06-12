// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/vm/vm_aspace.h>
#include <magenta/dispatcher.h>
#include <mxtl/canary.h>
#include <sys/types.h>

class IoMappingDispatcher : public Dispatcher {
public:
    static status_t Create(const char* dbg_name,
                           paddr_t paddr, size_t size,
                           uint vmm_flags, uint arch_mmu_flags,
                           mxtl::RefPtr<Dispatcher>* out_dispatcher,
                           mx_rights_t* out_rights);

    IoMappingDispatcher(const IoMappingDispatcher &) = delete;
    IoMappingDispatcher& operator=(const IoMappingDispatcher &) = delete;

    ~IoMappingDispatcher();
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_IOMAP; }

    // TODO(cpu): this should be removed when device waiting is refactored.
    virtual void Close();

    bool    closed() const;
    paddr_t paddr()  const { return paddr_; }
    vaddr_t vaddr()  const { return vaddr_; }
    size_t  size()   const { return size_; }
    const mxtl::RefPtr<VmAspace>& aspace() { return aspace_; }

protected:
    IoMappingDispatcher() { }
    void Cleanup();

    status_t Init(const char* dbg_name,
                  paddr_t paddr, size_t size,
                  uint vmm_flags, uint arch_mmu_flags);

private:
    mxtl::Canary<mxtl::magic("IOMD")> canary_;
    mxtl::RefPtr<VmAspace> aspace_;
    paddr_t                 paddr_;
    mxtl::RefPtr<VmMapping> mapping_;
    vaddr_t                 vaddr_ = 0;
    size_t                  size_;
};

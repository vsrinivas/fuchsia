// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/vm/vm_aspace.h>
#include <magenta/dispatcher.h>
#include <sys/types.h>

class IoMappingDispatcher : public Dispatcher {
public:
    static status_t Create(const char* dbg_name,
                           paddr_t paddr, size_t size,
                           uint vmm_flags, uint arch_mmu_flags,
                           utils::RefPtr<Dispatcher>* out_dispatcher,
                           mx_rights_t* out_rights);

    ~IoMappingDispatcher() override;
    void Close(Handle* handle) override;
    IoMappingDispatcher* get_io_mapping_dispatcher() final { return this; }

    bool    closed() const;
    paddr_t paddr()  const { return paddr_; }
    vaddr_t vaddr()  const { return vaddr_; }
    size_t  size()   const { return size_; }
    const utils::RefPtr<VmAspace>& aspace() { return aspace_; }

protected:
    static constexpr mx_rights_t kDefaultRights = MX_RIGHT_READ;
    IoMappingDispatcher() { }
    void Cleanup();

    status_t Init(const char* dbg_name,
                  paddr_t paddr, size_t size,
                  uint vmm_flags, uint arch_mmu_flags);

private:
    utils::RefPtr<VmAspace> aspace_;
    paddr_t                 paddr_;
    vaddr_t                 vaddr_ = 0;
    size_t                  size_;
};

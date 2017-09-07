// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <assert.h>
#include <kernel/mutex.h>
#include <kernel/vm.h>
#include <lib/user_copy/user_ptr.h>
#include <list.h>
#include <magenta/thread_annotations.h>
#include <fbl/array.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <stdint.h>
#include <vm/vm_object.h>

// VMO representing a physical range of memory
class VmObjectPhysical final : public VmObject {
public:
    static status_t Create(paddr_t base, uint64_t size, fbl::RefPtr<VmObject>* vmo);

    uint64_t size() const override
        // TODO: Figure out whether it's safe to lock here without causing
        // any deadlocks.
        TA_NO_THREAD_SAFETY_ANALYSIS { return size_; }

    status_t LookupUser(uint64_t offset, uint64_t len, user_ptr<paddr_t> buffer,
                        size_t buffer_size) override;

    void Dump(uint depth, bool verbose) override;

    status_t GetPageLocked(uint64_t offset, uint pf_flags, list_node* free_list,
                           vm_page_t**, paddr_t* pa) override TA_REQ(lock_);

    status_t GetMappingCachePolicy(uint32_t* cache_policy) override;
    status_t SetMappingCachePolicy(const uint32_t cache_policy) override;

private:
    // private constructor (use Create())
    VmObjectPhysical(paddr_t base, uint64_t size);

    // private destructor, only called from refptr
    ~VmObjectPhysical() override;
    friend fbl::RefPtr<VmObjectPhysical>;

    DISALLOW_COPY_ASSIGN_AND_MOVE(VmObjectPhysical);

    // members
    const uint64_t size_ TA_GUARDED(lock_) = 0;
    const paddr_t base_ TA_GUARDED(lock_) = 0;
    uint32_t mapping_cache_flags_ = 0;
};

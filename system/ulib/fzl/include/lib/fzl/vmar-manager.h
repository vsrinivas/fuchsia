// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <lib/zx/vmar.h>

namespace fzl {

// VmarManager
//
// A small utility class which manages the lifecycle of a VMAR intended to be
// shared among a collection of users.  VmarManager will handle simple tasks such as
// automatically destroying the VMAR at end-of-life in addition to releasing the
// handle.
//
// Currently, the primary use case for a VmarManager is to be used to create a
// COMPACT sub-vmar in order to hold a number of VMO mappings while minimizing
// page table fragmentation..
//
// See fzl::VmoMapper.
class VmarManager : public fbl::RefCounted<VmarManager> {
public:
    // Create a new VmarManager (creating the underlying VMAR object in the
    // process)
    //
    // size   : the size of the VMAR region to create.
    // parent : the parent of this VMAR, or nullptr to use the root VMAR.
    // flags  : creation flags to pass to vmar_allocate
    static fbl::RefPtr<VmarManager> Create(size_t size,
                                      fbl::RefPtr<VmarManager> parent = nullptr,
                                      uint32_t flags = ZX_VM_FLAG_COMPACT |
                                                       ZX_VM_FLAG_CAN_MAP_READ |
                                                       ZX_VM_FLAG_CAN_MAP_WRITE);

    const zx::vmar& vmar() const { return vmar_; }
    void* start() const { return start_; }
    uint64_t size() const { return size_; }
    const fbl::RefPtr<VmarManager>& parent() const { return  parent_; }

private:
    friend class fbl::RefPtr<VmarManager>;

    VmarManager() = default;
    ~VmarManager() {
        if (vmar_.is_valid()) {
            vmar_.destroy();
        }
    }

    // suppress default constructors
    DISALLOW_COPY_ASSIGN_AND_MOVE(VmarManager);

    zx::vmar vmar_;
    void* start_ = nullptr;
    uint64_t size_ = 0;
    fbl::RefPtr<VmarManager> parent_;
};

}  // namespace fzl

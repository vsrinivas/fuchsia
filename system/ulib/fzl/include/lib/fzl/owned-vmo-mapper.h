// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <fbl/ref_counted.h>
#include <lib/fzl/vmar-manager.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>

namespace fzl {

// OwnedVmoWrapper is a convenience wrapper around the underlying VmoMapper
// which also takes ownership of the underlying VMO.
class OwnedVmoMapper : protected VmoMapper {
public:
    OwnedVmoMapper() = default;
    ~OwnedVmoMapper() { Reset(); }
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(OwnedVmoMapper);

    // Move support
    OwnedVmoMapper(OwnedVmoMapper&& other) {
        MoveFromOther(&other);
    }

    OwnedVmoMapper& operator=(OwnedVmoMapper&& other) {
        Reset();
        MoveFromOther(&other);
        return *this;
    }

    // See |VmoMapper::CreateAndMap|.
    zx_status_t CreateAndMap(uint64_t size,
                             const char* name,
                             zx_vm_option_t map_options = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                             fbl::RefPtr<VmarManager> vmar_manager = nullptr,
                             uint32_t cache_policy = 0);

    // See |VmoMapper::Map|.
    zx_status_t Map(zx::vmo vmo,
                    uint64_t size = 0,
                    zx_vm_option_t map_options = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                    fbl::RefPtr<VmarManager> vmar_manager = nullptr);

    // Reset the VMO from whichever VMAR it was mapped into, then release.
    void Reset() {
        vmo_.reset();
        VmoMapper::Unmap();
    }

    const zx::vmo& vmo() const { return vmo_; }

    using VmoMapper::start;
    using VmoMapper::size;
    using VmoMapper::manager;

protected:
    void MoveFromOther(OwnedVmoMapper* other) {
        vmo_ = fbl::move(other->vmo_);
        VmoMapper::MoveFromOther(other);
    }

private:
    zx::vmo vmo_;
};

}  // namespace fzl

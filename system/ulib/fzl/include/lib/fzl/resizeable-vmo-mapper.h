// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <lib/fzl/vmar-manager.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>

namespace fzl {

// ResizeableVmoMapper is an extension of the basic VmoMapper utility which
// allows resizing of the mapping after it has been created.
//
class ResizeableVmoMapper : protected VmoMapper {
  public:
    static fbl::unique_ptr<ResizeableVmoMapper> Create(
            uint64_t size,
            const char* name,
            zx_vm_option_t map_options = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
            fbl::RefPtr<VmarManager> vmar_manager = nullptr,
            uint32_t cache_policy = 0);

    ResizeableVmoMapper() = default;
    ~ResizeableVmoMapper() { Unmap(); }
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ResizeableVmoMapper);

    // Move support
    ResizeableVmoMapper(ResizeableVmoMapper&& other) {
        MoveFromOther(&other);
    }

    ResizeableVmoMapper& operator=(ResizeableVmoMapper&& other) {
        Unmap();
        MoveFromOther(&other);
        return *this;
    }

    // See |VmoMapper::CreateAndMap|
    zx_status_t CreateAndMap(uint64_t size,
                             const char* name,
                             zx_vm_option_t map_options = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                             fbl::RefPtr<VmarManager> vmar_manager = nullptr,
                             uint32_t cache_policy = 0);

    // See |VmoMapper::Map|
    zx_status_t Map(zx::vmo vmo,
                    uint64_t size = 0,
                    zx_vm_option_t map_options = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                    fbl::RefPtr<VmarManager> vmar_manager = nullptr);

    // Attempts to reduce both the VMO size and VMAR mapping
    // from |size_| to |size|.
    //
    // Attempting to shrink the mapping to a size of zero or
    // requesting a "shrink" that would increase the mapping size
    // returns an error.
    //
    // If |size| is not page aligned, shrinking will fail.
    zx_status_t Shrink(size_t size);

    // Attempts to increase both VMO size and VMAR mapping:
    // From [addr_, addr_ + size_]
    // To   [addr_, addr_ + size]
    //
    // Attempting to grow the mapping to a size smaller than the
    // current size will return an error.
    //
    // On failure, the Mapping will be safe to use, but will remain at its original size.
    //
    // Unlike shrinking, it's permissible to grow to a non-page-aligned |size|.
    zx_status_t Grow(size_t size);

    // Unmap the VMO from whichever VMAR it was mapped into, then release.
    void Unmap() {
        vmo_.reset();
        VmoMapper::Unmap();
    }

    const zx::vmo& vmo() const { return vmo_; }
    using VmoMapper::start;
    using VmoMapper::size;
    using VmoMapper::manager;

  private:
    void MoveFromOther(ResizeableVmoMapper* other) {
        vmo_ = fbl::move(other->vmo_);
        VmoMapper::MoveFromOther(other);
    }

    zx::vmo vmo_;
    zx_vm_option_t map_options_ = 0;
};

}  // namespace fzl

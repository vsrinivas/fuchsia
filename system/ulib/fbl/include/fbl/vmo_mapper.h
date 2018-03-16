// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/macros.h>
#include <fbl/vmar_manager.h>
#include <fbl/ref_ptr.h>
#include <fbl/ref_counted.h>
#include <lib/zx/vmo.h>

namespace fbl {

class VmoMapper {
public:
    VmoMapper() = default;
    ~VmoMapper() { Unmap(); }

    // Create a new VMO and map it into our address space using the provided map
    // flags and optional target VMAR.  If requested, return the created VMO
    // with the requested rights.
    //
    // size         : The minimum size, in bytes, of the VMO to create.
    // map_flags    : The flags to use when mapping the VMO.
    // vmar         : A reference to a VmarManager to use when mapping the VMO, or
    //                nullptr to map the VMO using the root VMAR.
    // vmo_out      : A pointer which will receive the created VMO handle, or
    //                nullptr if the handle should be simply closed after it has
    //                been mapped.
    // vmo_rights   : The rights which should be applied to the VMO which is
    //                passed back to the user via vmo_out, or ZX_RIGHT_SAME_RIGHTS
    //                to leave the default rights.
    // cache_policy : When non-zero, indicates the cache policy to apply to the
    //                created VMO.
    zx_status_t CreateAndMap(uint64_t size,
                             uint32_t map_flags,
                             RefPtr<VmarManager> vmar_manager = nullptr,
                             zx::vmo* vmo_out = nullptr,
                             zx_rights_t vmo_rights = ZX_RIGHT_SAME_RIGHTS,
                             uint32_t cache_policy = 0);

    // Map an existing VMO our address space using the provided map
    // flags and optional target VMAR.
    //
    // vmo        : The vmo to map.
    // offset     : The offset into the vmo, in bytes, to start the map
    // size       : The amount of the vmo, in bytes, to map, or 0 to map from
    //              the offset to the end of the VMO.
    // map_flags  : The flags to use when mapping the VMO.
    // vmar       : A reference to a VmarManager to use when mapping the VMO, or
    //              nullptr to map the VMO using the root VMAR.
    zx_status_t Map(const zx::vmo& vmo,
                    uint64_t offset,
                    uint64_t size,
                    uint32_t map_flags,
                    RefPtr<VmarManager> vmar_manager = nullptr);

    // Unmap the VMO from whichever VMAR it was mapped into.
    void Unmap();

    void* start() const { return start_; }
    uint64_t size() const { return size_; }

    // suppress default constructors
    DISALLOW_COPY_ASSIGN_AND_MOVE(VmoMapper);

private:
    zx_status_t CheckReadyToMap(const RefPtr<VmarManager>& vmar_manager);
    zx_status_t InternalMap(const zx::vmo& vmo,
                            uint64_t offset,
                            uint64_t size,
                            uint32_t map_flags,
                            RefPtr<VmarManager> vmar_manager);

    RefPtr<VmarManager> vmar_manager_;
    void* start_ = nullptr;
    uint64_t size_ = 0;
};

class RefCountedVmoMapper : public VmoMapper,
                            public RefCounted<VmoMapper> {
public:
    RefCountedVmoMapper() = default;
};

}  // namespace fbl

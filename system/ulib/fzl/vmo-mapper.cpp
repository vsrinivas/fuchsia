// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <lib/fzl/vmo-mapper.h>
#include <zircon/assert.h>

namespace fzl {

zx_status_t VmoMapper::CreateAndMap(uint64_t size,
                                    zx_vm_option_t map_flags,
                                    fbl::RefPtr<VmarManager> vmar_manager,
                                    zx::vmo* vmo_out,
                                    zx_rights_t vmo_rights,
                                    uint32_t cache_policy) {
    if (size == 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t res = CheckReadyToMap(vmar_manager);
    if (res != ZX_OK) {
        return res;
    }

    zx::vmo vmo;
    zx_status_t ret = zx::vmo::create(size, 0, &vmo);
    if (ret != ZX_OK) {
        return ret;
    }

    if (cache_policy != 0) {
        ret = vmo.set_cache_policy(cache_policy);
        if (ret != ZX_OK) {
            return ret;
        }
    }

    ret = InternalMap(vmo, 0, size, map_flags, fbl::move(vmar_manager));
    if (ret != ZX_OK) {
        return ret;
    }

    if (vmo_out) {
        if (vmo_rights != ZX_RIGHT_SAME_RIGHTS) {
            ret = vmo.replace(vmo_rights, &vmo);
            if (ret != ZX_OK) {
                Unmap();
                return ret;
            }
        }

        *vmo_out = fbl::move(vmo);
    }

    return ZX_OK;
}

zx_status_t VmoMapper::Map(const zx::vmo& vmo,
                           uint64_t offset,
                           uint64_t size,
                           zx_vm_option_t map_options,
                           fbl::RefPtr<VmarManager> vmar_manager) {
    zx_status_t res;

    if (!vmo.is_valid()) {
        return ZX_ERR_INVALID_ARGS;
    }

    res = CheckReadyToMap(vmar_manager);
    if (res != ZX_OK) {
        return res;
    }

    uint64_t vmo_size;
    res = vmo.get_size(&vmo_size);
    if (res != ZX_OK) {
        return res;
    }

    uint64_t end_addr;
    if (add_overflow(size, offset, &end_addr) || end_addr > vmo_size) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    if (!size) {
        size = vmo_size - offset;
    }

    return InternalMap(vmo, offset, size, map_options, vmar_manager);
}

void VmoMapper::Unmap() {
    if (start() != nullptr) {
        ZX_DEBUG_ASSERT(size_ != 0);
        zx_handle_t vmar_handle = (vmar_manager_ == nullptr)
            ? zx::vmar::root_self()->get()
            : vmar_manager_->vmar().get();

        __UNUSED zx_status_t res;
        res = zx_vmar_unmap(vmar_handle, start_, size_);
        ZX_DEBUG_ASSERT(res == ZX_OK);
    }

    vmar_manager_.reset();
    start_ = 0;
    size_ = 0;
}

zx_status_t VmoMapper::CheckReadyToMap(const fbl::RefPtr<VmarManager>& vmar_manager) {
    if (start_ != 0) {
        return ZX_ERR_BAD_STATE;
    }

    if ((vmar_manager != nullptr) && !vmar_manager->vmar().is_valid()) {
        return ZX_ERR_INVALID_ARGS;
    }

    return ZX_OK;
}

zx_status_t VmoMapper::InternalMap(const zx::vmo& vmo,
                                   uint64_t offset,
                                   uint64_t size,
                                   zx_vm_option_t map_options,
                                   fbl::RefPtr<VmarManager> vmar_manager) {
    ZX_DEBUG_ASSERT(vmo.is_valid());
    ZX_DEBUG_ASSERT(start() == nullptr);
    ZX_DEBUG_ASSERT(size_ == 0);
    ZX_DEBUG_ASSERT(vmar_manager_ == nullptr);

    zx_handle_t vmar_handle = (vmar_manager == nullptr)
        ? zx::vmar::root_self()->get()
        : vmar_manager->vmar().get();

    zx_status_t res = zx_vmar_map(vmar_handle, map_options, 0, vmo.get(), offset, size, &start_);
    if (res != ZX_OK) {
        return res;
    }

    size_ = size;
    vmar_manager_ = fbl::move(vmar_manager);

    return ZX_OK;
}

} // namespace fzl

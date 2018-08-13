// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <lib/fzl/resizeable-vmo-mapper.h>
#include <string.h>

namespace fzl {

fbl::unique_ptr<ResizeableVmoMapper> ResizeableVmoMapper::Create(
        uint64_t size,
        const char* name,
        uint32_t map_options,
        fbl::RefPtr<VmarManager> vmar_manager,
        uint32_t cache_policy) {
    fbl::AllocChecker ac;
    auto ret = fbl::make_unique_checked<ResizeableVmoMapper>(&ac);
    if (!ac.check()) {
        return nullptr;
    }

    zx_status_t res = ret->CreateAndMap(size, name, map_options, vmar_manager, cache_policy);
    if (res != ZX_OK) {
        return nullptr;
    }

    return ret;
}

zx_status_t ResizeableVmoMapper::CreateAndMap(uint64_t size,
                                              const char* name,
                                              zx_vm_option_t map_options,
                                              fbl::RefPtr<VmarManager> vmar_manager,
                                              uint32_t cache_policy) {
    zx::vmo temp;
    zx_status_t res = VmoMapper::CreateAndMap(size,
                                              map_options,
                                              fbl::move(vmar_manager),
                                              &temp,
                                              ZX_RIGHT_SAME_RIGHTS,
                                              cache_policy);

    if (res == ZX_OK) {
        temp.set_property(ZX_PROP_NAME, name, name ? strlen(name) : 0);
        map_options_ = map_options;
        vmo_ = fbl::move(temp);
    }

    return res;
}

zx_status_t ResizeableVmoMapper::Map(zx::vmo vmo,
                                     uint64_t size,
                                     zx_vm_option_t map_options,
                                     fbl::RefPtr<VmarManager> vmar_manager) {
    zx_status_t res = VmoMapper::Map(vmo, 0, size, map_options, vmar_manager);

    if (res == ZX_OK) {
        vmo_ = fbl::move(vmo);
        map_options_ = map_options;
    }

    return res;
}

zx_status_t ResizeableVmoMapper::Shrink(size_t size) {
    if (!vmo_.is_valid()) {
        return ZX_ERR_BAD_STATE;
    } else if (size == 0 || size > size_) {
        return ZX_ERR_INVALID_ARGS;
    } else if (size == size_) {
        return ZX_OK;
    }

    zx_status_t status;
    zx_handle_t vmar_handle = vmar_manager_ ? vmar_manager_->vmar().get() : zx_vmar_root_self();

    // Unmap everything after the offset
    if ((status = zx_vmar_unmap(vmar_handle, start_ + size, size_ - size)) != ZX_OK) {
        return status;
    }
    size_ = size;

    if ((status = vmo_.op_range(ZX_VMO_OP_DECOMMIT, size, size_ - size,
                                nullptr, 0)) != ZX_OK) {
        // We can tolerate this error; from a client's perspective, the VMO
        // still should appear smaller.
        fprintf(stderr, "ResizeableVmoMapper::Shrink: VMO Decommit failed: %d\n", status);
    }

    return ZX_OK;
}

zx_status_t ResizeableVmoMapper::Grow(size_t size) {
    if (!vmo_.is_valid()) {
        return ZX_ERR_BAD_STATE;
    } else if (size < size_) {
        return ZX_ERR_INVALID_ARGS;
    }

    size = fbl::round_up<size_t>(size, ZX_PAGE_SIZE);
    zx_status_t status;

    zx_info_vmar_t vmar_info;
    zx_handle_t vmar_handle = vmar_manager_ ? vmar_manager_->vmar().get() : zx_vmar_root_self();
    if ((status = zx_object_get_info(vmar_handle, ZX_INFO_VMAR,
                                     &vmar_info, sizeof(vmar_info), NULL, NULL)) != ZX_OK) {
        return status;
    }

    if ((status = vmo_.set_size(size)) != ZX_OK) {
        return status;
    }

    // Try to extend mapping
    uintptr_t new_start;
    if ((status = zx_vmar_map(vmar_handle,
                              map_options_ | ZX_VM_FLAG_SPECIFIC,
                              start_ + size_ - vmar_info.base,
                              vmo_.get(),
                              size_,
                              size - size_,
                              &new_start)) != ZX_OK) {
        // If extension fails, create entirely new mapping and unmap the old one
        if ((status = zx_vmar_map(vmar_handle, map_options_, 0,
                                  vmo_.get(), 0, size, &new_start)) != ZX_OK) {

            // If we could not extend the old mapping, and we cannot create a
            // new mapping, then we are done.  Attempt to shrink the VMO back to
            // its original size.  This operation should *never* fail.  If it
            // does, something has gone insanely wrong and it is time to
            // terminate this process.
            zx_status_t stat2 = vmo_.set_size(size_);
            ZX_ASSERT_MSG(stat2 == ZX_OK,
                          "Failed to shrink to original size (0x%zx -> 0x%lx : res %d)\n",
                          size, this->size(), stat2);
            return status;
        }

        // Now that we have a new mapping, unmap our original mapping.  Once
        // again, this should *never* fail.  Hard assert that this is the case.
        status = zx_vmar_unmap(vmar_handle, start_, size_);
        ZX_ASSERT_MSG(status == ZX_OK,
                      "Failed to destroy original mapping ([%p, len 0x%lx] : res %d\n",
                      start(), this->size(), status);

        start_ = new_start;
    }

    size_ = size;
    return ZX_OK;
}

} // namespace fzl

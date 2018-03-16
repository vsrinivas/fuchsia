// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include <zircon/process.h>
#include <zircon/types.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/macros.h>

#if !defined _KERNEL && defined __Fuchsia__
#include <lib/zx/vmo.h>
#include <zircon/syscalls.h>
#endif

namespace bitmap {

class DefaultStorage {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(DefaultStorage);
    DefaultStorage() = default;

    zx_status_t Allocate(size_t size) {
        fbl::AllocChecker ac;
        auto arr = new (&ac) uint8_t[size];
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        storage_.reset(arr, size);
        return ZX_OK;
    }
    void* GetData() { return storage_.get(); }
    const void* GetData() const { return storage_.get(); }
private:
    fbl::Array<uint8_t> storage_;
};

template <size_t N>
class FixedStorage {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(FixedStorage);
    FixedStorage() = default;

    zx_status_t Allocate(size_t size) {
        ZX_ASSERT(size <= N);
        return ZX_OK;
    }
    void* GetData() { return storage_; }
    const void* GetData() const { return storage_; }
private:
    size_t storage_[(N + sizeof(size_t) - 1) / sizeof(size_t)];
};

#if !defined _KERNEL && defined __Fuchsia__
class VmoStorage {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(VmoStorage);
    VmoStorage() :
        vmo_(ZX_HANDLE_INVALID),
        mapped_addr_(0),
        size_(0) {}

    ~VmoStorage() {
        Release();
    }

    zx_status_t Allocate(size_t size) {
        Release();
        size_ = fbl::round_up(size, static_cast<size_t>(PAGE_SIZE));
        zx_status_t status;
        if ((status = zx::vmo::create(size_, 0, &vmo_)) != ZX_OK) {
            return status;
        } else if ((status = zx_vmar_map(zx_vmar_root_self(), 0, vmo_.get(), 0,
                                         size_, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                                         &mapped_addr_)) != ZX_OK) {
            vmo_.reset();
            return status;
        }
        return ZX_OK;
    }

    zx_status_t Grow(size_t size) {
        if (size <= size_) {
            return ZX_OK;
        }

        size = fbl::round_up(size, static_cast<size_t>(PAGE_SIZE));
        zx_status_t status;
        if ((status = vmo_.set_size(size)) != ZX_OK) {
            return status;
        }


        zx_info_vmar_t vmar_info;
        if ((status = zx_object_get_info(zx_vmar_root_self(), ZX_INFO_VMAR,
                                         &vmar_info, sizeof(vmar_info), NULL,
                                         NULL)) != ZX_OK) {
            return status;
        }

        // Try to extend mapping
        uintptr_t addr;
        if ((status = zx_vmar_map(zx_vmar_root_self(), mapped_addr_ + size_ -
                                  vmar_info.base, vmo_.get(), size_, size - size_,
                                  ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE |
                                  ZX_VM_FLAG_SPECIFIC, &addr)) != ZX_OK) {
            // If extension fails, create entirely new mapping and unmap the old one
            if ((status = zx_vmar_map(zx_vmar_root_self(), 0, vmo_.get(), 0, size,
                                      ZX_VM_FLAG_PERM_READ |
                                      ZX_VM_FLAG_PERM_WRITE, &addr)) != ZX_OK) {
                return status;
            }

            if ((status = zx_vmar_unmap(zx_vmar_root_self(), mapped_addr_, size_)) != ZX_OK) {
                return status;
            }

            mapped_addr_ = addr;
        }

        return ZX_OK;
    }

    void* GetData() { ZX_DEBUG_ASSERT(mapped_addr_ != 0); return (void*) mapped_addr_; }
    const void* GetData() const { ZX_DEBUG_ASSERT(mapped_addr_ != 0); return (void*) mapped_addr_; }
    zx_handle_t GetVmo() const { ZX_DEBUG_ASSERT(mapped_addr_ != 0); return vmo_.get(); }
private:
    void Release() {
        if (mapped_addr_ != 0) {
            zx_vmar_unmap(zx_vmar_root_self(), mapped_addr_, size_);
        }
    }
    zx::vmo vmo_;
    uintptr_t mapped_addr_;
    size_t size_;
};
#endif

} // namespace bitmap

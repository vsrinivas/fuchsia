// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pinned-buffer.h"

fbl::RefPtr<PinnedBuffer> PinnedBuffer::Create(size_t size, const zx::bti& bti,
                                               uint32_t cache_policy) {
    fbl::RefPtr<fzl::VmarManager> vmar_mgr;

    if (!bti.is_valid() || (size & (PAGE_SIZE - 1))) {
        return nullptr;
    }

    //create vmar large enough for rx,tx buffers, and rx,tx dma descriptors
    vmar_mgr = fzl::VmarManager::Create(size, nullptr);
    if (!vmar_mgr) {
        zxlogf(ERROR, "pinned-buffer: Creation of vmar manager failed\n");
        return nullptr;
    }

    fbl::AllocChecker ac;

    auto pbuf = fbl::AdoptRef(new (&ac) PinnedBuffer());
    if (!ac.check()) {
        return nullptr;
    }

    zx_status_t status = pbuf->vmo_mapper_.CreateAndMap(size,
                                ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                                fbl::move(vmar_mgr), &pbuf->vmo_,
                                ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_WRITE,
                                cache_policy);
    if (status != ZX_OK) {
        zxlogf(ERROR, "pinned-buffer: vmo creation failed %d\n", status);
        return nullptr;
    }

    uint32_t page_count = static_cast<uint32_t>(size / PAGE_SIZE);

    fbl::unique_ptr<zx_paddr_t[]> addrs(new (&ac) zx_paddr_t[page_count]);
    if (!ac.check()) {
        return nullptr;
    }

    // Now actually pin the region.

    status = bti.pin(ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE,
                     pbuf->vmo_, 0, size, addrs.get(),
                     page_count, &pbuf->pmt_);
    if (status != ZX_OK) {
        pbuf->UnPin();
        return nullptr;
    }

    pbuf->paddrs_.reset(addrs.release());
    return fbl::move(pbuf);
}

zx_status_t PinnedBuffer::UnPin() {
    if ((paddrs_ == nullptr) || !pmt_.is_valid()) {
        return ZX_ERR_BAD_STATE;
    }
    pmt_.unpin();
    paddrs_.reset();
    return ZX_OK;
}

// We need a status here since it is within the realm of possibility that
// the physical address returned could legitimately be 0x00000000, so
// returning a nullptr for a failure won't cut it.
zx_status_t PinnedBuffer::LookupPhys(zx_off_t offset, zx_paddr_t* out) {
    if (paddrs_ == nullptr) {
        return ZX_ERR_BAD_STATE;
    }
    if (offset >= GetSize()) {
        *out = 0;
        return ZX_ERR_INVALID_ARGS;
    }

    *out = paddrs_[offset / PAGE_SIZE] + (offset % PAGE_SIZE);

    return ZX_OK;
}

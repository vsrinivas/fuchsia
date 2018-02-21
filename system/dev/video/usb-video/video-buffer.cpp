// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <string.h>
#include <zx/vmar.h>

#include "video-buffer.h"

namespace video {
namespace usb {

VideoBuffer::~VideoBuffer() {
    if (virt_ != nullptr) {
        zx::vmar::root_self().unmap(reinterpret_cast<uintptr_t>(virt_), size_);
    }
}

zx_status_t VideoBuffer::Create(zx::vmo&& vmo,
                                fbl::unique_ptr<VideoBuffer>* out) {
    if (!vmo.is_valid()) {
        zxlogf(ERROR, "invalid buffer handle\n");
        return ZX_ERR_BAD_HANDLE;
    }

    uint64_t size;
    zx_status_t status = vmo.get_size(&size);
    if (status != ZX_OK) {
        zxlogf(ERROR, "could not get vmo size, err: %d\n", status);
        return status;
    }

    void* virt;
    uint32_t flags =  ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
    status = zx::vmar::root_self().map(0u, vmo,
                                       0u, size,
                                       flags, reinterpret_cast<uintptr_t*>(&virt));

    if (status != ZX_OK) {
        zxlogf(ERROR, "failed to map VMO, got error: %d\n", status);
        return status;
    }

    // Zero out the buffer.
    memset(virt, 0, size);

    fbl::AllocChecker ac;
    fbl::unique_ptr<VideoBuffer> res(
        new (&ac) VideoBuffer(fbl::move(vmo), size, virt));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    *out = fbl::move(res);
    return ZX_OK;
}

} // namespace usb
} // namespace video

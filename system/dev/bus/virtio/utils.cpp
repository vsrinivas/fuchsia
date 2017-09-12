// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utils.h"

#include <ddk/driver.h>
#include <limits.h>
#include <zircon/types.h>
#include <zx/process.h>
#include <zx/vmar.h>
#include <zx/vmo.h>
#include <fbl/auto_call.h>

#include "trace.h"

namespace virtio {

zx_status_t map_contiguous_memory(size_t size, uintptr_t* _va, zx_paddr_t* _pa) {

    zx_handle_t vmo_handle;
    zx_status_t r = zx_vmo_create_contiguous(get_root_resource(), size, 0, &vmo_handle);
    if (r) {
        VIRTIO_ERROR("zx_vmo_create_contiguous failed %d\n", r);
        return r;
    }

    zx::vmo vmo(vmo_handle);

    uintptr_t va;
    const uint32_t flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
    r = zx::vmar::root_self().map(0, vmo, 0, size, flags, &va);
    if (r) {
        VIRTIO_ERROR("zx_process_map_vm failed %d size: %zu\n", r, size);
        return r;
    }

    auto ac = fbl::MakeAutoCall([va, size]() {
        zx::vmar::root_self().unmap(va, size);
    });

// TODO: add when LOCK operation implemented in kernel
#if 0
    r = vmo.op_range(ZX_VMO_OP_LOCK, 0, size, nullptr, 0);
    if (r) {
        VIRTIO_ERROR("zx_vmo_op_range LOCK failed %d\n", r);
        return r;
    }
#endif

    zx_paddr_t pa;
    r = vmo.op_range(ZX_VMO_OP_LOOKUP, 0, PAGE_SIZE, &pa, sizeof(pa));
    if (r) {
        VIRTIO_ERROR("zx_vmo_op_range LOOKUP failed %d\n", r);
        return r;
    }

    ac.cancel();

    *_va = va;
    *_pa = pa;

    return ZX_OK;
}

} // namespace virtio

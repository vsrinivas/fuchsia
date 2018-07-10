// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_mmio.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"

#include <ddk/device.h>
#include <zircon/process.h> // for zx_vmar_root_self

namespace magma {

static_assert(ZX_CACHE_POLICY_CACHED == static_cast<int>(PlatformMmio::CACHE_POLICY_CACHED),
              "enum mismatch");
static_assert(ZX_CACHE_POLICY_UNCACHED == static_cast<int>(PlatformMmio::CACHE_POLICY_UNCACHED),
              "enum mismatch");
static_assert(ZX_CACHE_POLICY_UNCACHED_DEVICE ==
                  static_cast<int>(PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE),
              "enum mismatch");
static_assert(ZX_CACHE_POLICY_WRITE_COMBINING ==
                  static_cast<int>(PlatformMmio::CACHE_POLICY_WRITE_COMBINING),
              "enum mismatch");

ZirconPlatformMmio::ZirconPlatformMmio(void* addr, uint64_t size, zx_handle_t handle)
    : PlatformMmio(addr, size), handle_(handle)
{
}

ZirconPlatformMmio::~ZirconPlatformMmio()
{
    // Clean up the MMIO mapping that was made in the ctor.
    DLOG("ZirconPlatformMmio dtor");
    zx_status_t status =
        zx_vmar_unmap(zx_vmar_root_self(), reinterpret_cast<uintptr_t>(addr()), size());
    if (status != ZX_OK)
        DLOG("error unmapping %p (len %zu): %d\n", addr(), size(), status);
    zx_handle_close(handle_);
}

} // namespace magma

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>

#include <ddk/debug.h>

namespace ddk {

void PDev::ShowInfo() {
  pdev_device_info_t info;
  if (GetDeviceInfo(&info) == ZX_OK) {
    zxlogf(INFO, "VID:PID:DID         = %04x:%04x:%04x", info.vid, info.pid, info.did);
    zxlogf(INFO, "mmio count          = %d", info.mmio_count);
    zxlogf(INFO, "irq count           = %d", info.irq_count);
    zxlogf(INFO, "bti count           = %d", info.bti_count);
  }
}

__WEAK zx_status_t PDev::MapMmio(uint32_t index, std::optional<MmioBuffer>* mmio,
                                 uint32_t cache_policy) {
  pdev_mmio_t pdev_mmio;

  zx_status_t status = GetMmio(index, &pdev_mmio);
  if (status != ZX_OK) {
    return status;
  }
  return PDevMakeMmioBufferWeak(pdev_mmio, mmio, cache_policy);
}

// Regular implementation for drivers. Tests might override this.
[[gnu::weak]] zx_status_t PDevMakeMmioBufferWeak(const pdev_mmio_t& pdev_mmio,
                                                 std::optional<MmioBuffer>* mmio,
                                                 uint32_t cache_policy) {
  return MmioBuffer::Create(pdev_mmio.offset, pdev_mmio.size, zx::vmo(pdev_mmio.vmo), cache_policy,
                            mmio);
}

}  // namespace ddk

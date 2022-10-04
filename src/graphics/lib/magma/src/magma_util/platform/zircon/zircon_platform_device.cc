// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_device.h"

#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>
#include <zircon/process.h>

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_mmio.h"
#include "zircon_platform_handle.h"
#include "zircon_platform_interrupt.h"
#include "zircon_platform_mmio.h"

namespace magma {

bool ZirconPlatformDeviceWithoutProtocol::GetProtocol(uint32_t proto_id, void* proto_out) {
  zx_status_t status = device_get_protocol(zx_device_, proto_id, proto_out);
  if (status != ZX_OK) {
    return DRETF(false, "device_get_protocol for %d failed: %d", proto_id, status);
  }
  return true;
}

Status ZirconPlatformDeviceWithoutProtocol::LoadFirmware(
    const char* filename, std::unique_ptr<PlatformBuffer>* firmware_out, uint64_t* size_out) const {
  zx::vmo vmo;
  size_t size;
  zx_status_t status = load_firmware(zx_device_, filename, vmo.reset_and_get_address(), &size);
  if (status != ZX_OK) {
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failure to load firmware: %d", status);
  }
  *firmware_out = PlatformBuffer::Import(vmo.release());
  *size_out = size;
  return MAGMA_STATUS_OK;
}

std::unique_ptr<PlatformMmio> ZirconPlatformDevice::CpuMapMmio(
    unsigned int index, PlatformMmio::CachePolicy cache_policy) {
  DLOG("CpuMapMmio index %d", index);

  zx_status_t status;
  mmio_buffer_t mmio_buffer;

  status = pdev_map_mmio_buffer(&pdev_, index, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio_buffer);
  if (status != ZX_OK) {
    DRETP(nullptr, "mapping resource failed");
  }

  std::unique_ptr<ZirconPlatformMmio> mmio(new ZirconPlatformMmio(mmio_buffer));

  DLOG("map_mmio index %d cache_policy %d returned: 0x%x", index, static_cast<int>(cache_policy),
       mmio_buffer.vmo);

  zx::bti bti_handle;
  status = pdev_get_bti(&pdev_, 0, bti_handle.reset_and_get_address());
  if (status != ZX_OK)
    return DRETP(nullptr, "failed to get bus transaction initiator for pinning mmio: %d", status);

  if (!mmio->Pin(bti_handle.get()))
    return DRETP(nullptr, "Failed to pin mmio");

  return mmio;
}

std::unique_ptr<PlatformBuffer> ZirconPlatformDevice::GetMmioBuffer(unsigned int index) {
  pdev_mmio_t mmio;

  zx_status_t status = pdev_get_mmio(&pdev_, index, &mmio);
  if (status != ZX_OK)
    return DRETP(nullptr, "pdev_get_mmio failed: %d", status);

  return magma::PlatformBuffer::Import(mmio.vmo);
}

std::unique_ptr<PlatformInterrupt> ZirconPlatformDevice::RegisterInterrupt(unsigned int index) {
  zx_handle_t interrupt_handle;
  zx_status_t status = pdev_get_interrupt(&pdev_, index, 0, &interrupt_handle);
  if (status != ZX_OK)
    return DRETP(nullptr, "register interrupt failed");

  return std::make_unique<ZirconPlatformInterrupt>(zx::handle(interrupt_handle));
}

std::unique_ptr<PlatformHandle> ZirconPlatformDevice::GetBusTransactionInitiator() const {
  zx_handle_t bti_handle;
  zx_status_t status = pdev_get_bti(&pdev_, 0, &bti_handle);
  if (status != ZX_OK)
    return DRETP(nullptr, "failed to get bus transaction initiator");

  return std::make_unique<ZirconPlatformHandle>(zx::handle(bti_handle));
}

std::unique_ptr<PlatformDevice> PlatformDevice::Create(void* device_handle) {
  if (!device_handle)
    return DRETP(nullptr, "device_handle is null, cannot create PlatformDevice");

  zx_device_t* zx_device = static_cast<zx_device_t*>(device_handle);

  pdev_protocol_t pdev;
  zx_status_t status = device_get_protocol(zx_device, ZX_PROTOCOL_PDEV, &pdev);
  if (status != ZX_OK) {
    // if ZX_PROTOCOL_PDEV is not available, try using find via fragment.
    if (device_get_fragment_count(zx_device) > 0) {
      status = device_get_fragment_protocol(zx_device, "pdev", ZX_PROTOCOL_PDEV, &pdev);
    }
  }

  if (status == ZX_ERR_NOT_SUPPORTED) {
    return std::make_unique<ZirconPlatformDeviceWithoutProtocol>(zx_device);
  }

  if (status == ZX_OK) {
    pdev_device_info_t device_info;
    zx_status_t status = pdev_get_device_info(&pdev, &device_info);
    if (status != ZX_OK)
      return DRETP(nullptr, "pdev_get_device_info failed: %d", status);

    return std::make_unique<ZirconPlatformDevice>(zx_device, pdev, device_info.mmio_count);
  }

  return DRETP(nullptr, "Error requesting protocol: %d", status);
}

}  // namespace magma

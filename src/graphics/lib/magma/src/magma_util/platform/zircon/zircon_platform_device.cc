// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_device.h"

#include <lib/device-protocol/platform-device.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>
#include <zircon/process.h>

#include <ddk/device.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/protocol/composite.h>

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_mmio.h"
#include "zircon_platform_handle.h"
#include "zircon_platform_interrupt.h"
#include "zircon_platform_mmio.h"

namespace magma {

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

std::unique_ptr<PlatformHandle> ZirconPlatformDeviceWithoutProtocol::GetSchedulerProfile(
    Priority priority, const char* name) const {
  zx_handle_t handle;
  zx_status_t status = device_get_profile(zx_device_, priority, name, &handle);
  if (status != ZX_OK)
    return DRETP(nullptr, "Failed to get profile: %d", status);
  return PlatformHandle::Create(handle);
}

std::unique_ptr<PlatformHandle> ZirconPlatformDeviceWithoutProtocol::GetDeadlineSchedulerProfile(
    std::chrono::nanoseconds capacity_ns, std::chrono::nanoseconds deadline_ns,
    std::chrono::nanoseconds period_ns, const char* name) const {
  zx_handle_t handle;
  zx_status_t status = device_get_deadline_profile(
      zx_device_, capacity_ns.count(), deadline_ns.count(), period_ns.count(), name, &handle);
  if (status != ZX_OK)
    return DRETP(nullptr, "Failed to get profile: %d", status);
  return PlatformHandle::Create(handle);
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
    // if ZX_PROTOCOL_PDEV is not available, try using composite protocol.
    composite_protocol_t composite;
    if (device_get_protocol(zx_device, ZX_PROTOCOL_COMPOSITE, &composite) == ZX_OK) {
      zx_device_t* pdev_device;
      size_t actual;
      composite_get_components(&composite, &pdev_device, 1, &actual);
      if (actual == 1) {
        status = device_get_protocol(pdev_device, ZX_PROTOCOL_PDEV, &pdev);
      }
    }
  }
  switch (status) {
    case ZX_OK:
      return std::unique_ptr<PlatformDevice>(new ZirconPlatformDevice(zx_device, pdev));
    case ZX_ERR_NOT_SUPPORTED:
      return std::unique_ptr<PlatformDevice>(new ZirconPlatformDeviceWithoutProtocol(zx_device));
    default:
      return DRETP(nullptr, "Error requesting protocol: %d", status);
  }
}

}  // namespace magma

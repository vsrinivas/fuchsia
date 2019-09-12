// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <zircon/errors.h>
#include <zircon/syscalls/object.h>

#include <array>
#include <cinttypes>
#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/composite.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/sysmem.h>

namespace amlogic_secure_mem {

enum : size_t {
  kComponentPDev,
  kComponentSysmem,
  kComponentCount,
};

zx_status_t AmlogicSecureMemDevice::Create(void* ctx, zx_device_t* parent) {
  std::unique_ptr<AmlogicSecureMemDevice> sec_mem(new AmlogicSecureMemDevice(parent));

  zx_status_t status = sec_mem->Bind();
  if (status == ZX_OK) {
    // devmgr should now own the lifetime
    __UNUSED auto ptr = sec_mem.release();
  }

  return status;
}

zx_status_t AmlogicSecureMemDevice::Bind() {
  zx_status_t status = ZX_OK;

  ddk::CompositeProtocolClient composite(parent());
  if (!composite.is_valid()) {
    zxlogf(ERROR, "%s::%s: Unable to get composite protocol\n", kDeviceName, __FUNCTION__);
    return status;
  }

  std::array<zx_device_t*, kComponentCount> components;
  size_t actual_count;
  composite.GetComponents(components.data(), components.size(), &actual_count);
  if (actual_count != countof(components)) {
    zxlogf(ERROR, "%s::%s: Unable to composite_get_components()\n", kDeviceName, __FUNCTION__);
    return ZX_ERR_INTERNAL;
  }

  ddk::PDevProtocolClient pdev(components[kComponentPDev]);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s::%s: Unable to get pdev protocol\n", kDeviceName, __FUNCTION__);
    return status;
  }

  ddk::SysmemProtocolClient sysmem(components[kComponentSysmem]);
  if (!sysmem.is_valid()) {
    zxlogf(ERROR, "%s::%s: Unable to get sysmem protocol\n", kDeviceName, __FUNCTION__);
    return status;
  }

  // See note on the constraints of |bti_| in the header.
  constexpr uint32_t kBtiIndex = 0;
  status = pdev.GetBti(kBtiIndex, &bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s::%s: Unable to get bti handle\n", kDeviceName, __FUNCTION__);
    return status;
  }

  status = DdkAdd(kDeviceName);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s::%s: Failed to add device\n", kDeviceName, __FUNCTION__);
    return status;
  }

  return status;
}

zx_status_t AmlogicSecureMemDevice::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  llcpp::fuchsia::hardware::securemem::Device::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void AmlogicSecureMemDevice::GetSecureMemoryPhysicalAddress(
    zx::vmo secure_mem, GetSecureMemoryPhysicalAddressCompleter::Sync completer) {
  auto result = GetSecureMemoryPhysicalAddress(std::move(secure_mem));
  if (result.is_error()) {
    completer.Reply(result.error(), static_cast<zx_paddr_t>(0));
  }

  completer.Reply(ZX_OK, result.value());
}

fit::result<zx_paddr_t, zx_status_t> AmlogicSecureMemDevice::GetSecureMemoryPhysicalAddress(
    zx::vmo secure_mem) {
  ZX_DEBUG_ASSERT(secure_mem.is_valid());
  ZX_ASSERT(bti_.is_valid());

  // Validate that the VMO handle passed meets additional constraints.
  zx_info_vmo_t secure_mem_info;
  zx_status_t status = secure_mem.get_info(ZX_INFO_VMO, reinterpret_cast<void*>(&secure_mem_info),
                                           sizeof(secure_mem_info), nullptr, nullptr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s::%s: Failed to get VMO info (status %d)\n", kDeviceName, __FUNCTION__,
           status);
    return fit::error(status);
  }

  // Only allow pinning on VMOs that are contiguous.
  if ((secure_mem_info.flags & ZX_INFO_VMO_CONTIGUOUS) != ZX_INFO_VMO_CONTIGUOUS) {
    zxlogf(ERROR, "%s::%s: Received non-contiguous VMO type to pin\n", kDeviceName, __FUNCTION__);
    return fit::error(ZX_ERR_WRONG_TYPE);
  }

  // Pin the VMO to get the physical address.
  zx_paddr_t paddr;
  zx::pmt pmt;
  status = bti_.pin(ZX_BTI_CONTIGUOUS, secure_mem, 0 /* offset */, secure_mem_info.size_bytes,
                    &paddr, 1u, &pmt);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s::%s: Failed to pin memory (status: %d)\n", kDeviceName, __FUNCTION__, status);
    return fit::error(status);
  }

  // Unpinning the PMT should never fail
  status = pmt.unpin();
  ZX_DEBUG_ASSERT(status == ZX_OK);

  return fit::ok(paddr);
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = AmlogicSecureMemDevice::Create;
  return ops;
}();

}  // namespace amlogic_secure_mem

// clang-format off
ZIRCON_DRIVER_BEGIN(amlogic_secure_mem, amlogic_secure_mem::driver_ops, "zircon", "0.1", 4)
  BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D2),
  BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_SECURE_MEM),
ZIRCON_DRIVER_END(amlogic_secure_mem)

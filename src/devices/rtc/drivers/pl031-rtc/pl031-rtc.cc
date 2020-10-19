// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pl031-rtc.h"

#include <lib/device-protocol/platform-device.h>
#include <librtc.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/device.h>

namespace rtc {

zx_status_t Pl031::Bind(void* /*unused*/, zx_device_t* dev) {
  pdev_protocol_t proto;
  zx_status_t status = device_get_protocol(dev, ZX_PROTOCOL_PDEV, &proto);
  if (status != ZX_OK) {
    return status;
  }

  auto pl031_device = std::make_unique<Pl031>(dev);

  // Carve out some address space for this device.
  status = pdev_map_mmio_buffer(&proto, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE, &pl031_device->mmio_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "pl031_rtc: bind failed to pdev_map_mmio.");
    return status;
  }

  pl031_device->regs_ = (MMIO_PTR Pl031Regs*)pl031_device->mmio_.vaddr;

  status = pl031_device->DdkAdd(ddk::DeviceAddArgs("rtc").set_proto_id(ZX_PROTOCOL_RTC));
  if (status != ZX_OK) {
    zxlogf(ERROR, "pl031_rtc: error adding device");
    return status;
  }

  // The object is owned by the DDK, now that it has been added. It will be deleted
  // when the device is released.
  __UNUSED auto ptr = pl031_device.release();

  return status;
}

Pl031::Pl031(zx_device_t* parent) : RtcDeviceType(parent) {}

zx_status_t Pl031::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_rtc_Device_dispatch(this, txn, msg, Ops());
}

void Pl031::DdkRelease() { delete this; }

zx_status_t Pl031::FidlGetRtc(fidl_txn_t* txn) {
  fuchsia_hardware_rtc_Time rtc;
  seconds_to_rtc(MmioRead32(&regs_->dr), &rtc);
  return fuchsia_hardware_rtc_DeviceGet_reply(txn, &rtc);
}

zx_status_t Pl031::FidlSetRtc(const fuchsia_hardware_rtc_Time* rtc, fidl_txn_t* txn) {
  if (rtc_is_invalid(rtc)) {
    return fuchsia_hardware_rtc_DeviceSet_reply(txn, ZX_ERR_OUT_OF_RANGE);
  }

  MmioWrite32((uint32_t)seconds_since_epoch(rtc), &regs_->lr);

  // Set the UTC offset.
  uint64_t const kRtcNanoseconds = seconds_since_epoch(rtc) * 1000000000;
  int64_t const kUtcOffset = kRtcNanoseconds - zx_clock_get_monotonic();

  // TODO(fxb/31358): Replace get_root_resource().
  zx_status_t const kStatus = zx_clock_adjust(get_root_resource(), ZX_CLOCK_UTC, kUtcOffset);

  if (kStatus != ZX_OK) {
    zxlogf(ERROR, "The RTC driver was unable to set the UTC clock!");
  }

  return fuchsia_hardware_rtc_DeviceSet_reply(txn, kStatus);
}

zx_driver_ops_t pl031_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = Pl031::Bind,
};

}  // namespace rtc

// clang-format off
ZIRCON_DRIVER_BEGIN(pl031, rtc::pl031_driver_ops, "zircon", "0.1", 3)
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
  BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_RTC_PL031),
ZIRCON_DRIVER_END(pl031)
    // clang-format on

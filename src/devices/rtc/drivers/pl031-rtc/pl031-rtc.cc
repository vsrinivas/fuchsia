// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pl031-rtc.h"

#include <lib/device-protocol/platform-device.h>
#include <librtc_llcpp.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/fidl.h>

#include "src/devices/rtc/drivers/pl031-rtc/pl031_rtc_bind.h"

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

  // Retrieve and sanitize the RTC value. Set the RTC to the value.
  FidlRtc::Time rtc = SecondsToRtc(MmioRead32(&pl031_device->regs_->dr));
  rtc = SanitizeRtc(rtc);
  pl031_device->SetRtc(rtc);

  // The object is owned by the DDK, now that it has been added. It will be deleted
  // when the device is released.
  __UNUSED auto ptr = pl031_device.release();

  return status;
}

Pl031::Pl031(zx_device_t* parent) : RtcDeviceType(parent) {}

void Pl031::Get(GetCompleter::Sync& completer) {
  FidlRtc::Time rtc = SecondsToRtc(MmioRead32(&regs_->dr));
  completer.Reply(rtc);
}

void Pl031::Set(FidlRtc::Time rtc, SetCompleter::Sync& completer) { completer.Reply(SetRtc(rtc)); }

zx_status_t Pl031::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  FidlRtc::Device::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void Pl031::DdkRelease() { delete this; }

zx_status_t Pl031::SetRtc(FidlRtc::Time rtc) {
  if (!IsRtcValid(rtc)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  MmioWrite32((uint32_t)SecondsSinceEpoch(rtc), &regs_->lr);

  // Set the UTC offset.
  uint64_t const kRtcNanoseconds = SecondsSinceEpoch(rtc) * 1000000000;
  int64_t const kUtcOffset = kRtcNanoseconds - zx_clock_get_monotonic();

  // TODO(fxb/31358): Replace get_root_resource().
  zx_status_t const kStatus = zx_clock_adjust(get_root_resource(), ZX_CLOCK_UTC, kUtcOffset);
  if (kStatus != ZX_OK) {
    zxlogf(ERROR, "The RTC driver was unable to set the UTC clock!");
  }

  return ZX_OK;
}

zx_driver_ops_t pl031_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = Pl031::Bind,
};

}  // namespace rtc

ZIRCON_DRIVER(pl031, rtc::pl031_driver_ops, "zircon", "0.1")

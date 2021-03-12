// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/rtc/drivers/pl031-rtc/pl031-rtc.h"

#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/device-protocol/pdev.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/fidl.h>

#include "src/devices/rtc/drivers/pl031-rtc/pl031_rtc_bind.h"
#include "src/devices/rtc/lib/rtc/include/librtc_llcpp.h"

namespace rtc {

zx_status_t Pl031::Bind(void* /*unused*/, zx_device_t* dev) {
  ddk::PDev pdev(dev);
  if (!pdev.is_valid()) {
    return ZX_ERR_NO_RESOURCES;
  }

  // Carve out some address space for this device.
  std::optional<ddk::MmioBuffer> mmio;
  zx_status_t status = pdev.MapMmio(0, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to map mmio: %s", __func__, zx_status_get_string(status));
    return status;
  }

  auto pl031_device = std::make_unique<Pl031>(dev, *std::move(mmio));

  status = pl031_device->DdkAdd(ddk::DeviceAddArgs("rtc").set_proto_id(ZX_PROTOCOL_RTC));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s error adding device: %s", __func__, zx_status_get_string(status));
    return status;
  }

  // Retrieve and sanitize the RTC value. Set the RTC to the value.
  FidlRtc::wire::Time rtc = SecondsToRtc(MmioRead32(&pl031_device->regs_->dr));
  rtc = SanitizeRtc(rtc);
  status = pl031_device->SetRtc(rtc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to set rtc: %s", __func__, zx_status_get_string(status));
  }

  // The object is owned by the DDK, now that it has been added. It will be deleted
  // when the device is released.
  __UNUSED auto ptr = pl031_device.release();

  return status;
}

Pl031::Pl031(zx_device_t* parent, ddk::MmioBuffer mmio)
    : RtcDeviceType(parent),
      mmio_(std::move(mmio)),
      regs_(reinterpret_cast<MMIO_PTR Pl031Regs*>(mmio_.get())) {}

void Pl031::Get(GetCompleter::Sync& completer) {
  FidlRtc::wire::Time rtc = SecondsToRtc(MmioRead32(&regs_->dr));
  completer.Reply(rtc);
}

void Pl031::Set(FidlRtc::wire::Time rtc, SetCompleter::Sync& completer) {
  completer.Reply(SetRtc(rtc));
}

zx_status_t Pl031::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  FidlRtc::Device::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void Pl031::DdkRelease() { delete this; }

zx_status_t Pl031::SetRtc(FidlRtc::wire::Time rtc) {
  if (!IsRtcValid(rtc)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  MmioWrite32(static_cast<uint32_t>(SecondsSinceEpoch(rtc)), &regs_->lr);

  return ZX_OK;
}

zx_driver_ops_t pl031_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = Pl031::Bind,
};

}  // namespace rtc

ZIRCON_DRIVER(pl031, rtc::pl031_driver_ops, "zircon", "0.1");

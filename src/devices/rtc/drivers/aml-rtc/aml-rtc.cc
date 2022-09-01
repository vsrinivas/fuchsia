// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/rtc/drivers/aml-rtc/aml-rtc.h"

#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/pdev.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <ddktl/fidl.h>

#include "src/devices/rtc/drivers/aml-rtc/aml_rtc_bind.h"
#include "src/devices/rtc/lib/rtc/include/librtc_llcpp.h"

namespace rtc {

zx_status_t AmlRtc::Bind(void* ctx, zx_device_t* device) {
  uint32_t reg_val;
  ddk::PDev pdev(device);
  if (!pdev.is_valid()) {
    return ZX_ERR_NO_RESOURCES;
  }

  pdev.ShowInfo();

  std::optional<fdf::MmioBuffer> mmio;
  zx_status_t status = pdev.MapMmio(0, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to map MMIO: %s", zx_status_get_string(status));
    return status;
  }

  mmio->SetBit<uint32_t>(RTC_OSC_SEL_BIT, RTC_CTRL);

  /* Set RTC osillator to freq_out to freq_in/((N0*M0+N1*M1)/(M0+M1)) */
  reg_val = mmio->Read32(RTC_OSCIN_CTRL0);
  reg_val &= (~(0x3 << FREQ_OUT_SELECT));
  reg_val |= (0x1 << FREQ_OUT_SELECT);
  /* Enable clock_in gate of osillator 24MHz */
  reg_val |= (1 << CLK_IN_GATE_EN);
  /* N0 is set to 733, N1 is set to 732 by default */
  mmio->Write32(reg_val, RTC_OSCIN_CTRL0);
  /* Set M0 to 2, M1 to 3, so freq_out = 32768 Hz */
  reg_val = mmio->Read32(RTC_OSCIN_CTRL1);
  reg_val &= ~(0xfff);
  reg_val |= (0x1 << CLK_DIV_M0);
  reg_val &= ~(0xfff << CLK_DIV_M1);
  reg_val |= (0x2 << CLK_DIV_M1);
  mmio->Write32(reg_val, RTC_OSCIN_CTRL1);

  /* Enable RTC, which Requires a delay to take effect */
  /* Referring to the RTC code in Linux, the delay range is 100us~200us */
  /* Tested in fuchsia, A minimum 5us delay is required for the RTC to work correctly */
  mmio->SetBit<uint32_t>(RTC_ENABLE_BIT, RTC_CTRL);
  usleep(5);

  auto amlrtc_device = std::make_unique<AmlRtc>(device, *std::move(mmio));

  // Retrieve and sanitize the RTC value. Set the RTC to the value.
  FidlRtc::wire::Time rtc = SecondsToRtc(MmioRead32(&amlrtc_device->regs_->real_time));
  rtc = SanitizeRtc(device, rtc);
  status = amlrtc_device->SetRtc(rtc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to set rtc: %s", zx_status_get_string(status));
  }

  status = amlrtc_device->DdkAdd(ddk::DeviceAddArgs("aml-rtc").set_proto_id(ZX_PROTOCOL_RTC));
  if (status != ZX_OK) {
    zxlogf(ERROR, "error adding device: %s", zx_status_get_string(status));
    return status;
  }
  // The object is owned by the DDK, now that it has been added. It will be deleted
  // when the device is released.
  __UNUSED auto ptr = amlrtc_device.release();

  return status;
}

AmlRtc::AmlRtc(zx_device_t* parent, fdf::MmioBuffer mmio)
    : RtcDeviceType(parent),
      mmio_(std::move(mmio)),
      regs_(reinterpret_cast<MMIO_PTR AmlRtcRegs*>(mmio_.get())) {}

void AmlRtc::Get(GetCompleter::Sync& completer) {
  FidlRtc::wire::Time rtc = SecondsToRtc(MmioRead32(&regs_->real_time));
  completer.Reply(rtc);
}

void AmlRtc::Set(SetRequestView request, SetCompleter::Sync& completer) {
  completer.Reply(SetRtc(request->rtc));
}

void AmlRtc::DdkRelease() { delete this; }

zx_status_t AmlRtc::SetRtc(FidlRtc::wire::Time rtc) {
  if (!IsRtcValid(rtc)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  MmioWrite32(static_cast<uint32_t>(SecondsSinceEpoch(rtc)), &regs_->counter);

  return ZX_OK;
}

zx_driver_ops_t amlrtc_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = AmlRtc::Bind,
};

}  // namespace rtc

ZIRCON_DRIVER(amlrtc, rtc::amlrtc_driver_ops, "zircon", "0.1");

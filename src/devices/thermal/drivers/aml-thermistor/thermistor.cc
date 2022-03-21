// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thermistor.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/pdev.h>
#include <lib/fit/defer.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/interrupt.h>
#include <string.h>
#include <zircon/types.h>

#include <fbl/ref_counted.h>
#include <soc/aml-common/aml-g12-saradc.h>

#include "src/devices/thermal/drivers/aml-thermistor/aml-thermistor-bind.h"

namespace thermal {

static constexpr uint32_t kMaxNtcChannels = 4;
static constexpr uint32_t kMaxAdcChannels = 4;

zx_status_t AmlThermistor::Create(void* ctx, zx_device_t* parent) {
  zx_status_t status;

  std::unique_ptr<AmlThermistor> device(new AmlThermistor(parent));

  if ((status = device->DdkAdd(
                    ddk::DeviceAddArgs("thermistor-device").set_flags(DEVICE_ADD_NON_BINDABLE)) !=
                ZX_OK)) {
    zxlogf(ERROR, "%s: DdkAdd failed", __func__);
    return status;
  }
  __UNUSED auto* unused = device.release();

  return ZX_OK;
}

zx_status_t AmlThermistor::InitPdev() {
  zx_status_t status;

  ddk::PDev pdev(parent());
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s: failed to get pdev", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  std::optional<fdf::MmioBuffer> adc_mmio;
  status = pdev.MapMmio(0, &adc_mmio);
  if (status != ZX_OK) {
    return status;
  }
  std::optional<fdf::MmioBuffer> ao_mmio;
  status = pdev.MapMmio(1, &ao_mmio);
  if (status != ZX_OK) {
    return status;
  }

  zx::interrupt irq;
  status = pdev.GetInterrupt(0, &irq);
  if (status != ZX_OK) {
    zxlogf(ERROR, "dwmac: could not map dma interrupt");
    return status;
  }

  saradc_ = fbl::MakeRefCounted<AmlSaradcDevice>(*std::move(adc_mmio), *std::move(ao_mmio),
                                                 std::move(irq));
  return ZX_OK;
}

zx_status_t AmlThermistor::AddThermChannel(NtcChannel ch, NtcInfo info) {
  zx_status_t status;

  std::unique_ptr<ThermistorChannel> dev(
      new ThermistorChannel(zxdev(), saradc_, ch.adc_channel, info, ch.pullup_ohms));

  status = dev->DdkAdd(ddk::DeviceAddArgs(ch.name));
  if (status != ZX_OK) {
    return status;
  }
  __UNUSED auto ptr = dev.release();
  return ZX_OK;
}

zx_status_t AmlThermistor::AddRawChannel(uint32_t adc_chan) {
  std::unique_ptr<RawChannel> dev(new RawChannel(zxdev(), saradc_, adc_chan));

  char name[20];
  snprintf(name, sizeof(name), "adc-%d", adc_chan);

  zx_status_t status = dev->DdkAdd(ddk::DeviceAddArgs(name));
  if (status != ZX_OK) {
    return status;
  }
  __UNUSED auto ptr = dev.release();

  return ZX_OK;
}

void AmlThermistor::DdkInit(ddk::InitTxn txn) {
  zx_status_t status = ZX_OK;

  status = InitPdev();
  if (status != ZX_OK) {
    txn.Reply(status);
    return;
  }

  saradc_->HwInit();

  auto on_error = fit::defer([this]() { saradc_->Shutdown(); });

  NtcChannel ntc_channels[kMaxNtcChannels];
  size_t actual;

  status =
      DdkGetMetadata(NTC_CHANNELS_METADATA_PRIVATE, &ntc_channels, sizeof(ntc_channels), &actual);
  if (status != ZX_OK) {
    txn.Reply(status);
    return;
  }

  ZX_DEBUG_ASSERT(actual % sizeof(NtcChannel) == 0);
  size_t num_channels = actual / sizeof(NtcChannel);

  NtcInfo ntc_info[kMaxNtcChannels];
  status = DdkGetMetadata(NTC_PROFILE_METADATA_PRIVATE, &ntc_info, sizeof(ntc_info), &actual);
  if (status != ZX_OK) {
    txn.Reply(status);
    return;
  }
  ZX_DEBUG_ASSERT(actual % sizeof(NtcInfo) == 0);
  size_t num_profiles = actual / sizeof(NtcInfo);

  for (uint32_t i = 0; i < num_channels; i++) {
    if (ntc_channels[i].profile_idx >= num_profiles) {
      txn.Reply(ZX_ERR_INVALID_ARGS);
      return;
    }
    status = AddThermChannel(ntc_channels[i], ntc_info[ntc_channels[i].profile_idx]);
    if (status != ZX_OK) {
      txn.Reply(status);
      return;
    }
  }

  // Expose all the adc channels via adc protocol
  //  this includes channels which may not have a thermistor.
  for (uint32_t i = 0; i < kMaxAdcChannels; i++) {
    status = AddRawChannel(i);
    if (status != ZX_OK) {
      txn.Reply(status);
      return;
    }
  }
  txn.Reply(ZX_OK);
  on_error.cancel();
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = AmlThermistor::Create;
  return ops;
}();

}  // namespace thermal

// clang-format off
ZIRCON_DRIVER(aml-thermistor, thermal::driver_ops, "thermistor", "0.1");

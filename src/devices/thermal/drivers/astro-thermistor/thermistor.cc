// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thermistor.h"

#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/interrupt.h>
#include <string.h>
#include <zircon/types.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <fbl/auto_call.h>
#include <fbl/ref_counted.h>
#include <soc/aml-common/aml-g12-saradc.h>

namespace thermal {

static constexpr uint32_t kMaxNtcChannels = 4;

zx_status_t AstroThermistor::Create(void* ctx, zx_device_t* parent) {
  zx_status_t status;

  std::unique_ptr<AstroThermistor> device(new AstroThermistor(parent));

  if ((status = device->DdkAdd("thermistor-device") != ZX_OK)) {
    zxlogf(ERROR, "%s: DdkAdd failed", __func__);
    return status;
  }
  __UNUSED auto* unused = device.release();

  return ZX_OK;
}

zx_status_t AstroThermistor::InitPdev() {
  zx_status_t status;

  ddk::PDev pdev(parent());
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s: failed to get pdev", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  std::optional<ddk::MmioBuffer> adc_mmio;
  status = pdev.MapMmio(0, &adc_mmio);
  if (status != ZX_OK) {
    return status;
  }
  std::optional<ddk::MmioBuffer> ao_mmio;
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

zx_status_t AstroThermistor::AddThermChannel(NtcChannel ch, NtcInfo info) {
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

zx_status_t AstroThermistor::AddRawChannel(uint32_t adc_chan) {
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

void AstroThermistor::DdkInit(ddk::InitTxn txn) {
  zx_status_t status = ZX_OK;

  status = InitPdev();
  if (status != ZX_OK) {
    txn.Reply(status);
    return;
  }

  saradc_->HwInit();

  auto on_error = fbl::MakeAutoCall([this]() { saradc_->Shutdown(); });

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
  ops.bind = AstroThermistor::Create;
  return ops;
}();

}  // namespace thermal

// clang-format off
ZIRCON_DRIVER_BEGIN(astro-thermistor, thermal::driver_ops, "thermistor", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GOOGLE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_ASTRO),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_ASTRO_THERMISTOR),
ZIRCON_DRIVER_END(astro-thermistor)

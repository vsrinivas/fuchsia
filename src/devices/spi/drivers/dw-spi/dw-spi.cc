// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dw-spi.h"

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <fuchsia/hardware/spiimpl/c/banjo.h>
#include <lib/device-protocol/pdev.h>
#include <lib/device-protocol/platform-device.h>
#include <string.h>
#include <threads.h>
#include <zircon/types.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <hw/reg.h>

#include "registers.h"
#include "src/devices/spi/drivers/dw-spi/dw_spi-bind.h"

namespace spi {

void DwSpi::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void DwSpi::DdkRelease() { delete this; }

zx_status_t DwSpi::SpiImplExchange(uint32_t cs, const uint8_t* txdata, size_t txdata_size,
                                   uint8_t* out_rxdata, size_t rxdata_size,
                                   size_t* out_rxdata_actual) {
  if (cs >= SpiImplGetChipSelectCount()) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (txdata_size && rxdata_size && (txdata_size != rxdata_size)) {
    return ZX_ERR_INVALID_ARGS;
  }

  Enable::Get().FromValue(0).WriteTo(&mmio_);

  Ctrl0::Get()
      .FromValue(0)
      .set_dfs(7)  // 8 bits per word (bits)
      .set_frf(Ctrl0::FRF_SPI)
      .set_scph(0)
      .set_scol(0)
      .set_tmod(Ctrl0::TMOD_TR)
      .WriteTo(&mmio_);

  ChipEnable::Get().FromValue(1 << cs).WriteTo(&mmio_);

  Enable::Get().FromValue(1).WriteTo(&mmio_);

  const uint8_t* tx = txdata;
  uint8_t* rx = out_rxdata;

  size_t exchange_size = txdata_size ? txdata_size : rxdata_size;
  size_t tx_done = 0;
  size_t rx_done = 0;

  while (tx_done < exchange_size || rx_done < exchange_size) {
    // load TX FIFO
    while (tx_done < exchange_size && Status::Get().ReadFrom(&mmio_).tf_not_full()) {
      uint8_t byte = tx ? *tx++ : 0xff;
      Data::Get().FromValue(byte).WriteTo(&mmio_);
      tx_done++;
    }

    // read RX FIFO
    while (rx_done < exchange_size && Status::Get().ReadFrom(&mmio_).rf_not_empty()) {
      uint8_t byte = static_cast<uint8_t>(Data::Get().ReadFrom(&mmio_).reg_value());
      if (rx) {
        *rx++ = byte;
      }
      rx_done++;
    }
  }

  if (rx) {
    *out_rxdata_actual = rxdata_size;
  }

  return ZX_OK;
}

zx_status_t DwSpi::SpiImplRegisterVmo(uint32_t cs, uint32_t vmo_id, zx::vmo vmo, uint64_t offset,
                                      uint64_t size) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t DwSpi::SpiImplUnregisterVmo(uint32_t cs, uint32_t vmo_id, zx::vmo* out_vmo) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t DwSpi::SpiImplTransmitVmo(uint32_t chip_select, uint32_t vmo_id, uint64_t offset,
                                      uint64_t size) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t DwSpi::SpiImplReceiveVmo(uint32_t chip_select, uint32_t vmo_id, uint64_t offset,
                                     uint64_t size) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t DwSpi::SpiImplExchangeVmo(uint32_t cs, uint32_t tx_vmo_id, uint64_t tx_offset,
                                      uint32_t rx_vmo_id, uint64_t rx_offset, uint64_t size) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t DwSpi::Create(void* ctx, zx_device_t* parent) {
  ddk::PDev pdev(parent);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s: Failed to get ZX_PROTOCOL_PDEV", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  pdev_device_info_t info;
  zx_status_t status = pdev.GetDeviceInfo(&info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: pdev_get_device_info failed", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (info.mmio_count != info.irq_count) {
    zxlogf(ERROR, "%s: mmio_count %u does not match irq_count %u", __func__, info.mmio_count,
           info.irq_count);
    status = ZX_ERR_INVALID_ARGS;
    return status;
  }

  for (uint32_t i = 0; i < info.mmio_count; i++) {
    std::optional<ddk::MmioBuffer> mmio;
    if ((status = pdev.MapMmio(i, &mmio)) != ZX_OK) {
      zxlogf(ERROR, "%s: MapMmio failed %d", __func__, status);
      return status;
    }

    // hardware setup
    Enable::Get().FromValue(0).WriteTo(&*mmio);
    IMR::Get().FromValue(0xff).WriteTo(&*mmio);
    Enable::Get().FromValue(1).WriteTo(&*mmio);

    TXFLTR::Get().FromValue(0).WriteTo(&*mmio);

    // timing hardcoded for bringup
    Enable::Get().FromValue(0).WriteTo(&*mmio);
    BaudRate::Get().FromValue(100).WriteTo(&*mmio);
    Enable::Get().FromValue(1).WriteTo(&*mmio);

    fbl::AllocChecker ac;
    auto* spi = new (&ac) DwSpi(parent, *std::move(mmio));
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }

    auto cleanup = fbl::MakeAutoCall([&spi]() { spi->DdkRelease(); });

    char devname[32];
    sprintf(devname, "dw-spi-%d", i);

    status = spi->DdkAdd(devname);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: DdkAdd failed for %s", __func__, devname);
      return status;
    }

    status = spi->DdkAddMetadata(DEVICE_METADATA_PRIVATE, &i, sizeof i);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: DdkAddMetadata failed for %s", __func__, devname);
      return status;
    }

    cleanup.cancel();
  }

  return ZX_OK;
}

static zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = DwSpi::Create;
  return ops;
}();

}  // namespace spi

// clang-format off
ZIRCON_DRIVER(dw_spi, spi::driver_ops, "zircon", "0.1");


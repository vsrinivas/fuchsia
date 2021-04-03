// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mtk-spi.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/pdev.h>
#include <lib/fit/defer.h>
#include <unistd.h>

#include <ddk/metadata/spi.h>
#include <fbl/alloc_checker.h>

#include "registers.h"
#include "src/devices/spi/drivers/mtk-spi/mtk_spi_bind.h"

namespace spi {

namespace {

constexpr size_t kFifoAccessSize = 4;
constexpr size_t kMaxFifoSize = 32;  // Depth of FIFO is 32 bytes.
constexpr uint32_t kDummy = 0xFFFFFFFF;

}  // namespace

void MtkSpi::FifoTransferPacket(const uint8_t** tx, uint8_t** rx, size_t packet_size) {
  // Transfer
  {  // Fill FIFO
    for (size_t bytes_left = packet_size; bytes_left > 0;) {
      const size_t transfer_size = std::min(kFifoAccessSize, bytes_left);

      if (*tx) {
        uint32_t i32 = 0;
        memcpy(&i32, *tx, transfer_size);
        mmio_.Write32(i32, MTK_SPI_TX_DATA);
        *tx += transfer_size;
      } else {
        mmio_.Write32(kDummy, MTK_SPI_TX_DATA);
      }

      bytes_left -= transfer_size;
    }

    // Enable transfer
    CmdReg::Get().ReadFrom(&mmio_).set_activate(1).WriteTo(&mmio_);
  }

  // Wait for completion
  auto busy = Status1Reg::Get().ReadFrom(&mmio_);
  while (!busy.busy()) {
    busy.ReadFrom(&mmio_);
  };

  // Receive
  {
    for (size_t bytes_left = packet_size; bytes_left > 0;) {
      const size_t transfer_size = std::min(kFifoAccessSize, bytes_left);

      auto data = mmio_.Read32(MTK_SPI_RX_DATA);
      if (*rx) {
        memcpy(*rx, &data, transfer_size);
        *rx += transfer_size;
      }

      bytes_left -= transfer_size;
    }
  }
}

zx_status_t MtkSpi::FifoExchange(const uint8_t* txdata, uint8_t* out_rxdata, size_t data_size) {
  CmdReg::Get().ReadFrom(&mmio_).set_tx_dma_en(0).set_rx_dma_en(0).WriteTo(&mmio_);  // Disable DMA

  // Setup packet
  auto packet_size = std::min(kMaxFifoSize, data_size);
  auto packet_loop = static_cast<uint32_t>(data_size / packet_size);
  Cfg1Reg::Get()
      .ReadFrom(&mmio_)
      .set_packet_length(static_cast<uint32_t>(packet_size - 1))
      .set_packet_loop_count(packet_loop - 1)
      .WriteTo(&mmio_);

  const uint8_t* tx = txdata;
  uint8_t* rx = out_rxdata;
  for (uint32_t loop = 0; loop < packet_loop; loop++) {
    FifoTransferPacket(&tx, &rx, packet_size);
  }

  return (data_size % packet_size) ? FifoExchange(tx, rx, data_size % packet_size) : ZX_OK;
}

zx_status_t MtkSpi::SpiImplExchange(uint32_t cs, const uint8_t* txdata, size_t txdata_size,
                                    uint8_t* out_rxdata, size_t rxdata_size,
                                    size_t* out_rxdata_actual) {
  if (cs >= SpiImplGetChipSelectCount()) {
    return ZX_ERR_INVALID_ARGS;
  }

  if ((txdata_size && rxdata_size && (txdata_size != rxdata_size)) || (!txdata && !out_rxdata) ||
      (static_cast<bool>(txdata) ^ static_cast<bool>(txdata_size)) ||
      (static_cast<bool>(out_rxdata) ^ static_cast<bool>(rxdata_size))) {
    return ZX_ERR_INVALID_ARGS;
  }
  size_t data_size = txdata_size ? txdata_size : rxdata_size;

  memset(out_rxdata, 0, rxdata_size);

  zx_status_t status = ZX_OK;
  // Using FIFO for now, could also support DMA
  if ((status = FifoExchange(txdata, out_rxdata, data_size)) != ZX_OK) {
    zxlogf(ERROR, "%s: FifoExchange failed with %d", __func__, status);
    return status;
  }

  *out_rxdata_actual = rxdata_size;

  return ZX_OK;
}

zx_status_t MtkSpi::SpiImplRegisterVmo(uint32_t cs, uint32_t vmo_id, zx::vmo vmo, uint64_t offset,
                                       uint64_t size, uint32_t rights) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MtkSpi::SpiImplTransmitVmo(uint32_t chip_select, uint32_t vmo_id, uint64_t offset,
                                       uint64_t size) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MtkSpi::SpiImplReceiveVmo(uint32_t chip_select, uint32_t vmo_id, uint64_t offset,
                                      uint64_t size) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MtkSpi::SpiImplUnregisterVmo(uint32_t cs, uint32_t vmo_id, zx::vmo* out_vmo) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MtkSpi::SpiImplExchangeVmo(uint32_t cs, uint32_t tx_vmo_id, uint64_t tx_offset,
                                       uint32_t rx_vmo_id, uint64_t rx_offset, uint64_t size) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MtkSpi::Init() {
  // Reset
  CmdReg::Get().ReadFrom(&mmio_).set_reset(1).WriteTo(&mmio_);
  CmdReg::Get().ReadFrom(&mmio_).set_reset(0).WriteTo(&mmio_);

  CmdReg::Get().ReadFrom(&mmio_).set_rx_msb_first(1).set_tx_msb_first(1).WriteTo(&mmio_);

  // Prepare transfer
  uint32_t div =
      (speed_hz_ < spi_clk_hz_ / 2) ? static_cast<uint32_t>((spi_clk_hz_ + 0.5) / speed_hz_) : 1;
  uint32_t sck_time = (div + 1) / 2;
  uint32_t cs_time = sck_time * 2;
  Cfg0Reg::Get()
      .ReadFrom(&mmio_)
      .set_cs_setup_count((cs_time - 1) & 0xFFFF)
      .set_cs_hold_count((cs_time - 1) & 0xFFFF)
      .WriteTo(&mmio_);
  Cfg2Reg::Get()
      .ReadFrom(&mmio_)
      .set_sck_low_count((sck_time - 1) & 0xFFFF)
      .set_sck_high_count((sck_time - 1) & 0xFFFF)
      .WriteTo(&mmio_);
  Cfg1Reg::Get().ReadFrom(&mmio_).set_cs_idle_count((cs_time - 1) & 0xFF).WriteTo(&mmio_);

  return ZX_OK;
}

zx_status_t MtkSpi::Create(void* ctx, zx_device_t* device) {
  ddk::PDev pdev(device);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s: Could not get pdev protocol", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t status = ZX_OK;
  size_t metadata_size, actual;
  if ((status = device_get_metadata_size(device, DEVICE_METADATA_SPI_CHANNELS, &metadata_size)) !=
      ZX_OK) {
    zxlogf(ERROR, "%s: device_get_metadata_size failed %d", __func__, status);
    return ZX_ERR_INTERNAL;
  }
  auto channel_count = metadata_size / sizeof(spi_channel_t);
  fbl::AllocChecker ac;
  std::unique_ptr<spi_channel_t[]> channels(new (&ac) spi_channel_t[channel_count]);
  if (!ac.check()) {
    zxlogf(ERROR, "%s: out of memory", __func__);
    return ZX_ERR_NO_MEMORY;
  }
  status = device_get_metadata(device, DEVICE_METADATA_SPI_CHANNELS, channels.get(), metadata_size,
                               &actual);
  if (status != ZX_OK || actual != metadata_size) {
    zxlogf(ERROR, "%s: device_get_metadata failed %d", __func__, status);
    return ZX_ERR_INTERNAL;
  }

  for (uint32_t i = 0; i < channel_count; i++) {
    std::optional<ddk::MmioBuffer> mmio;
    if ((status = pdev.MapMmio(i, &mmio)) != ZX_OK) {
      zxlogf(ERROR, "%s: could not map mmio %d", __func__, status);
      return status;
    }

    fbl::AllocChecker ac;
    auto spi = fbl::make_unique_checked<MtkSpi>(&ac, device, std::move(mmio.value()));
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }

    if ((status = spi->Init()) != ZX_OK) {
      zxlogf(ERROR, "%s could not init %d", __func__, status);
      return status;
    }

    char devname[32];
    sprintf(devname, "mtk-spi-%d", channels[i].bus_id);
    status = spi->DdkAdd(devname);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: DdkDeviceAdd failed for %s", __func__, devname);
      return status;
    }
    auto* ptr = spi.release();

    auto cleanup = fit::defer([&ptr]() { ptr->DdkAsyncRemove(); });

    status = ptr->DdkAddMetadata(DEVICE_METADATA_PRIVATE, &channels[i].bus_id,
                                 sizeof channels[i].bus_id);
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
  ops.bind = MtkSpi::Create;
  return ops;
}();

}  // namespace spi

ZIRCON_DRIVER(mtk_spi, spi::driver_ops, "zircon", "0.1");

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-spi.h"

#include <fuchsia/hardware/composite/cpp/banjo.h>
#include <fuchsia/hardware/spiimpl/c/banjo.h>
#include <lib/device-protocol/pdev.h>
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
#include "src/devices/spi/drivers/aml-spi/aml_spi_bind.h"

namespace spi {

static constexpr size_t kBurstMax = 16;

void AmlSpi::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void AmlSpi::DdkRelease() { delete this; }

#define dump_reg(reg) zxlogf(ERROR, "%-21s (+%02x): %08x", #reg, reg, mmio_.Read32(reg))

void AmlSpi::DumpState() {
  // skip registers with side-effects
  // dump_reg(AML_SPI_RXDATA);
  // dump_reg(AML_SPI_TXDATA);
  dump_reg(AML_SPI_CONREG);
  dump_reg(AML_SPI_INTREG);
  dump_reg(AML_SPI_DMAREG);
  dump_reg(AML_SPI_STATREG);
  dump_reg(AML_SPI_PERIODREG);
  dump_reg(AML_SPI_TESTREG);
  dump_reg(AML_SPI_DRADDR);
  dump_reg(AML_SPI_DWADDR);
  dump_reg(AML_SPI_LD_CNTL0);
  dump_reg(AML_SPI_LD_CNTL1);
  dump_reg(AML_SPI_LD_RADDR);
  dump_reg(AML_SPI_LD_WADDR);
  dump_reg(AML_SPI_ENHANCE_CNTL);
  dump_reg(AML_SPI_ENHANCE_CNTL1);
  dump_reg(AML_SPI_ENHANCE_CNTL2);
}

#undef dump_reg

zx_status_t AmlSpi::SpiImplExchange(uint32_t cs, const uint8_t* txdata, size_t txdata_size,
                                    uint8_t* out_rxdata, size_t rxdata_size,
                                    size_t* out_rxdata_actual) {
  if (cs >= SpiImplGetChipSelectCount()) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (txdata_size && rxdata_size && (txdata_size != rxdata_size)) {
    return ZX_ERR_INVALID_ARGS;
  }

  size_t exchange_size = txdata_size ? txdata_size : rxdata_size;

  // transfer settings
  auto conreg = ConReg::Get()
                    .FromValue(0)
                    .set_en(1)
                    .set_mode(ConReg::kModeMaster)
                    .set_bits_per_word(8 - 1)
                    .WriteTo(&mmio_);

  // reset both fifos
  auto testreg = TestReg::Get().FromValue(0).set_fiforst(3).WriteTo(&mmio_);
  do {
    testreg.ReadFrom(&mmio_);
  } while ((testreg.rxcnt() != 0) || (testreg.txcnt() != 0));

  const uint8_t* tx = txdata;
  uint8_t* rx = out_rxdata;

  gpio_[cs].Write(0);

  while (exchange_size) {
    uint32_t burst_size = (uint32_t)std::min(kBurstMax, exchange_size);

    // fill FIFO
    for (uint32_t i = 0; i < burst_size; i++) {
      if (tx) {
        mmio_.Write32(*tx++, AML_SPI_TXDATA);
      } else {
        mmio_.Write32(0xff, AML_SPI_TXDATA);
      }
    }

    // start burst
    auto statreg = StatReg::Get().FromValue(0).set_tc(1).WriteTo(&mmio_);
    conreg.set_burst_length(burst_size - 1).set_xch(1).WriteTo(&mmio_);

    // wait for completion
    do {
      statreg.ReadFrom(&mmio_);
    } while (!statreg.tc());

    // read
    if (rx) {
      for (uint32_t i = 0; i < burst_size; i++) {
        *rx++ = static_cast<uint8_t>(mmio_.Read32(AML_SPI_RXDATA));
      }
    } else {
      for (uint32_t i = 0; i < burst_size; i++) {
        mmio_.Read32(AML_SPI_RXDATA);
      }
    }

    exchange_size -= burst_size;
  }

  gpio_[cs].Write(1);

  if (rx) {
    *out_rxdata_actual = rxdata_size;
  }

  return ZX_OK;
}

zx_status_t AmlSpi::GpioInit(amlspi_cs_map_t* map, ddk::CompositeProtocolClient& composite) {
  for (uint32_t i = 0; i < map->cs_count; i++) {
    uint32_t index = map->cs[i];
    char fragment_name[32] = {};
    snprintf(fragment_name, 32, "gpio-cs-%d", index);
    ddk::GpioProtocolClient gpio(composite, fragment_name);
    if (!gpio.is_valid()) {
      zxlogf(ERROR, "%s: failed to acquire gpio for SS%d", __func__, i);
      return ZX_ERR_NO_RESOURCES;
    }

    gpio_.push_back(gpio);
  }

  return ZX_OK;
}

zx_status_t AmlSpi::Create(void* ctx, zx_device_t* device) {
  ddk::CompositeProtocolClient composite(device);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "%s: could not get composite protocol", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  ddk::PDev pdev(composite);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_PDEV not available", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  pdev_device_info_t info;
  zx_status_t status = pdev.GetDeviceInfo(&info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: pdev_get_device_info failed", __func__);
    return status;
  }

  if (info.mmio_count != info.irq_count) {
    zxlogf(ERROR, "%s: mmio_count %u does not match irq_count %u", __func__, info.mmio_count,
           info.irq_count);
    status = ZX_ERR_INVALID_ARGS;
    return status;
  }

  size_t actual;
  amlspi_cs_map_t gpio_map[info.mmio_count];
  status = device_get_metadata(device, DEVICE_METADATA_AMLSPI_CS_MAPPING, gpio_map, sizeof gpio_map,
                               &actual);
  if ((status != ZX_OK) || (actual != sizeof gpio_map)) {
    zxlogf(ERROR, "%s: failed to read GPIO/chip select map", __func__);
    return status;
  }

  for (uint32_t i = 0; i < info.mmio_count; i++) {
    std::optional<ddk::MmioBuffer> mmio;
    status = pdev.MapMmio(i, &mmio);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: pdev_map_&mmio__buffer #%d failed %d", __func__, i, status);
      return status;
    }

    fbl::AllocChecker ac;
    auto* spi = new (&ac) AmlSpi(device, *std::move(mmio));
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }

    auto cleanup = fbl::MakeAutoCall([&spi]() { spi->DdkRelease(); });

    status = spi->GpioInit(&gpio_map[i], composite);
    if (status != ZX_OK) {
      return status;
    }

    char devname[32];
    sprintf(devname, "aml-spi-%d", i);

    status = spi->DdkAdd(devname);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: DdkDeviceAdd failed for %s", __func__, devname);
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
  ops.bind = AmlSpi::Create;
  return ops;
}();

}  // namespace spi

ZIRCON_DRIVER(aml_spi, spi::driver_ops, "zircon", "0.1");

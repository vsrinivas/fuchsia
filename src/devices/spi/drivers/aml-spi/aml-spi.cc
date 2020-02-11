// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-spi.h"
#include "registers.h"
#include <string.h>
#include <threads.h>
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <ddk/protocol/spiimpl.h>
#include <hw/reg.h>
#include <lib/device-protocol/platform-device.h>
#include <zircon/types.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>

namespace spi {

static constexpr size_t kBurstMax = 16;

enum {
  COMPONENT_PDEV,
  COMPONENT_GPIO0,
};

void AmlSpi::DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }

void AmlSpi::DdkRelease() { delete this; }

#define dump_reg(reg) zxlogf(ERROR, "%-21s (+%02x): %08x\n", #reg, reg, mmio_.Read32(reg))

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

zx_status_t AmlSpi::GpioInit(amlspi_cs_map_t* map, zx_device_t** gpio_components, size_t count) {
  for (uint32_t i = 0; i < map->cs_count; i++) {
    gpio_protocol_t gpio;
    uint32_t index = map->cs[i];
    zx_status_t status = device_get_protocol(gpio_components[index], ZX_PROTOCOL_GPIO, &gpio);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: failed to acquire gpio for SS%d\n", __func__, i);
      return status;
    }

    ddk::GpioProtocolClient gpio_client(&gpio);
    gpio_.push_back(gpio_client);
  }

  return ZX_OK;
}

zx_status_t AmlSpi::Create(void* ctx, zx_device_t* device) {
  composite_protocol_t composite;

  auto status = device_get_protocol(device, ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: could not get composite protocol\n", __func__);
    return status;
  }

  size_t component_count = composite_get_component_count(&composite);

  zx_device_t* components[component_count];
  size_t actual;
  composite_get_components(&composite, components, component_count, &actual);
  if (actual != component_count) {
    zxlogf(ERROR, "%s: could not get components\n", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  pdev_protocol_t pdev;
  status = device_get_protocol(components[COMPONENT_PDEV], ZX_PROTOCOL_PDEV, &pdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_PDEV not available\n", __func__);
    return status;
  }

  pdev_device_info_t info;
  status = pdev_get_device_info(&pdev, &info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: pdev_get_device_info failed\n", __func__);
    return status;
  }

  if (info.mmio_count != info.irq_count) {
    zxlogf(ERROR, "%s: mmio_count %u does not match irq_count %u\n", __func__, info.mmio_count,
           info.irq_count);
    status = ZX_ERR_INVALID_ARGS;
    return status;
  }

  amlspi_cs_map_t gpio_map[info.mmio_count];
  status = device_get_metadata(device, DEVICE_METADATA_AMLSPI_CS_MAPPING, gpio_map, sizeof gpio_map,
                               &actual);
  if ((status != ZX_OK) || (actual != sizeof gpio_map)) {
    zxlogf(ERROR, "%s: failed to read GPIO/chip select map\n", __func__);
    return status;
  }

  for (uint32_t i = 0; i < info.mmio_count; i++) {
    mmio_buffer_t mmio;
    status = pdev_map_mmio_buffer(&pdev, i, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: pdev_map_&mmio__buffer #%d failed %d\n", __func__, i, status);
      return status;
    }

    fbl::AllocChecker ac;
    auto* spi = new (&ac) AmlSpi(device, ddk::MmioBuffer(mmio));
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }

    auto cleanup = fbl::MakeAutoCall([&spi]() { spi->DdkRelease(); });

    status = spi->GpioInit(&gpio_map[i], &components[COMPONENT_GPIO0], component_count - 1);
    if (status != ZX_OK) {
      return status;
    }

    char devname[32];
    sprintf(devname, "aml-spi-%d", i);

    status = spi->DdkAdd(devname);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: DdkDeviceAdd failed for %s\n", __func__, devname);
      return status;
    }

    status = spi->DdkAddMetadata(DEVICE_METADATA_PRIVATE, &i, sizeof i);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: DdkAddMetadata failed for %s\n", __func__, devname);
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

// clang-format off
ZIRCON_DRIVER_BEGIN(aml_spi, spi::driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_SPI),
ZIRCON_DRIVER_END(aml_spi)

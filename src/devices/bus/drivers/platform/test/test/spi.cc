// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <fuchsia/hardware/spiimpl/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>

#include <memory>

#include <lib/ddk/metadata.h>
#include <ddktl/device.h>

#include "src/devices/bus/drivers/platform/test/test-spi-bind.h"

#define DRIVER_NAME "test-spi"

namespace spi {

class TestSpiDevice;
using DeviceType = ddk::Device<TestSpiDevice>;

class TestSpiDevice : public DeviceType,
                      public ddk::SpiImplProtocol<TestSpiDevice, ddk::base_protocol> {
 public:
  static zx_status_t Create(zx_device_t* parent) {
    auto dev = std::make_unique<TestSpiDevice>(parent, 0);
    pdev_protocol_t pdev;
    zx_status_t status;

    zxlogf(INFO, "TestSpiDevice::Create: %s ", DRIVER_NAME);

    status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: could not get ZX_PROTOCOL_PDEV", __func__);
      return status;
    }

    status = dev->DdkAdd("test-spi");
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: DdkAdd failed: %d", __func__, status);
      return status;
    }

    status = dev->DdkAddMetadata(DEVICE_METADATA_PRIVATE, &dev->bus_id_, sizeof dev->bus_id_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: DdkAddMetadata failed: %d", __func__, status);
      return status;
    }

    // devmgr is now in charge of dev.
    __UNUSED auto ptr = dev.release();

    zxlogf(INFO, "%s: returning ZX_OK", __func__);
    return ZX_OK;
  }

  explicit TestSpiDevice(zx_device_t* parent, uint32_t bus_id)
      : DeviceType(parent), bus_id_(bus_id) {}

  uint32_t SpiImplGetChipSelectCount() { return 1; }
  zx_status_t SpiImplExchange(uint32_t cs, const uint8_t* txdata_list, size_t txdata_count,
                              uint8_t* out_rxdata_list, size_t rxdata_count,
                              size_t* out_rxdata_actual) {
    // TX only, ignore
    if (!out_rxdata_list) {
      return ZX_OK;
    }

    // RX only, fill with pattern
    else if (!txdata_list) {
      for (size_t i = 0; i < rxdata_count; i++) {
        out_rxdata_list[i] = i & 0xff;
      }
      *out_rxdata_actual = rxdata_count;
      return ZX_OK;
    }

    // Both TX and RX; copy
    else {
      if (txdata_count != rxdata_count) {
        return ZX_ERR_INVALID_ARGS;
      }
      memcpy(out_rxdata_list, txdata_list, txdata_count);
      *out_rxdata_actual = txdata_count;
      return ZX_OK;
    }
  }

  zx_status_t SpiImplRegisterVmo(uint32_t chip_select, uint32_t vmo_id, zx::vmo vmo,
                                 uint64_t offset, uint64_t size, uint32_t rights) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t SpiImplUnregisterVmo(uint32_t chip_select, uint32_t vmo_id, zx::vmo* out_vmo) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t SpiImplTransmitVmo(uint32_t chip_select, uint32_t vmo_id, uint64_t offset,
                                 uint64_t size) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t SpiImplReceiveVmo(uint32_t chip_select, uint32_t vmo_id, uint64_t offset,
                                uint64_t size) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t SpiImplExchangeVmo(uint32_t chip_select, uint32_t tx_vmo_id, uint64_t tx_offset,
                                 uint32_t rx_vmo_id, uint64_t rx_offset, uint64_t size) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Methods required by the ddk mixins
  void DdkRelease() { delete this; }

 private:
  uint32_t bus_id_;
};

zx_status_t test_spi_bind(void* ctx, zx_device_t* parent) { return TestSpiDevice::Create(parent); }

constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t driver_ops = {};
  driver_ops.version = DRIVER_OPS_VERSION;
  driver_ops.bind = test_spi_bind;
  return driver_ops;
}();

}  // namespace spi

ZIRCON_DRIVER(test_spi, spi::driver_ops, "zircon", "0.1");

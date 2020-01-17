// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-ethernet.h"

#include <lib/device-protocol/i2c.h>
#include <lib/device-protocol/platform-device.h>
#include <stdio.h>
#include <string.h>
#include <zircon/compiler.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <ddk/protocol/ethernet.h>
#include <ddk/protocol/platform/device.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <hw/reg.h>
#include <soc/aml-s912/s912-hw.h>

#include "aml-regs.h"

namespace eth {

#define MCU_I2C_REG_BOOT_EN_WOL 0x21
#define MCU_I2C_REG_BOOT_EN_WOL_RESET_ENABLE 0x03

zx_status_t AmlEthernet::EthBoardResetPhy() {
  if (has_reset_) {
    gpios_[PHY_RESET].Write(0);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
    gpios_[PHY_RESET].Write(1);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
  }
  return ZX_OK;
}

zx_status_t AmlEthernet::InitPdev() {
  composite_protocol_t composite;

  auto status = device_get_protocol(parent(), ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not get composite protocol\n");
    return status;
  }

  zx_device_t* components[COMPONENT_COUNT];
  size_t actual;
  composite_get_components(&composite, components, fbl::count_of(components), &actual);
  if (actual == fbl::count_of(components)) {
    has_reset_ = true;
  } else {
    if (actual == (fbl::count_of(components) - 1)) {
      has_reset_ = false;
    } else {
      zxlogf(ERROR, "could not get components\n");
      return ZX_ERR_NOT_SUPPORTED;
    }
  }

  pdev_protocol_t pdev;
  status = device_get_protocol(components[COMPONENT_PDEV], ZX_PROTOCOL_PDEV, &pdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not get PDEV protocol\n");
    return status;
  }
  pdev_ = &pdev;

  i2c_protocol_t i2c;
  status = device_get_protocol(components[COMPONENT_I2C], ZX_PROTOCOL_I2C, &i2c);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not get I2C protocol\n");
    return status;
  }
  i2c_ = &i2c;

  gpio_protocol_t gpio;
  if (has_reset_) {
    status = device_get_protocol(components[COMPONENT_RESET_GPIO], ZX_PROTOCOL_GPIO, &gpio);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Could not get GPIO protocol\n");
      return status;
    }
    gpios_[PHY_RESET] = &gpio;
  }

  status = device_get_protocol(components[COMPONENT_INTR_GPIO], ZX_PROTOCOL_GPIO, &gpio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not get GPIO protocol\n");
    return status;
  }
  gpios_[PHY_INTR] = &gpio;

  // Map amlogic peripheral control registers.
  status = pdev_.MapMmio(MMIO_PERIPH, &periph_mmio_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-dwmac: could not map periph mmio: %d\n", status);
    return status;
  }

  // Map HHI regs (clocks and power domains).
  status = pdev_.MapMmio(MMIO_HHI, &hhi_mmio_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-dwmac: could not map hiu mmio: %d\n", status);
    return status;
  }

  return status;
}

zx_status_t AmlEthernet::Bind() {
  // Set reset line to output if implemented
  if (has_reset_) {
    gpios_[PHY_RESET].ConfigOut(0);
  }

  // Initialize AMLogic peripheral registers associated with dwmac.
  // Sorry about the magic...rtfm
  periph_mmio_->Write32(0x1621, PER_ETH_REG0);
  periph_mmio_->Write32(0x20000, PER_ETH_REG1);

  periph_mmio_->Write32(REG2_ETH_REG2_REVERSED | REG2_INTERNAL_PHY_ID, PER_ETH_REG2);

  periph_mmio_->Write32(REG3_CLK_IN_EN | REG3_ETH_REG3_19_RESVERD | REG3_CFG_PHY_ADDR |
                            REG3_CFG_MODE | REG3_CFG_EN_HIGH | REG3_ETH_REG3_2_RESERVED,
                        PER_ETH_REG3);

  // Enable clocks and power domain for dwmac
  hhi_mmio_->SetBits32(1 << 3, HHI_GCLK_MPEG1);
  hhi_mmio_->ClearBits32((1 << 3) | (1 << 2), HHI_MEM_PD_REG0);

  // WOL reset enable to MCU
  uint8_t write_buf[2] = {MCU_I2C_REG_BOOT_EN_WOL, MCU_I2C_REG_BOOT_EN_WOL_RESET_ENABLE};
  zx_status_t status = i2c_.WriteSync(write_buf, sizeof(write_buf));
  if (status) {
    zxlogf(ERROR, "aml-ethernet: WOL reset enable to MCU failed: %d\n", status);
    return status;
  }

  // Populate board specific information
  eth_dev_metadata_t mac_info;
  size_t actual;
  status = device_get_metadata(parent(), DEVICE_METADATA_ETH_MAC_DEVICE, &mac_info,
                               sizeof(eth_dev_metadata_t), &actual);
  if (status != ZX_OK || actual != sizeof(eth_dev_metadata_t)) {
    zxlogf(ERROR, "aml-ethernet: Could not get MAC metadata %d\n", status);
    return status;
  }

  zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, mac_info.vid},
      {BIND_PLATFORM_DEV_DID, 0, mac_info.did},
  };

  return DdkAdd("aml-ethernet", 0, props, fbl::count_of(props));
}

void AmlEthernet::DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }

void AmlEthernet::DdkRelease() { delete this; }

zx_status_t AmlEthernet::Create(void* ctx, zx_device_t* parent) {
  zxlogf(INFO, "aml-ethernet: adding driver\n");
  fbl::AllocChecker ac;
  auto eth_device = fbl::make_unique_checked<AmlEthernet>(&ac, parent);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = eth_device->InitPdev();
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-ethernet: failed to init platform device\n");
    return status;
  }

  status = eth_device->Bind();
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-ethernet driver failed to get added: %d\n", status);
    return status;
  } else {
    zxlogf(INFO, "aml-ethernet driver added\n");
  }

  // eth_device intentionally leaked as it is now held by DevMgr
  __UNUSED auto ptr = eth_device.release();

  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = AmlEthernet::Create;
  return ops;
}();

}  // namespace eth

// clang-format off
ZIRCON_DRIVER_BEGIN(aml_eth, eth::driver_ops, "aml-ethernet", "0.1", 5)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_ETH),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S912),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_A311D),
ZIRCON_DRIVER_END(aml_eth)

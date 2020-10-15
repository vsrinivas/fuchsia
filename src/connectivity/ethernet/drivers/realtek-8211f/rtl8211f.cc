// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rtl8211f.h"

#include <stdio.h>
#include <string.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>

#include "mdio-regs.h"
#include "src/connectivity/ethernet/drivers/realtek-8211f/rtl8211f-bind.h"

namespace phy {

zx_status_t PhyDevice::ConfigPhy(const uint8_t mac[MAC_ARRAY_LENGTH]) {
  uint32_t val;

  // WOL reset.
  eth_mac_.MdioWrite(MII_EPAGSR, 0xd40);
  eth_mac_.MdioWrite(22, 0x20);
  eth_mac_.MdioWrite(MII_EPAGSR, 0);

  eth_mac_.MdioWrite(MII_EPAGSR, 0xd8c);
  eth_mac_.MdioWrite(16, (mac[1] << 8) | mac[0]);
  eth_mac_.MdioWrite(17, (mac[3] << 8) | mac[2]);
  eth_mac_.MdioWrite(18, (mac[5] << 8) | mac[4]);
  eth_mac_.MdioWrite(MII_EPAGSR, 0);

  eth_mac_.MdioWrite(MII_EPAGSR, 0xd8a);
  eth_mac_.MdioWrite(17, 0x9fff);
  eth_mac_.MdioWrite(MII_EPAGSR, 0);

  eth_mac_.MdioWrite(MII_EPAGSR, 0xd8a);
  eth_mac_.MdioWrite(16, 0x1000);
  eth_mac_.MdioWrite(MII_EPAGSR, 0);

  eth_mac_.MdioWrite(MII_EPAGSR, 0xd80);
  eth_mac_.MdioWrite(16, 0x3000);
  eth_mac_.MdioWrite(17, 0x0020);
  eth_mac_.MdioWrite(18, 0x03c0);
  eth_mac_.MdioWrite(19, 0x0000);
  eth_mac_.MdioWrite(20, 0x0000);
  eth_mac_.MdioWrite(21, 0x0000);
  eth_mac_.MdioWrite(22, 0x0000);
  eth_mac_.MdioWrite(23, 0x0000);
  eth_mac_.MdioWrite(MII_EPAGSR, 0);

  eth_mac_.MdioWrite(MII_EPAGSR, 0xd8a);
  eth_mac_.MdioWrite(19, 0x1002);
  eth_mac_.MdioWrite(MII_EPAGSR, 0);

  // Fix txdelay issuee for rtl8211.  When a hw reset is performed
  // on the phy, it defaults to having an extra delay in the TXD path.
  // Since we reset the phy, this needs to be corrected.
  eth_mac_.MdioWrite(MII_EPAGSR, 0xd08);
  eth_mac_.MdioRead(0x11, &val);
  val &= ~0x100;
  eth_mac_.MdioWrite(0x11, val);
  eth_mac_.MdioWrite(MII_EPAGSR, 0x00);

  // Enable GigE advertisement.
  eth_mac_.MdioWrite(MII_GBCR, 1 << 9);

  // Restart advertisements.
  eth_mac_.MdioRead(MII_BMCR, &val);
  val |= BMCR_ANENABLE | BMCR_ANRESTART;
  val &= ~BMCR_ISOLATE;
  eth_mac_.MdioWrite(MII_BMCR, val);

  return ZX_OK;
}

void PhyDevice::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void PhyDevice::DdkRelease() { delete this; }

zx_status_t PhyDevice::Create(void* ctx, zx_device_t* device) {
  fbl::AllocChecker ac;
  auto phy_device = fbl::make_unique_checked<PhyDevice>(&ac, device);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  // Get ETH_MAC protocol.
  if (!phy_device->eth_mac_.is_valid()) {
    zxlogf(ERROR, "aml-dwmac: could not obtain ETH_BOARD protocol");
    return ZX_ERR_NO_RESOURCES;
  }

  zx_status_t status = phy_device->DdkAdd("phy_null_device", DEVICE_ADD_NON_BINDABLE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "dwmac: Could not create phy device: %d", status);
    return status;
  }

  // devmgr now owns device.
  auto* dev = phy_device.release();

  eth_mac_callbacks_t cb;
  cb.config_phy = [](void* ctx, const uint8_t* mac) {
    return static_cast<PhyDevice*>(ctx)->ConfigPhy(mac);
  };
  cb.ctx = dev;

  dev->eth_mac_.RegisterCallbacks(&cb);
  return status;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = PhyDevice::Create;
  return ops;
}();

}  // namespace phy

ZIRCON_DRIVER(rtl8211f, phy::driver_ops, "rtl8211-phy", "0.1")

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rtl8211f.h"
#include "mdio-regs.h"
#include <ddk/debug.h>
#include <ddk/protocol/platform-defs.h>
#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>
#include <stdio.h>
#include <string.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

namespace phy {

void PhyDevice::ConfigPhy() {
    uint32_t val;

    // WOL reset.
    mdio_write(&eth_mac_, MII_EPAGSR, 0xd40);
    mdio_write(&eth_mac_, 22, 0x20);
    mdio_write(&eth_mac_, MII_EPAGSR, 0);

    mdio_write(&eth_mac_, MII_EPAGSR, 0xd8c);
    // mdio_write(&eth_mac_, 16, (mac_[1] << 8) | mac_[0]);
    // mdio_write(&eth_mac_, 17, (mac_[3] << 8) | mac_[2]);
    // mdio_write(&eth_mac_, 18, (mac_[5] << 8) | mac_[4]);
    mdio_write(&eth_mac_, MII_EPAGSR, 0);

    mdio_write(&eth_mac_, MII_EPAGSR, 0xd8a);
    mdio_write(&eth_mac_, 17, 0x9fff);
    mdio_write(&eth_mac_, MII_EPAGSR, 0);

    mdio_write(&eth_mac_, MII_EPAGSR, 0xd8a);
    mdio_write(&eth_mac_, 16, 0x1000);
    mdio_write(&eth_mac_, MII_EPAGSR, 0);

    mdio_write(&eth_mac_, MII_EPAGSR, 0xd80);
    mdio_write(&eth_mac_, 16, 0x3000);
    mdio_write(&eth_mac_, 17, 0x0020);
    mdio_write(&eth_mac_, 18, 0x03c0);
    mdio_write(&eth_mac_, 19, 0x0000);
    mdio_write(&eth_mac_, 20, 0x0000);
    mdio_write(&eth_mac_, 21, 0x0000);
    mdio_write(&eth_mac_, 22, 0x0000);
    mdio_write(&eth_mac_, 23, 0x0000);
    mdio_write(&eth_mac_, MII_EPAGSR, 0);

    mdio_write(&eth_mac_, MII_EPAGSR, 0xd8a);
    mdio_write(&eth_mac_, 19, 0x1002);
    mdio_write(&eth_mac_, MII_EPAGSR, 0);

    // Fix txdelay issuee for rtl8211.  When a hw reset is performed
    // on the phy, it defaults to having an extra delay in the TXD path.
    // Since we reset the phy, this needs to be corrected.
    mdio_write(&eth_mac_, MII_EPAGSR, 0xd08);
    mdio_read(&eth_mac_, 0x11, &val);
    val &= ~0x100;
    mdio_write(&eth_mac_, 0x11, val);
    mdio_write(&eth_mac_, MII_EPAGSR, 0x00);

    // Enable GigE advertisement.
    mdio_write(&eth_mac_, MII_GBCR, 1 << 9);

    // Restart advertisements.
    mdio_read(&eth_mac_, MII_BMCR, &val);
    val |= BMCR_ANENABLE | BMCR_ANRESTART;
    val &= ~BMCR_ISOLATE;
    mdio_write(&eth_mac_, MII_BMCR, val);
}

static void DdkUnbind(void* ctx) {
    auto& self = *static_cast<PhyDevice*>(ctx);
    device_remove(self.device_);
}

static void DdkRelease(void* ctx) {
    delete static_cast<PhyDevice*>(ctx);
}

static zx_protocol_device_t device_ops = []() {
    zx_protocol_device_t result;

    result.version = DEVICE_OPS_VERSION;
    result.unbind = &DdkUnbind;
    result.release = &DdkRelease;
    return result;
}();

static device_add_args_t phy_device_args = []() {
    device_add_args_t result;
    result.version = DEVICE_ADD_ARGS_VERSION;
    result.flags = DEVICE_ADD_NON_BINDABLE;
    result.ops = &device_ops;
    return result;
}();

zx_status_t PhyDevice::Create(zx_device_t* device) {

    fbl::AllocChecker ac;
    auto phy_device = fbl::make_unique_checked<PhyDevice>(&ac);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    // Get ETH_MAC protocol.
    zx_status_t status = device_get_protocol(device,
                                             ZX_PROTOCOL_ETH_MAC,
                                             &phy_device->eth_mac_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-dwmac: could not obtain ETH_BOARD protocol: %d\n", status);
        return status;
    }

    status = device_add(device, &phy_device_args, &phy_device->device_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "dwmac: Could not create phy device: %d\n", status);

        return status;
    }
    return status;
}

} // namespace phy

extern "C" zx_status_t rtl8211f_bind(void* ctx, zx_device_t* device) {
    return phy::PhyDevice::Create(device);
}

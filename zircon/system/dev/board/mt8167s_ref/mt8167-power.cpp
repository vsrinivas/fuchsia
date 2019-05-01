// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddktl/protocol/powerimpl.h>
#include <soc/mt8167/mt8167-hw.h>
#include <ddk/metadata.h>
#include <ddk/metadata/power.h>

#include "mt8167.h"

namespace board_mt8167 {

zx_status_t Mt8167::Vgp1Enable() {
    ddk::PowerImplProtocolClient power(parent());
    if (!power.is_valid()) {
        zxlogf(ERROR, "Failed to get power impl protocol\n");
        return ZX_ERR_NO_RESOURCES;
    }

    zx_status_t status = power.EnablePowerDomain(kVDLdoVGp1);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to enable VGP1 regulator: %d\n", __FUNCTION__, status);
    }

    return status;
}

zx_status_t Mt8167::PowerInit() {
    static const pbus_mmio_t power_mmios[] = {
        {
            .base = MT8167_PMIC_WRAP_BASE,
            .length = MT8167_PMIC_WRAP_SIZE,
        }
    };
    static const power_domain_t power_domains[] = {
        { kVDLdoVGp2 }, // Display Panel
    };
    static const pbus_metadata_t power_metadata[] = {
        {
            .type = DEVICE_METADATA_POWER_DOMAINS,
            .data_buffer = &power_domains,
            .data_size = sizeof(power_domains),
        },
    };

    pbus_dev_t power_dev = {};
    power_dev.name = "power";
    power_dev.vid = PDEV_VID_MEDIATEK;
    power_dev.did = PDEV_DID_MEDIATEK_POWER;
    power_dev.mmio_list = power_mmios;
    power_dev.mmio_count = countof(power_mmios);
    power_dev.metadata_list = power_metadata;
    power_dev.metadata_count = countof(power_metadata);

    zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_POWER_IMPL, &power_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Adding power device failed %d\n", __FUNCTION__, status);
        return status;
    }

    // Vgp1Enable() must be called before ThermalInit() as it uses the PMIC wrapper to enable the
    // VGP1 regulator.
    return Vgp1Enable();
}

}  // namespace board_mt8167

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/metadata/power.h>
#include <ddk/platform-defs.h>
#include <ddktl/protocol/powerimpl.h>
#include <soc/mt8167/mt8167-hw.h>

#include "mt8167.h"
namespace {
static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t power_impl_driver_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_POWER_IMPL),
};
static const device_fragment_part_t power_impl_fragment[] = {
    {countof(root_match), root_match},
    {countof(power_impl_driver_match), power_impl_driver_match},
};
zx_device_prop_t props[] = {
    {BIND_POWER_DOMAIN_COMPOSITE, 0, PDEV_DID_POWER_DOMAIN_COMPOSITE},
};

// kVDLdoVGp2
static const device_fragment_t power_domain_kVDLdoVGp2_fragments[] = {
    {countof(power_impl_fragment), power_impl_fragment},
};
static const power_domain_t power_domain_kVDLdoVGp2[] = {
    {kVDLdoVGp2},
};
static const device_metadata_t power_metadata_kVDLdoVGp2[] = {{
    .type = DEVICE_METADATA_POWER_DOMAINS,
    .data = &power_domain_kVDLdoVGp2,
    .length = sizeof(power_domain_kVDLdoVGp2),
}};
const composite_device_desc_t power_domain_kVDLdoVGp2_desc = {
    .props = props,
    .props_count = countof(props),
    .fragments = power_domain_kVDLdoVGp2_fragments,
    .fragments_count = countof(power_domain_kVDLdoVGp2_fragments),
    .coresident_device_index = 0,
    .metadata_list = power_metadata_kVDLdoVGp2,
    .metadata_count = countof(power_metadata_kVDLdoVGp2),
};

}  // namespace
namespace board_mt8167 {

zx_status_t Mt8167::Vgp1Enable() {
  ddk::PowerImplProtocolClient power(parent());
  if (!power.is_valid()) {
    zxlogf(ERROR, "Failed to get power impl protocol");
    return ZX_ERR_NO_RESOURCES;
  }

  zx_status_t status = power.EnablePowerDomain(kVDLdoVGp1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to enable VGP1 regulator: %d", __FUNCTION__, status);
  }

  return status;
}

zx_status_t Mt8167::PowerInit() {
  static const pbus_mmio_t power_mmios[] = {{
      .base = MT8167_PMIC_WRAP_BASE,
      .length = MT8167_PMIC_WRAP_SIZE,
  }};

  pbus_dev_t power_dev = {};
  power_dev.name = "power";
  power_dev.vid = PDEV_VID_MEDIATEK;
  power_dev.did = PDEV_DID_MEDIATEK_POWER;
  power_dev.mmio_list = power_mmios;
  power_dev.mmio_count = countof(power_mmios);

  zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_POWER_IMPL, &power_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Adding power-impl device failed %d", __FUNCTION__, status);
    return status;
  }

  status = DdkAddComposite("composite-pd-kVDLdoVGp2", &power_domain_kVDLdoVGp2_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAddComposite for power domain kVDLdoVGp2 failed: %d", __FUNCTION__,
           status);
    return status;
  }

  // Vgp1Enable() must be called before ThermalInit() as it uses the PMIC wrapper to enable the
  // VGP1 regulator.
  return Vgp1Enable();
}

}  // namespace board_mt8167

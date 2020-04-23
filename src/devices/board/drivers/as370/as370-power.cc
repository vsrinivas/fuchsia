// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/power.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/as370/as370-power.h>

#include "as370.h"

namespace board_as370 {

namespace {

static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};

static const zx_bind_inst_t power_impl_driver_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_POWER_IMPL),
};

constexpr device_fragment_part_t power_impl_fragment[] = {
    {countof(root_match), root_match},
    {countof(power_impl_driver_match), power_impl_driver_match},
};

zx_device_prop_t power_domain_kBuckSoC_props[] = {
    {BIND_POWER_DOMAIN_COMPOSITE, 0, PDEV_DID_POWER_DOMAIN_COMPOSITE},
};

constexpr device_fragment_t power_domain_kBuckSoC_fragments[] = {
    {countof(power_impl_fragment), power_impl_fragment},
};

static const power_domain_t power_domain_kBuckSoC[] = {
    {kBuckSoC},
};

static const device_metadata_t power_domain_kBuckSoC_metadata[] = {
    {
        .type = DEVICE_METADATA_POWER_DOMAINS,
        .data = &power_domain_kBuckSoC,
        .length = sizeof(power_domain_kBuckSoC),
    },
};

const composite_device_desc_t power_domain_kBuckSoC_desc = {
    .props = power_domain_kBuckSoC_props,
    .props_count = countof(power_domain_kBuckSoC_props),
    .fragments = power_domain_kBuckSoC_fragments,
    .fragments_count = countof(power_domain_kBuckSoC_fragments),
    .coresident_device_index = 0,
    .metadata_list = power_domain_kBuckSoC_metadata,
    .metadata_count = countof(power_domain_kBuckSoC_metadata),
};

static const zx_bind_inst_t i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 0x0),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x66),
};

static const device_fragment_part_t i2c_fragment[] = {
    {countof(root_match), root_match},
    {countof(i2c_match), i2c_match},
};

static const device_fragment_t fragments[] = {
    {countof(i2c_fragment), i2c_fragment},
};

constexpr zx_device_prop_t props[] = {
    {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_SYNAPTICS},
    {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_AS370_POWER},
};

const composite_device_desc_t comp_desc = {
    .props = props,
    .props_count = countof(props),
    .fragments = fragments,
    .fragments_count = countof(fragments),
    .coresident_device_index = UINT32_MAX,
    .metadata_list = nullptr,
    .metadata_count = 0,
};

}  // namespace

zx_status_t As370::PowerInit() {
  zx_status_t status;

  status = DdkAddComposite("power", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd for powerimpl failed %d", __FUNCTION__, status);
    return status;
  }

  status = DdkAddComposite("composite-pd-kBuckSoC", &power_domain_kBuckSoC_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd for power domain kBuckSoC failed %d", __FUNCTION__,
           status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_as370

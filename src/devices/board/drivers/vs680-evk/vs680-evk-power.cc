// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/metadata/power.h>
#include <ddk/platform-defs.h>
#include <fbl/algorithm.h>
#include <soc/vs680/vs680-power.h>

#include "vs680-evk.h"

namespace board_vs680_evk {

zx_status_t Vs680Evk::PowerInit() {
  constexpr zx_bind_inst_t root_match[] = {
      BI_MATCH(),
  };

  constexpr zx_bind_inst_t pmic_i2c_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
      BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 1),
      BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x62),
  };

  const device_fragment_part_t pmic_i2c_fragment[] = {
      {fbl::count_of(root_match), root_match},
      {fbl::count_of(pmic_i2c_match), pmic_i2c_match},
  };

  const device_fragment_t power_impl_fragments[] = {
      {fbl::count_of(pmic_i2c_fragment), pmic_i2c_fragment},
  };

  constexpr zx_device_prop_t power_impl_props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_SYNAPTICS},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_VS680_POWER},
  };

  const composite_device_desc_t power_impl_desc = {
      .props = power_impl_props,
      .props_count = fbl::count_of(power_impl_props),
      .fragments = power_impl_fragments,
      .fragments_count = fbl::count_of(power_impl_fragments),
      .coresident_device_index = UINT32_MAX,
  };

  constexpr zx_device_prop_t power_domain_vcpu_props[] = {
      {BIND_POWER_DOMAIN_COMPOSITE, 0, PDEV_DID_POWER_DOMAIN_COMPOSITE},
  };

  constexpr zx_bind_inst_t power_impl_driver_match[] = {
      BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_POWER_IMPL),
  };

  const device_fragment_part_t power_impl_fragment[] = {
      {fbl::count_of(root_match), root_match},
      {fbl::count_of(power_impl_driver_match), power_impl_driver_match},
  };

  const device_fragment_t power_domain_vcpu_fragments[] = {
      {fbl::count_of(power_impl_fragment), power_impl_fragment},
  };

  constexpr power_domain_t power_domain_vcpu[] = {
      {vs680::kPowerDomainVCpu},
  };

  const device_metadata_t power_domain_vcpu_metadata[] = {
      {
          .type = DEVICE_METADATA_POWER_DOMAINS,
          .data = &power_domain_vcpu,
          .length = sizeof(power_domain_vcpu),
      },
  };

  const composite_device_desc_t power_domain_vcpu_desc = {
      .props = power_domain_vcpu_props,
      .props_count = fbl::count_of(power_domain_vcpu_props),
      .fragments = power_domain_vcpu_fragments,
      .fragments_count = fbl::count_of(power_domain_vcpu_fragments),
      .coresident_device_index = 0,
      .metadata_list = power_domain_vcpu_metadata,
      .metadata_count = fbl::count_of(power_domain_vcpu_metadata),
  };

  zx_status_t status = DdkAddComposite("power", &power_impl_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to add power composite device: %d", __func__, status);
    return status;
  }

  if ((status = DdkAddComposite("composite-pd-vcpu", &power_domain_vcpu_desc)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to add VCPU composite device: %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_vs680_evk

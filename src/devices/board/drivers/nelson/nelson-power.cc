// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include "nelson-gpios.h"
#include "nelson.h"
#include "src/devices/board/drivers/nelson/brownout_protection_bind.h"
#include "src/devices/board/drivers/nelson/ti_ina231_mlb_bind.h"
#include "src/devices/board/drivers/nelson/ti_ina231_speakers_bind.h"
#include "src/devices/power/drivers/ti-ina231/ti-ina231-metadata.h"

namespace nelson {

// These values are specific to Nelson, and are only used within this board driver.
enum : uint32_t {
  kPowerSensorDomainMlb = 0,
  kPowerSensorDomainAudio = 1,
};

constexpr power_sensor::Ina231Metadata kMlbSensorMetadata = {
    .mode = power_sensor::Ina231Metadata::kModeShuntAndBusContinuous,
    .shunt_voltage_conversion_time = power_sensor::Ina231Metadata::kConversionTime332us,
    .bus_voltage_conversion_time = power_sensor::Ina231Metadata::kConversionTime332us,
    .averages = power_sensor::Ina231Metadata::kAverages1024,
    .shunt_resistance_microohm = 10'000,
    .bus_voltage_limit_microvolt = 0,
    .alert = power_sensor::Ina231Metadata::kAlertNone,
    .power_sensor_domain = kPowerSensorDomainMlb,
};

constexpr power_sensor::Ina231Metadata kAudioSensorMetadata = {
    .mode = power_sensor::Ina231Metadata::kModeShuntAndBusContinuous,
    .shunt_voltage_conversion_time = power_sensor::Ina231Metadata::kConversionTime332us,
    .bus_voltage_conversion_time = power_sensor::Ina231Metadata::kConversionTime332us,
    .averages = power_sensor::Ina231Metadata::kAverages1024,
    .shunt_resistance_microohm = 10'000,
    .bus_voltage_limit_microvolt = 11'000'000,
    .alert = power_sensor::Ina231Metadata::kAlertBusUnderVoltage,
    .power_sensor_domain = kPowerSensorDomainAudio,
};

constexpr device_metadata_t kMlbMetadata[] = {
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data = &kMlbSensorMetadata,
        .length = sizeof(kMlbSensorMetadata),
    },
};

constexpr device_metadata_t kAudioMetadata[] = {
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data = &kAudioSensorMetadata,
        .length = sizeof(kAudioSensorMetadata),
    },
};

constexpr zx_device_prop_t props[] = {
    {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_TI},
    {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_TI_INA231},
};

constexpr composite_device_desc_t mlb_power_sensor_dev = {
    .props = props,
    .props_count = countof(props),
    .fragments = ti_ina231_mlb_fragments,
    .fragments_count = countof(ti_ina231_mlb_fragments),
    .primary_fragment = "i2c",
    .spawn_colocated = true,
    .metadata_list = kMlbMetadata,
    .metadata_count = countof(kMlbMetadata),
};

constexpr composite_device_desc_t speakers_power_sensor_dev = {
    .props = props,
    .props_count = countof(props),
    .fragments = ti_ina231_speakers_fragments,
    .fragments_count = countof(ti_ina231_speakers_fragments),
    .primary_fragment = "i2c",
    .spawn_colocated = true,
    .metadata_list = kAudioMetadata,
    .metadata_count = countof(kAudioMetadata),
};

constexpr zx_device_prop_t brownout_protection_props[] = {
    {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GOOGLE},
    {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_NELSON},
    {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_GOOGLE_BROWNOUT},
};

constexpr composite_device_desc_t brownout_protection_dev = {
    .props = brownout_protection_props,
    .props_count = countof(brownout_protection_props),
    .fragments = brownout_protection_fragments,
    .fragments_count = countof(brownout_protection_fragments),
    .primary_fragment = "codec",  // ???
    .spawn_colocated = false,
};

zx_status_t Nelson::PowerInit() {
  zx_status_t status = DdkAddComposite("ti-ina231-mlb", &mlb_power_sensor_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s DdkAddComposite failed %d", __FUNCTION__, status);
    return status;
  }

  if ((status = DdkAddComposite("ti-ina231-speakers", &speakers_power_sensor_dev)) != ZX_OK) {
    zxlogf(ERROR, "%s DdkAddComposite failed %d", __FUNCTION__, status);
    return status;
  }

  if ((status = DdkAddComposite("brownout-protection", &brownout_protection_dev)) != ZX_OK) {
    zxlogf(ERROR, "%s DdkAddComposite failed %d", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace nelson

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/thermal/ntc.h>
#include <limits.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"

namespace sherlock {

static const pbus_mmio_t saradc_mmios[] = {
    {
        .base = T931_SARADC_BASE,
        .length = T931_SARADC_LENGTH,
    },
    {
        .base = T931_AOBUS_BASE,
        .length = T931_AOBUS_LENGTH,
    },
};

static const pbus_irq_t saradc_irqs[] = {
    {
        .irq = T931_SARADC_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

zx_status_t Sherlock::ThermistorInit() {
  if (pid_ != PDEV_PID_LUIS) {
    return ZX_OK;
  }

  thermal::NtcInfo ntc_info[] = {
      {.part = "ncpXXwf104",
       .profile =
           {
               {.temperature_c = -40, .resistance_ohm = 4397119},
               {.temperature_c = -35, .resistance_ohm = 3088599},
               {.temperature_c = -30, .resistance_ohm = 2197225},
               {.temperature_c = -25, .resistance_ohm = 1581881},
               {.temperature_c = -20, .resistance_ohm = 1151037},
               {.temperature_c = -15, .resistance_ohm = 846579},
               {.temperature_c = -10, .resistance_ohm = 628988},
               {.temperature_c = -5, .resistance_ohm = 471632},
               {.temperature_c = 0, .resistance_ohm = 357012},
               {.temperature_c = 5, .resistance_ohm = 272500},
               {.temperature_c = 10, .resistance_ohm = 209710},
               {.temperature_c = 15, .resistance_ohm = 162651},
               {.temperature_c = 20, .resistance_ohm = 127080},
               {.temperature_c = 25, .resistance_ohm = 100000},
               {.temperature_c = 30, .resistance_ohm = 79222},
               {.temperature_c = 35, .resistance_ohm = 63167},
               {.temperature_c = 40, .resistance_ohm = 50677},
               {.temperature_c = 45, .resistance_ohm = 40904},
               {.temperature_c = 50, .resistance_ohm = 33195},
               {.temperature_c = 55, .resistance_ohm = 27091},
               {.temperature_c = 60, .resistance_ohm = 22224},
               {.temperature_c = 65, .resistance_ohm = 18323},
               {.temperature_c = 70, .resistance_ohm = 15184},
               {.temperature_c = 75, .resistance_ohm = 12635},
               {.temperature_c = 80, .resistance_ohm = 10566},
               {.temperature_c = 85, .resistance_ohm = 8873},
               {.temperature_c = 90, .resistance_ohm = 7481},
               {.temperature_c = 95, .resistance_ohm = 6337},
               {.temperature_c = 100, .resistance_ohm = 5384},
               {.temperature_c = 105, .resistance_ohm = 4594},
               {.temperature_c = 110, .resistance_ohm = 3934},
               {.temperature_c = 115, .resistance_ohm = 3380},
               {.temperature_c = 120, .resistance_ohm = 2916},
               {.temperature_c = 125, .resistance_ohm = 2522},
           }},
  };

  thermal::NtcChannel ntc_channels[] = {
      {.adc_channel = 1, .pullup_ohms = 47000, .profile_idx = 0, .name = "therm-mic"},
      {.adc_channel = 2, .pullup_ohms = 47000, .profile_idx = 0, .name = "therm-amp"},
      {.adc_channel = 3, .pullup_ohms = 47000, .profile_idx = 0, .name = "therm-ambient"},
  };

  pbus_metadata_t therm_metadata[] = {
      {
          .type = NTC_CHANNELS_METADATA_PRIVATE,
          .data_buffer = reinterpret_cast<uint8_t*>(&ntc_channels),
          .data_size = sizeof(ntc_channels),
      },
      {
          .type = NTC_PROFILE_METADATA_PRIVATE,
          .data_buffer = reinterpret_cast<uint8_t*>(&ntc_info),
          .data_size = sizeof(ntc_info),
      },
  };

  pbus_dev_t thermistor = {};
  thermistor.name = "thermistor";
  thermistor.vid = PDEV_VID_GOOGLE;
  thermistor.pid = PDEV_PID_LUIS;
  thermistor.did = PDEV_DID_ASTRO_THERMISTOR;
  thermistor.mmio_list = saradc_mmios;
  thermistor.mmio_count = countof(saradc_mmios);
  thermistor.irq_list = saradc_irqs;
  thermistor.irq_count = countof(saradc_irqs);
  thermistor.metadata_list = therm_metadata;
  thermistor.metadata_count = countof(therm_metadata);

  zx_status_t status = pbus_.DeviceAdd(&thermistor);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed: %d", __func__, status);
    return status;
  }

  return ZX_OK;
}
}  // namespace sherlock

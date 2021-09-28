// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/gpioimpl/cpp/banjo.h>
#include <fuchsia/hardware/thermal/c/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/camera.h>
#include <soc/aml-common/aml-thermal.h>
#include <soc/aml-meson/g12b-clk.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>
#include <soc/aml-t931/t931-pwm.h>

#include "sherlock.h"
#include "src/devices/board/drivers/sherlock/sherlock-thermal-bind.h"

namespace sherlock {

namespace {

constexpr uint32_t kPwmDFn = 3;

const pbus_mmio_t thermal_mmios_pll[] = {
    {
        .base = T931_TEMP_SENSOR_PLL_BASE,
        .length = T931_TEMP_SENSOR_PLL_LENGTH,
    },
    {
        .base = T931_TEMP_SENSOR_PLL_TRIM,
        .length = T931_TEMP_SENSOR_TRIM_LENGTH,
    },
    {
        .base = T931_HIU_BASE,
        .length = T931_HIU_LENGTH,
    },
};

const pbus_mmio_t thermal_mmios_ddr[] = {
    {
        .base = T931_TEMP_SENSOR_DDR_BASE,
        .length = T931_TEMP_SENSOR_DDR_LENGTH,
    },
    {
        .base = T931_TEMP_SENSOR_DDR_TRIM,
        .length = T931_TEMP_SENSOR_TRIM_LENGTH,
    },
    {
        .base = T931_HIU_BASE,
        .length = T931_HIU_LENGTH,
    },
};

const pbus_irq_t thermal_irqs_pll[] = {
    {
        .irq = T931_TS_PLL_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

const pbus_irq_t thermal_irqs_ddr[] = {
    {
        .irq = T931_TS_DDR_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

constexpr fuchsia_hardware_thermal_ThermalTemperatureInfo TripPoint(float temp_c,
                                                                    uint16_t cpu_opp_big,
                                                                    uint16_t cpu_opp_little,
                                                                    uint16_t gpu_opp) {
  constexpr float kHysteresis = 2.0f;

  return {
      .up_temp_celsius = temp_c + kHysteresis,
      .down_temp_celsius = temp_c - kHysteresis,
      .fan_level = 0,
      .big_cluster_dvfs_opp = cpu_opp_big,
      .little_cluster_dvfs_opp = cpu_opp_little,
      .gpu_clk_freq_source = gpu_opp,
  };
}

/*
 * PASSIVE COOLING - For Sherlock, we have DVFS support added
 *
 * Below is the operating point information for Small cluster
 * Operating point 0  - Freq 0.1000 Ghz Voltage 0.7310 V
 * Operating point 1  - Freq 0.2500 Ghz Voltage 0.7310 V
 * Operating point 2  - Freq 0.5000 Ghz Voltage 0.7310 V
 * Operating point 3  - Freq 0.6670 Ghz Voltage 0.7310 V
 * Operating point 4  - Freq 1.0000 Ghz Voltage 0.7310 V
 * Operating point 5  - Freq 1.2000 Ghz Voltage 0.7310 V
 * Operating point 6  - Freq 1.3980 Ghz Voltage 0.7610 V
 * Operating point 7  - Freq 1.5120 Ghz Voltage 0.7910 V
 * Operating point 8  - Freq 1.6080 Ghz Voltage 0.8310 V
 * Operating point 9  - Freq 1.7040 Ghz Voltage 0.8610 V
 * Operating point 10 - Freq 1.8960 Ghz Voltage 0.9810 V
 *
 * Below is the operating point information for Big cluster
 * Operating point 0  - Freq 0.1000 Ghz Voltage 0.7510 V
 * Operating point 1  - Freq 0.2500 Ghz Voltage 0.7510 V
 * Operating point 2  - Freq 0.5000 Ghz Voltage 0.7510 V
 * Operating point 3  - Freq 0.6670 Ghz Voltage 0.7510 V
 * Operating point 4  - Freq 1.0000 Ghz Voltage 0.7710 V
 * Operating point 5  - Freq 1.2000 Ghz Voltage 0.7710 V
 * Operating point 6  - Freq 1.3980 Ghz Voltage 0.7910 V
 * Operating point 7  - Freq 1.5120 Ghz Voltage 0.8210 V
 * Operating point 8  - Freq 1.6080 Ghz Voltage 0.8610 V
 * Operating point 9  - Freq 1.7040 Ghz Voltage 0.8910 V
 *
 * GPU_CLK_FREQUENCY_SOURCE -
 * 0 - 285.7 MHz
 * 1 - 400 MHz
 * 2 - 500 MHz
 * 3 - 666 MHz
 * 4 - 800 MHz
 */

// NOTE: This is a very trivial policy, no data backing it up
// As we do more testing this policy can evolve.
fuchsia_hardware_thermal_ThermalDeviceInfo thermal_config_pll = {
    .active_cooling = false,
    .passive_cooling = true,
    .gpu_throttling = true,
    .num_trip_points = 4,
    .big_little = true,
    .critical_temp_celsius = 101.0f,
    .trip_point_info =
        {
            TripPoint(82.5f, 9, 10, 4), TripPoint(85.0f, 8, 9, 4), TripPoint(87.5f, 6, 6, 4),
            TripPoint(90.0f, 4, 4, 4),
            TripPoint(-273.15f, 0, 0, 0),  // 0 Kelvin is impossible, marks end of TripPoints
        },
    .opps =
        {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wc99-designator"
            [fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN] =
#pragma GCC diagnostic pop
                {
                    .opp =
                        {
                            {.freq_hz = 100'000'000, .volt_uv = 751'000},
                            {.freq_hz = 250'000'000, .volt_uv = 751'000},
                            {.freq_hz = 500'000'000, .volt_uv = 751'000},
                            {.freq_hz = 667'000'000, .volt_uv = 751'000},
                            {.freq_hz = 1'000'000'000, .volt_uv = 771'000},
                            {.freq_hz = 1'200'000'000, .volt_uv = 771'000},
                            {.freq_hz = 1'398'000'000, .volt_uv = 791'000},
                            {.freq_hz = 1'512'000'000, .volt_uv = 821'000},
                            {.freq_hz = 1'608'000'000, .volt_uv = 861'000},
                            {.freq_hz = 1'704'000'000, .volt_uv = 891'000},
                            {.freq_hz = 1'704'000'000, .volt_uv = 891'000},
                        },
                    .latency = 0,
                    .count = 10,
                },
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wc99-designator"
            [fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN] =
#pragma GCC diagnostic pop
                {
                    .opp =
                        {
                            {.freq_hz = 100'000'000, .volt_uv = 731'000},
                            {.freq_hz = 250'000'000, .volt_uv = 731'000},
                            {.freq_hz = 500'000'000, .volt_uv = 731'000},
                            {.freq_hz = 667'000'000, .volt_uv = 731'000},
                            {.freq_hz = 1'000'000'000, .volt_uv = 731'000},
                            {.freq_hz = 1'200'000'000, .volt_uv = 731'000},
                            {.freq_hz = 1'398'000'000, .volt_uv = 761'000},
                            {.freq_hz = 1'512'000'000, .volt_uv = 791'000},
                            {.freq_hz = 1'608'000'000, .volt_uv = 831'000},
                            {.freq_hz = 1'704'000'000, .volt_uv = 861'000},
                            {.freq_hz = 1'896'000'000, .volt_uv = 1'011'000},
                        },
                    .latency = 0,
                    .count = 11,
                },
        },
};

fuchsia_hardware_thermal_ThermalDeviceInfo thermal_config_ddr = {
    .active_cooling = false,
    .passive_cooling = false,
    .gpu_throttling = false,
    .num_trip_points = 0,
    .big_little = false,
    .critical_temp_celsius = 110.0,
    .trip_point_info = {TripPoint(-273.15f, 0, 0, 0)},  // Unused
    .opps = {}};

// clang-format on
aml_thermal_info_t aml_thermal_info = {
    .voltage_table =
        {
            {1'022'000, 0}, {1'011'000, 3}, {1'001'000, 6}, {991'000, 10}, {981'000, 13},
            {971'000, 16},  {961'000, 20},  {951'000, 23},  {941'000, 26}, {931'000, 30},
            {921'000, 33},  {911'000, 36},  {901'000, 40},  {891'000, 43}, {881'000, 46},
            {871'000, 50},  {861'000, 53},  {851'000, 56},  {841'000, 60}, {831'000, 63},
            {821'000, 67},  {811'000, 70},  {801'000, 73},  {791'000, 76}, {781'000, 80},
            {771'000, 83},  {761'000, 86},  {751'000, 90},  {741'000, 93}, {731'000, 96},
            {721'000, 100},
        },
    .initial_cluster_frequencies =
        {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wc99-designator"
            [fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN] = 1'000'000'000,
            [fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN] = 1'200'000'000,
#pragma GCC diagnostic pop
        },
    .voltage_pwm_period_ns = 1250,
    .opps = {},
    .cluster_id_map = {},
};

const pbus_metadata_t thermal_metadata_pll[] = {
    {
        .type = DEVICE_METADATA_THERMAL_CONFIG,
        .data_buffer = reinterpret_cast<const uint8_t*>(&thermal_config_pll),
        .data_size = sizeof(thermal_config_pll),
    },
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = reinterpret_cast<const uint8_t*>(&aml_thermal_info),
        .data_size = sizeof(aml_thermal_info),
    },
};

const pbus_metadata_t thermal_metadata_ddr[] = {
    {
        .type = DEVICE_METADATA_THERMAL_CONFIG,
        .data_buffer = reinterpret_cast<const uint8_t*>(&thermal_config_ddr),
        .data_size = sizeof(thermal_config_ddr),
    },
};

constexpr pbus_dev_t thermal_dev_pll = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-thermal-pll";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_T931;
  dev.did = PDEV_DID_AMLOGIC_THERMAL_PLL;
  dev.mmio_list = thermal_mmios_pll;
  dev.mmio_count = countof(thermal_mmios_pll);
  dev.irq_list = thermal_irqs_pll;
  dev.irq_count = countof(thermal_irqs_pll);
  dev.metadata_list = thermal_metadata_pll;
  dev.metadata_count = countof(thermal_metadata_pll);
  return dev;
}();

constexpr pbus_dev_t thermal_dev_ddr = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-thermal-ddr";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_T931;
  dev.did = PDEV_DID_AMLOGIC_THERMAL_DDR;
  dev.mmio_list = thermal_mmios_ddr;
  dev.mmio_count = countof(thermal_mmios_ddr);
  dev.irq_list = thermal_irqs_ddr;
  dev.irq_count = countof(thermal_irqs_ddr);
  dev.metadata_list = thermal_metadata_ddr;
  dev.metadata_count = countof(thermal_metadata_ddr);
  return dev;
}();

}  // namespace

zx_status_t Sherlock::SherlockThermalInit() {
  // Configure the GPIO to be Output & set it to alternate
  // function 3 which puts in PWM_D mode. A53 cluster (Small)
  gpio_impl_.SetAltFunction(T931_GPIOE(1), kPwmDFn);

  zx_status_t status = gpio_impl_.ConfigOut(T931_GPIOE(1), 0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ConfigOut failed: %d", __func__, status);
    return status;
  }

  // Configure the GPIO to be Output & set it to alternate
  // function 3 which puts in PWM_D mode. A73 cluster (Big)
  gpio_impl_.SetAltFunction(T931_GPIOE(2), kPwmDFn);

  status = gpio_impl_.ConfigOut(T931_GPIOE(2), 0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ConfigOut failed: %d", __func__, status);
    return status;
  }

  // The PLL sensor is controlled by a legacy thermal device, which performs DVFS.
  status =
      pbus_.AddComposite(&thermal_dev_pll, reinterpret_cast<uint64_t>(aml_thermal_pll_fragments),
                         countof(aml_thermal_pll_fragments), "pdev");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: AddComposite failed %d", __func__, status);
    return status;
  }

  // The DDR sensor is controlled by a non-legacy thermal device, which only reads temperature.
  status = pbus_.DeviceAdd(&thermal_dev_ddr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed: %d", __func__, status);
    return status;
  }

  return status;
}

zx_status_t Sherlock::ThermalInit() {
  switch (pid_) {
    case PDEV_PID_LUIS:
      return LuisThermalInit();
    case PDEV_PID_SHERLOCK:
      return SherlockThermalInit();
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

}  // namespace sherlock

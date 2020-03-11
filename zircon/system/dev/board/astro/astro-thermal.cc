// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/thermal/c/fidl.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/aml-common/aml-thermal.h>
#include <soc/aml-meson/g12a-clk.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <soc/aml-s905d2/s905d2-pwm.h>

#include "astro.h"

namespace astro {

static const pbus_mmio_t thermal_mmios[] = {
    {
        .base = S905D2_TEMP_SENSOR_BASE,
        .length = S905D2_TEMP_SENSOR_LENGTH,
    },
    {
        .base = S905D2_GPIO_A0_BASE,
        .length = S905D2_GPIO_AO_LENGTH,
    },
    {
        .base = S905D2_HIU_BASE,
        .length = S905D2_HIU_LENGTH,
    },
};

static const pbus_irq_t thermal_irqs[] = {
    {
        .irq = S905D2_TS_PLL_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

constexpr fuchsia_hardware_thermal_ThermalTemperatureInfo TripPoint(float temp_c, uint16_t cpu_opp,
                                                                    uint16_t gpu_opp) {
  constexpr float kHysteresis = 2.0f;

  return {
      .up_temp_celsius = temp_c + kHysteresis,
      .down_temp_celsius = temp_c - kHysteresis,
      .fan_level = 0,
      .big_cluster_dvfs_opp = cpu_opp,
      .little_cluster_dvfs_opp = 0,
      .gpu_clk_freq_source = gpu_opp,
  };
}

/*
 * PASSIVE COOLING - For Astro, we have DVFS support added
 * Below is the operating point information for Big cluster
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
 * GPU_CLK_FREQUENCY_SOURCE -
 * 0 - 285.7 MHz
 * 1 - 400 MHz
 * 2 - 500 MHz
 * 3 - 666 MHz
 * 4 - 800 MHz
 * 5 - 846 MHz
 */

// NOTE: This is a very trivial policy, no data backing it up
// As we do more testing this policy can evolve.
static fuchsia_hardware_thermal_ThermalDeviceInfo astro_config = {
    .active_cooling = false,
    .passive_cooling = true,
    .gpu_throttling = true,
    .num_trip_points = 7,
    .big_little = false,
    .critical_temp_celsius = 102.0f,
    .trip_point_info =
        {
            // The first trip point entry is the default state of the machine
            // and the driver does not use the specified temperature/hysterisis
            // to set any interrupt trip points.
            TripPoint(0.0f, 10, 5), TripPoint(75.0f, 9, 4), TripPoint(80.0f, 8, 3),
            TripPoint(85.0f, 7, 3), TripPoint(90.0f, 6, 2), TripPoint(95.0f, 5, 1),
            TripPoint(100.0f, 4, 0),
            TripPoint(-273.15f, 0, 0),  // 0 Kelvin is impossible, marks end of TripPoints
        },
    .opps =
        {
            // Considering this as LITTLE one since in BIG-LITTLE arch for same
            // thermal driver, these settings apply to the LITTLE cluster.
            [fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN] =
                {
                    .opp =
                        {
                            [0] = {.freq_hz = 100'000'000, .volt_uv = 731'000},
                            [1] = {.freq_hz = 250'000'000, .volt_uv = 731'000},
                            [2] = {.freq_hz = 500'000'000, .volt_uv = 731'000},
                            [3] = {.freq_hz = 667'000'000, .volt_uv = 731'000},
                            [4] = {.freq_hz = 1'000'000'000, .volt_uv = 731'000},
                            [5] = {.freq_hz = 1'200'000'000, .volt_uv = 731'000},
                            [6] = {.freq_hz = 1'398'000'000, .volt_uv = 761'000},
                            [7] = {.freq_hz = 1'512'000'000, .volt_uv = 791'000},
                            [8] = {.freq_hz = 1'608'000'000, .volt_uv = 831'000},
                            [9] = {.freq_hz = 1'704'000'000, .volt_uv = 861'000},
                            [10] = {.freq_hz = 1'896'000'000, .volt_uv = 981'000},
                        },
                    .latency = 0,
                    .count = 11,
                },
        },
};

static aml_thermal_info_t aml_thermal_info = {
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
            [fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN] = 1'200'000'000,
        },
    .voltage_pwm_period_ns = 1250,
    .opps = {},
    .cluster_id_map = {},
};

static const pbus_metadata_t thermal_metadata[] = {
    {
        .type = DEVICE_METADATA_THERMAL_CONFIG,
        .data_buffer = &astro_config,
        .data_size = sizeof(astro_config),
    },
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = &aml_thermal_info,
        .data_size = sizeof(aml_thermal_info),
    },
};

static pbus_dev_t thermal_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-thermal";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_S905D2;
  dev.did = PDEV_DID_AMLOGIC_THERMAL;
  dev.mmio_list = thermal_mmios;
  dev.mmio_count = countof(thermal_mmios);
  dev.irq_list = thermal_irqs;
  dev.irq_count = countof(thermal_irqs);
  dev.metadata_list = thermal_metadata;
  dev.metadata_count = countof(thermal_metadata);
  return dev;
}();

constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
const zx_bind_inst_t pwm_ao_d_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PWM),
    BI_MATCH_IF(EQ, BIND_PWM_ID, S905D2_PWM_AO_D),
};
static const zx_bind_inst_t clk1_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, g12a_clk::CLK_SYS_PLL_DIV16),
};
static const zx_bind_inst_t clk2_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, g12a_clk::CLK_SYS_CPU_CLK_DIV16),
};
const device_fragment_part_t pwm_ao_d_fragment[] = {
    {countof(root_match), root_match},
    {countof(pwm_ao_d_match), pwm_ao_d_match},
};
static const device_fragment_part_t clk1_fragment[] = {
    {countof(root_match), root_match},
    {countof(clk1_match), clk1_match},
};
static const device_fragment_part_t clk2_fragment[] = {
    {countof(root_match), root_match},
    {countof(clk2_match), clk2_match},
};
static const device_fragment_t fragments[] = {
    {countof(pwm_ao_d_fragment), pwm_ao_d_fragment},
    {countof(clk1_fragment), clk1_fragment},
    {countof(clk2_fragment), clk2_fragment},
};

zx_status_t Astro::ThermalInit() {
  // Configure the GPIO to be Output & set it to alternate
  // function 3 which puts in PWM_D mode.
  zx_status_t status = gpio_impl_.ConfigOut(S905D2_PWM_D_PIN, 0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ConfigOut failed: %d\n", __func__, status);
    return status;
  }

  status = gpio_impl_.SetAltFunction(S905D2_PWM_D_PIN, S905D2_PWM_D_FN);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: SetAltFunction failed: %d\n", __func__, status);
    return status;
  }

  status = pbus_.CompositeDeviceAdd(&thermal_dev, fragments, countof(fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed: %d\n", __func__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace astro

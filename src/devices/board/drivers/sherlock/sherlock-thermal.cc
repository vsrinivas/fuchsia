// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/thermal/c/fidl.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/camera.h>
#include <ddk/platform-defs.h>
#include <ddktl/protocol/gpioimpl.h>
#include <hw/reg.h>
#include <soc/aml-common/aml-thermal.h>
#include <soc/aml-meson/g12b-clk.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>
#include <soc/aml-t931/t931-pwm.h>

#include "sherlock.h"

namespace sherlock {

namespace {

constexpr uint32_t kPwmDFn = 3;

const pbus_mmio_t thermal_mmios[] = {
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

const pbus_irq_t thermal_irqs[] = {
    {
        .irq = T931_TS_PLL_IRQ,
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
fuchsia_hardware_thermal_ThermalDeviceInfo aml_sherlock_config =
    {
        .active_cooling = false,
        .passive_cooling = true,
        .gpu_throttling = true,
        .num_trip_points = 6,
        .big_little = true,
        .critical_temp_celsius = 101.0f,
        .trip_point_info =
            {
                TripPoint(55.0f, 9, 10, 4), TripPoint(75.0f, 8, 9, 4), TripPoint(80.0f, 7, 8, 3),
                TripPoint(90.0f, 6, 7, 3), TripPoint(95.0f, 5, 6, 3), TripPoint(100.0f, 4, 5, 2),
                TripPoint(-273.15f, 0, 0, 0),  // 0 Kelvin is impossible, marks end of TripPoints
            },
        .opps =
            {
                [fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN] =
                    {
                        .opp =
                            {
                                [0] = {.freq_hz = 100'000'000, .volt_uv = 751'000},
                                [1] = {.freq_hz = 250'000'000, .volt_uv = 751'000},
                                [2] = {.freq_hz = 500'000'000, .volt_uv = 751'000},
                                [3] = {.freq_hz = 667'000'000, .volt_uv = 751'000},
                                [4] = {.freq_hz = 1'000'000'000, .volt_uv = 771'000},
                                [5] = {.freq_hz = 1'200'000'000, .volt_uv = 771'000},
                                [6] = {.freq_hz = 1'398'000'000, .volt_uv = 791'000},
                                [7] = {.freq_hz = 1'512'000'000, .volt_uv = 821'000},
                                [8] = {.freq_hz = 1'608'000'000, .volt_uv = 861'000},
                                [9] = {.freq_hz = 1'704'000'000, .volt_uv = 891'000},
                                [10] = {.freq_hz = 1'704'000'000, .volt_uv = 891'000},
                            },
                        .latency = 0,
                        .count = 10,
                    },
                [fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN] =
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
                                [10] = {.freq_hz = 1'896'000'000, .volt_uv = 1'011'000},
                            },
                        .latency = 0,
                        .count = 11,
                    },
            },
};

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
            [fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN] = 1'000'000'000,
            [fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN] = 1'200'000'000,
        },
    .voltage_pwm_period_ns = 1250,
    .opps = {},
    .cluster_id_map = {},
};

const pbus_metadata_t thermal_metadata[] = {
    {
        .type = DEVICE_METADATA_THERMAL_CONFIG,
        .data_buffer = &aml_sherlock_config,
        .data_size = sizeof(aml_sherlock_config),
    },
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = &aml_thermal_info,
        .data_size = sizeof(aml_thermal_info),
    },
};

constexpr pbus_dev_t thermal_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-thermal-pll";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_T931;
  dev.did = PDEV_DID_AMLOGIC_THERMAL_PLL;
  dev.mmio_list = thermal_mmios;
  dev.mmio_count = countof(thermal_mmios);
  dev.irq_list = thermal_irqs;
  dev.irq_count = countof(thermal_irqs);
  dev.metadata_list = thermal_metadata;
  dev.metadata_count = countof(thermal_metadata);
  return dev;
}();

const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
const zx_bind_inst_t pwm_ao_d_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PWM),
    BI_MATCH_IF(EQ, BIND_PWM_ID, T931_PWM_AO_D),
};
const zx_bind_inst_t pwm_a_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PWM),
    BI_MATCH_IF(EQ, BIND_PWM_ID, T931_PWM_A),
};
const zx_bind_inst_t clk1_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, g12b_clk::G12B_CLK_SYS_PLL_DIV16),
};
const zx_bind_inst_t clk2_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, g12b_clk::G12B_CLK_SYS_CPU_CLK_DIV16),
};
const zx_bind_inst_t clk3_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, g12b_clk::G12B_CLK_SYS_PLLB_DIV16),
};
const zx_bind_inst_t clk4_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, g12b_clk::G12B_CLK_SYS_CPUB_CLK_DIV16),
};
const device_fragment_part_t pwm_ao_d_fragment[] = {
    {countof(root_match), root_match},
    {countof(pwm_ao_d_match), pwm_ao_d_match},
};
const device_fragment_part_t pwm_a_fragment[] = {
    {countof(root_match), root_match},
    {countof(pwm_a_match), pwm_a_match},
};
const device_fragment_part_t clk1_fragment[] = {
    {countof(root_match), root_match},
    {countof(clk1_match), clk1_match},
};
const device_fragment_part_t clk2_fragment[] = {
    {countof(root_match), root_match},
    {countof(clk2_match), clk2_match},
};
const device_fragment_part_t clk3_fragment[] = {
    {countof(root_match), root_match},
    {countof(clk3_match), clk3_match},
};
const device_fragment_part_t clk4_fragment[] = {
    {countof(root_match), root_match},
    {countof(clk4_match), clk4_match},
};
const device_fragment_t fragments[] = {
    // First fragment must be big cluster PWM, second must be little cluster PWM.
    {"pwm-a", countof(pwm_a_fragment), pwm_a_fragment},
    {"pwm-ao-d", countof(pwm_ao_d_fragment), pwm_ao_d_fragment},
    {"clock-1", countof(clk1_fragment), clk1_fragment},
    {"clock-2", countof(clk2_fragment), clk2_fragment},
    {"clock-3", countof(clk3_fragment), clk3_fragment},
    {"clock-4", countof(clk4_fragment), clk4_fragment},
};

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

  status = pbus_.CompositeDeviceAdd(&thermal_dev, fragments, countof(fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
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

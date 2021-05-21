// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/gpioimpl/cpp/banjo.h>
#include <fuchsia/hardware/thermal/c/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>
#include <lib/ddk/hw/reg.h>

#include <lib/ddk/metadata.h>
#include <ddk/metadata/camera.h>
#include <soc/aml-a311d/a311d-gpio.h>
#include <soc/aml-a311d/a311d-hw.h>
#include <soc/aml-a311d/a311d-pwm.h>
#include <soc/aml-common/aml-thermal.h>
#include <soc/aml-meson/g12b-clk.h>

#include "vim3.h"

namespace vim3 {

namespace {

static constexpr pbus_mmio_t thermal_mmios_pll[] = {
    {
        .base = A311D_TEMP_SENSOR_PLL_BASE,
        .length = A311D_TEMP_SENSOR_PLL_LENGTH,
    },
    {
        .base = A311D_TEMP_SENSOR_PLL_TRIM,
        .length = A311D_TEMP_SENSOR_TRIM_LENGTH,
    },
    {
        .base = A311D_HIU_BASE,
        .length = A311D_HIU_LENGTH,
    },
};

static constexpr pbus_mmio_t thermal_mmios_ddr[] = {
    {
        .base = A311D_TEMP_SENSOR_DDR_BASE,
        .length = A311D_TEMP_SENSOR_DDR_LENGTH,
    },
    {
        .base = A311D_TEMP_SENSOR_DDR_TRIM,
        .length = A311D_TEMP_SENSOR_TRIM_LENGTH,
    },
    {
        .base = A311D_HIU_BASE,
        .length = A311D_HIU_LENGTH,
    },
};

static constexpr pbus_irq_t thermal_irqs_pll[] = {
    {
        .irq = A311D_TS_PLL_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static constexpr pbus_irq_t thermal_irqs_ddr[] = {
    {
        .irq = A311D_TS_DDR_IRQ,
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
 * PASSIVE COOLING - For Vim3, we have DVFS support added
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
static constexpr fuchsia_hardware_thermal_ThermalDeviceInfo thermal_config_pll =
    {
        .active_cooling = false,
        .passive_cooling = true,
        .gpu_throttling = true,
        .num_trip_points = 4,
        .big_little = true,
        .critical_temp_celsius = 101.0f,
        // clang-format off
        .trip_point_info =
            {
                TripPoint(82.5f, 9, 10, 4),
                TripPoint(85.0f, 8, 9, 4),
                TripPoint(87.5f, 6, 6, 4),
                TripPoint(90.0f, 4, 4, 4),
                TripPoint(-273.15f, 0, 0, 0),  // 0 Kelvin is impossible, marks end of TripPoints
            },
        // clang-format on
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

static constexpr fuchsia_hardware_thermal_ThermalDeviceInfo thermal_config_ddr = {
    .active_cooling = false,
    .passive_cooling = false,
    .gpu_throttling = false,
    .num_trip_points = 0,
    .big_little = false,
    .critical_temp_celsius = 110.0,
    .trip_point_info = {TripPoint(-273.15f, 0, 0, 0)},  // Unused
    .opps = {},
};

static constexpr aml_thermal_info_t aml_thermal_info = {
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

static constexpr pbus_dev_t thermal_dev_pll = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-thermal-pll";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_A311D;
  dev.did = PDEV_DID_AMLOGIC_THERMAL_PLL;
  dev.mmio_list = thermal_mmios_pll;
  dev.mmio_count = countof(thermal_mmios_pll);
  dev.irq_list = thermal_irqs_pll;
  dev.irq_count = countof(thermal_irqs_pll);
  dev.metadata_list = thermal_metadata_pll;
  dev.metadata_count = countof(thermal_metadata_pll);
  return dev;
}();

static constexpr pbus_dev_t thermal_dev_ddr = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-thermal-ddr";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_A311D;
  dev.did = PDEV_DID_AMLOGIC_THERMAL_DDR;
  dev.mmio_list = thermal_mmios_ddr;
  dev.mmio_count = countof(thermal_mmios_ddr);
  dev.irq_list = thermal_irqs_ddr;
  dev.irq_count = countof(thermal_irqs_ddr);
  dev.metadata_list = thermal_metadata_ddr;
  dev.metadata_count = countof(thermal_metadata_ddr);
  return dev;
}();

const zx_bind_inst_t pwm_ao_d_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PWM),
    BI_MATCH_IF(EQ, BIND_PWM_ID, A311D_PWM_AO_D),
};
const zx_bind_inst_t pwm_a_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PWM),
    BI_MATCH_IF(EQ, BIND_PWM_ID, A311D_PWM_A),
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
    {countof(pwm_ao_d_match), pwm_ao_d_match},
};
const device_fragment_part_t pwm_a_fragment[] = {
    {countof(pwm_a_match), pwm_a_match},
};
const device_fragment_part_t clk1_fragment[] = {
    {countof(clk1_match), clk1_match},
};
const device_fragment_part_t clk2_fragment[] = {
    {countof(clk2_match), clk2_match},
};
const device_fragment_part_t clk3_fragment[] = {
    {countof(clk3_match), clk3_match},
};
const device_fragment_part_t clk4_fragment[] = {
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

zx_status_t Vim3::ThermalInit() {
  // Configure the GPIO to be Output & set it to alternate
  // function 3 which puts in PWM_D mode. A53 cluster (Small)
  gpio_impl_.SetAltFunction(A311D_GPIOE(1), A311D_GPIOE_1_PWM_D_FN);

  zx_status_t status = gpio_impl_.ConfigOut(A311D_GPIOE(1), 0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ConfigOut failed: %d", __func__, status);
    return status;
  }

  // Configure the GPIO to be Output & set it to alternate
  // function 3 which puts in PWM_D mode. A73 cluster (Big)
  gpio_impl_.SetAltFunction(A311D_GPIOE(2), A311D_GPIOE_2_PWM_D_FN);

  status = gpio_impl_.ConfigOut(A311D_GPIOE(2), 0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ConfigOut failed: %d", __func__, status);
    return status;
  }

  // The PLL sensor is controlled by a legacy thermal device, which performs DVFS.
  status = pbus_.CompositeDeviceAdd(&thermal_dev_pll, reinterpret_cast<uint64_t>(fragments),
                                    countof(fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
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

}  // namespace vim3

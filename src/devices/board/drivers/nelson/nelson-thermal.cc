// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <fuchsia/hardware/thermal/c/fidl.h>
#include <zircon/syscalls/smc.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <soc/aml-common/aml-thermal.h>
#include <soc/aml-meson/sm1-clk.h>
#include <soc/aml-s905d3/s905d3-gpio.h>
#include <soc/aml-s905d3/s905d3-hw.h>
#include <soc/aml-s905d3/s905d3-pwm.h>

#include "nelson.h"

namespace nelson {

static const pbus_mmio_t thermal_mmios[] = {
    {
        .base = S905D3_TEMP_SENSOR_BASE,
        .length = S905D3_TEMP_SENSOR_LENGTH,
    },
    {
        .base = S905D3_TEMP_SENSOR_TRIM,
        .length = S905D3_TEMP_SENSOR_TRIM_LENGTH,
    },
    {
        .base = S905D3_HIU_BASE,
        .length = S905D3_HIU_LENGTH,
    },
};

static const pbus_irq_t thermal_irqs[] = {
    {
        .irq = S905D3_TS_PLL_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_smc_t thermal_smcs[] = {
    {
        .service_call_num_base = ARM_SMC_SERVICE_CALL_NUM_SIP_SERVICE_BASE,
        .count = ARM_SMC_SERVICE_CALL_NUM_SIP_SERVICE_LENGTH,
        .exclusive = false,
    },
};

constexpr fuchsia_hardware_thermal_ThermalTemperatureInfo TripPoint(float temp_c,
                                                                    float hysteresis_c,
                                                                    uint16_t cpu_opp,
                                                                    uint16_t gpu_opp) {
  return {
      .up_temp_celsius = temp_c + hysteresis_c,
      .down_temp_celsius = temp_c - hysteresis_c,
      .fan_level = 0,
      .big_cluster_dvfs_opp = cpu_opp,
      .little_cluster_dvfs_opp = 0,
      .gpu_clk_freq_source = gpu_opp,
  };
}

/*
 * GPU_CLK_FREQUENCY_SOURCE -
 * 0 - 285.7 MHz
 * 1 - 400 MHz
 * 2 - 500 MHz
 * 3 - 666 MHz
 * 4 - 800 MHz
 * 5 - 846 MHz
 */
static const fuchsia_hardware_thermal_ThermalDeviceInfo nelson_config = {
    .active_cooling = false,
    .passive_cooling = true,
    .gpu_throttling = true,
    .num_trip_points = 5,
    .big_little = false,
    .critical_temp_celsius = 110.0f,
    .trip_point_info =
        {
            // The first trip point entry is the default state of the machine
            // and the driver does not use the specified temperature/hysterisis
            // to set any interrupt trip points.
            TripPoint(0.0f, 5.0f, 11, 5),
            TripPoint(60.0f, 5.0f, 9, 4),
            TripPoint(75.0f, 5.0f, 8, 3),
            TripPoint(80.0f, 5.0f, 7, 2),
            TripPoint(110.0f, 1.0f, 0, 0),
            // 0 Kelvin is impossible, marks end of TripPoints
            TripPoint(-273.15f, 2.0f, 0, 0),
        },
    .opps = {},
};

static const aml_thermal_info_t
    aml_thermal_info =
        {
            .voltage_table =
                {
                    [0] = {.microvolt = 1'050'000, .duty_cycle = 0},
                    [1] = {.microvolt = 1'040'000, .duty_cycle = 3},
                    [2] = {.microvolt = 1'030'000, .duty_cycle = 6},
                    [3] = {.microvolt = 1'020'000, .duty_cycle = 8},
                    [4] = {.microvolt = 1'010'000, .duty_cycle = 11},
                    [5] = {.microvolt = 1'000'000, .duty_cycle = 14},
                    [6] = {.microvolt = 990'000, .duty_cycle = 17},
                    [7] = {.microvolt = 980'000, .duty_cycle = 20},
                    [8] = {.microvolt = 970'000, .duty_cycle = 23},
                    [9] = {.microvolt = 960'000, .duty_cycle = 26},
                    [10] = {.microvolt = 950'000, .duty_cycle = 29},
                    [11] = {.microvolt = 940'000, .duty_cycle = 31},
                    [12] = {.microvolt = 930'000, .duty_cycle = 34},
                    [13] = {.microvolt = 920'000, .duty_cycle = 37},
                    [14] = {.microvolt = 910'000, .duty_cycle = 40},
                    [15] = {.microvolt = 900'000, .duty_cycle = 43},
                    [16] = {.microvolt = 890'000, .duty_cycle = 45},
                    [17] = {.microvolt = 880'000, .duty_cycle = 48},
                    [18] = {.microvolt = 870'000, .duty_cycle = 51},
                    [19] = {.microvolt = 860'000, .duty_cycle = 54},
                    [20] = {.microvolt = 850'000, .duty_cycle = 56},
                    [21] = {.microvolt = 840'000, .duty_cycle = 59},
                    [22] = {.microvolt = 830'000, .duty_cycle = 62},
                    [23] = {.microvolt = 820'000, .duty_cycle = 65},
                    [24] = {.microvolt = 810'000, .duty_cycle = 68},
                    [25] = {.microvolt = 800'000, .duty_cycle = 70},
                    [26] = {.microvolt = 790'000, .duty_cycle = 73},
                    [27] = {.microvolt = 780'000, .duty_cycle = 76},
                    [28] = {.microvolt = 770'000, .duty_cycle = 79},
                    [29] = {.microvolt = 760'000, .duty_cycle = 81},
                    [30] = {.microvolt = 750'000, .duty_cycle = 84},
                    [31] = {.microvolt = 740'000, .duty_cycle = 87},
                    [32] = {.microvolt = 730'000, .duty_cycle = 89},
                    [33] = {.microvolt = 720'000, .duty_cycle = 92},
                    [34] = {.microvolt = 710'000, .duty_cycle = 95},
                    [35] = {.microvolt = 700'000, .duty_cycle = 98},
                    [36] = {.microvolt = 690'000, .duty_cycle = 100},
                },
            .initial_cluster_frequencies =
                {
                    [fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN] = 1'200'000'000,
                },
            .voltage_pwm_period_ns = 1500,
            .opps =
                {
                    [fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN] =
                        {
                            [0] =
                                {
                                    .opp =
                                        {
                                            [0] = {.freq_hz = 100'000'000, .volt_uv = 760'000},
                                            [1] = {.freq_hz = 250'000'000, .volt_uv = 760'000},
                                            [2] = {.freq_hz = 500'000'000, .volt_uv = 760'000},
                                            [3] = {.freq_hz = 667'000'000, .volt_uv = 780'000},
                                            [4] = {.freq_hz = 1'000'000'000, .volt_uv = 800'000},
                                            [5] = {.freq_hz = 1'200'000'000, .volt_uv = 810'000},
                                            [6] = {.freq_hz = 1'404'000'000, .volt_uv = 820'000},
                                            [7] = {.freq_hz = 1'512'000'000, .volt_uv = 830'000},
                                            [8] = {.freq_hz = 1'608'000'000, .volt_uv = 860'000},
                                            [9] = {.freq_hz = 1'704'000'000, .volt_uv = 900'000},
                                            [10] = {.freq_hz = 1'800'000'000, .volt_uv = 940'000},
                                            [11] = {.freq_hz = 1'908'000'000, .volt_uv = 970'000},
                                        },
                                    .latency = 0,
                                    .count = 12,
                                },
                            [1] =
                                {
                                    .opp =
                                        {
                                            [0] = {.freq_hz = 100'000'000, .volt_uv = 760'000},
                                            [1] = {.freq_hz = 250'000'000, .volt_uv = 760'000},
                                            [2] = {.freq_hz = 500'000'000, .volt_uv = 760'000},
                                            [3] = {.freq_hz = 667'000'000, .volt_uv = 780'000},
                                            [4] = {.freq_hz = 1'000'000'000, .volt_uv = 800'000},
                                            [5] = {.freq_hz = 1'200'000'000, .volt_uv = 810'000},
                                            [6] = {.freq_hz = 1'404'000'000, .volt_uv = 820'000},
                                            [7] = {.freq_hz = 1'500'000'000, .volt_uv = 830'000},
                                            [8] = {.freq_hz = 1'608'000'000, .volt_uv = 860'000},
                                            [9] = {.freq_hz = 1'704'000'000, .volt_uv = 900'000},
                                            [10] = {.freq_hz = 1'800'000'000, .volt_uv = 910'000},
                                            [11] = {.freq_hz = 1'908'000'000, .volt_uv = 910'000},
                                        },
                                    .latency = 0,
                                    .count = 12,
                                },
                            [2] =
                                {
                                    .opp =
                                        {
                                            [0] = {.freq_hz = 100'000'000, .volt_uv = 760'000},
                                            [1] = {.freq_hz = 250'000'000, .volt_uv = 760'000},
                                            [2] = {.freq_hz = 500'000'000, .volt_uv = 760'000},
                                            [3] = {.freq_hz = 667'000'000, .volt_uv = 780'000},
                                            [4] = {.freq_hz = 1'000'000'000, .volt_uv = 800'000},
                                            [5] = {.freq_hz = 1'200'000'000, .volt_uv = 810'000},
                                            [6] = {.freq_hz = 1'404'000'000, .volt_uv = 820'000},
                                            [7] = {.freq_hz = 1'500'000'000, .volt_uv = 830'000},
                                            [8] = {.freq_hz = 1'608'000'000, .volt_uv = 860'000},
                                            [9] = {.freq_hz = 1'704'000'000, .volt_uv = 860'000},
                                            [10] = {.freq_hz = 1'800'000'000, .volt_uv = 860'000},
                                            [11] = {.freq_hz = 1'908'000'000, .volt_uv = 860'000},
                                        },
                                    .latency = 0,
                                    .count = 12,
                                },
                        },
                },
            .cluster_id_map =
                {
                    [fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN] = 0,
                },
};

static const pbus_metadata_t thermal_metadata[] = {
    {
        .type = DEVICE_METADATA_THERMAL_CONFIG,
        .data_buffer = reinterpret_cast<const uint8_t*>(&nelson_config),
        .data_size = sizeof(nelson_config),
    },
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = reinterpret_cast<const uint8_t*>(&aml_thermal_info),
        .data_size = sizeof(aml_thermal_info),
    },
};

static const pbus_dev_t thermal_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-thermal-pll";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_S905D3;
  dev.did = PDEV_DID_AMLOGIC_THERMAL_PLL;
  dev.mmio_list = thermal_mmios;
  dev.mmio_count = countof(thermal_mmios);
  dev.irq_list = thermal_irqs;
  dev.irq_count = countof(thermal_irqs);
  dev.metadata_list = thermal_metadata;
  dev.metadata_count = countof(thermal_metadata);
  dev.smc_list = thermal_smcs;
  dev.smc_count = countof(thermal_smcs);
  return dev;
}();

constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
const zx_bind_inst_t pwm_ao_d_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PWM),
    BI_MATCH_IF(EQ, BIND_PWM_ID, S905D3_PWM_AO_D),
};
static const zx_bind_inst_t clk1_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, sm1_clk::CLK_SYS_PLL_DIV16),
};
static const zx_bind_inst_t clk2_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, sm1_clk::CLK_SYS_CPU_CLK_DIV16),
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
    {"pwm-a", countof(pwm_ao_d_fragment), pwm_ao_d_fragment},
    {"clock-1", countof(clk1_fragment), clk1_fragment},
    {"clock-2", countof(clk2_fragment), clk2_fragment},
};

zx_status_t Nelson::ThermalInit() {
  // Configure the GPIO to be Output & set it to alternate
  // function 3 which puts in PWM_D mode.
  zx_status_t status = gpio_impl_.ConfigOut(S905D3_PWM_D_PIN, 0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ConfigOut failed: %d", __func__, status);
    return status;
  }

  status = gpio_impl_.SetAltFunction(S905D3_PWM_D_PIN, S905D3_PWM_D_FN);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: SetAltFunction failed: %d", __func__, status);
    return status;
  }

  status = pbus_.CompositeDeviceAdd(&thermal_dev, reinterpret_cast<uint64_t>(fragments),
                                    countof(fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed: %d", __func__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace nelson

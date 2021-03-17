// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <fuchsia/hardware/scpi/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include <lib/ddk/metadata.h>
#include <fbl/algorithm.h>
#include <soc/aml-common/aml-thermal.h>
#include <soc/aml-s912/s912-gpio.h>
#include <soc/aml-s912/s912-hw.h>

#include "vim-gpios.h"
#include "vim.h"

namespace vim {

static const pbus_mmio_t mailbox_mmios[] = {
    // Mailbox
    {
        .base = S912_HIU_MAILBOX_BASE,
        .length = S912_HIU_MAILBOX_LENGTH,
    },
    // Mailbox Payload
    {
        .base = S912_MAILBOX_PAYLOAD_BASE,
        .length = S912_MAILBOX_PAYLOAD_LENGTH,
    },
};

// IRQ for Mailbox
static const pbus_irq_t mailbox_irqs[] = {
    {.irq = S912_MBOX_IRQ_RECEIV0, .mode = ZX_INTERRUPT_MODE_EDGE_HIGH},
    {.irq = S912_MBOX_IRQ_RECEIV1, .mode = ZX_INTERRUPT_MODE_EDGE_HIGH},
    {.irq = S912_MBOX_IRQ_RECEIV2, .mode = ZX_INTERRUPT_MODE_EDGE_HIGH},
    {.irq = S912_MBOX_IRQ_SEND3, .mode = ZX_INTERRUPT_MODE_EDGE_HIGH},
    {.irq = S912_MBOX_IRQ_SEND4, .mode = ZX_INTERRUPT_MODE_EDGE_HIGH},
    {.irq = S912_MBOX_IRQ_SEND5, .mode = ZX_INTERRUPT_MODE_EDGE_HIGH},
};

/* ACTIVE COOLING - For VIM2, we assume that all devices
 * are connected with a GPIO-controlled fan.
 * The GPIO controlled fan has 3 levels of speed (1-3)
 *
 * PASSIVE COOLING - For VIM2, we have DVFS support added
 * Below is the operating point information for Big cluster
 * Operating point 0 - Freq 0.1000 Ghz Voltage 0.9100 V
 * Operating point 1 - Freq 0.2500 Ghz Voltage 0.9100 V
 * Operating point 2 - Freq 0.5000 Ghz Voltage 0.9100 V
 * Operating point 3 - Freq 0.6670 Ghz Voltage 0.9500 V
 * Operating point 4 - Freq 1.0000 Ghz Voltage 0.9900 V
 * Operating point 5 - Freq 1.2000 Ghz Voltage 1.0700 V
 * Operating point 6 - Freq 1.2960 Ghz Voltage 1.1000 V
 *
 * Below is the operating point information for Little cluster
 * Operating point 0 - Freq 0.1000 Ghz Voltage 0.9100 V
 * Operating point 1 - Freq 0.2500 Ghz Voltage 0.9100 V
 * Operating point 2 - Freq 0.5000 Ghz Voltage 0.9100 V
 * Operating point 3 - Freq 0.6670 Ghz Voltage 0.9500 V
 * Operating point 4 - Freq 1.0000 Ghz Voltage 0.9900 V
 *
 * GPU_CLK_FREQUENCY_SOURCE - For VIM2, we support GPU
 * throttling. Currently we have pre-defined frequencies
 * we can set the GPU clock to, but we can always add more
 * One's we support now are below
 * Operating point  0 - 285.7 MHz
 * Operating point  1 - 400.0 MHz
 * Operating point  2 - 500.0 MHz
 * Operating point  3 - 666.0 MHz
 * Operating point -1 - INVALID/No throttling needed
 */

static fuchsia_hardware_thermal_ThermalDeviceInfo aml_vim2_config = {
    .active_cooling = true,
    .passive_cooling = true,
    .gpu_throttling = true,
    .num_trip_points = 8,
    .big_little = true,
    .critical_temp_celsius = 81.0f,
    .trip_point_info =
        {
            {
                // This is the initial thermal setup of the device
                // Fan set to OFF
                // CPU freq set to a known stable MAX

                // c++ initialization error
                .up_temp_celsius = 2.0f,
                .down_temp_celsius = 0.0f,
                // c++ initialization error
                .fan_level = 0,
                .big_cluster_dvfs_opp = 6,
                .little_cluster_dvfs_opp = 4,
                .gpu_clk_freq_source = 3,
            },
            {
                .up_temp_celsius = 65.0f,
                .down_temp_celsius = 63.0f,
                .fan_level = 1,
                .big_cluster_dvfs_opp = 6,
                .little_cluster_dvfs_opp = 4,
                .gpu_clk_freq_source = 3,
            },
            {
                .up_temp_celsius = 70.0f,
                .down_temp_celsius = 68.0f,
                .fan_level = 2,
                .big_cluster_dvfs_opp = 6,
                .little_cluster_dvfs_opp = 4,
                .gpu_clk_freq_source = 3,
            },
            {
                .up_temp_celsius = 75.0f,
                .down_temp_celsius = 73.0f,
                .fan_level = 3,
                .big_cluster_dvfs_opp = 6,
                .little_cluster_dvfs_opp = 4,
                .gpu_clk_freq_source = 3,
            },
            {
                .up_temp_celsius = 82.0f,
                .down_temp_celsius = 79.0f,
                .fan_level = 3,
                .big_cluster_dvfs_opp = 5,
                .little_cluster_dvfs_opp = 4,
                .gpu_clk_freq_source = 2,
            },
            {
                .up_temp_celsius = 87.0f,
                .down_temp_celsius = 84.0f,
                .fan_level = 3,
                .big_cluster_dvfs_opp = 4,
                .little_cluster_dvfs_opp = 4,
                .gpu_clk_freq_source = 2,
            },
            {
                .up_temp_celsius = 92.0f,
                .down_temp_celsius = 89.0f,
                .fan_level = 3,
                .big_cluster_dvfs_opp = 3,
                .little_cluster_dvfs_opp = 3,
                .gpu_clk_freq_source = 1,
            },
            {
                .up_temp_celsius = 96.0f,
                .down_temp_celsius = 93.0f,
                .fan_level = 3,
                .big_cluster_dvfs_opp = 2,
                .little_cluster_dvfs_opp = 2,
                .gpu_clk_freq_source = 0,
            },
        },
    .opps = {},
};

static const pbus_metadata_t vim_thermal_metadata[] = {{
    .type = DEVICE_METADATA_THERMAL_CONFIG,
    .data_buffer = reinterpret_cast<const uint8_t*>(&aml_vim2_config),
    .data_size = sizeof(aml_vim2_config),
}};

// Composite binding rules for thermal driver.
static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t scpi_match[] = {
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_SCPI),
};
static const zx_bind_inst_t fan0_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_THERMAL_FAN_O),
};
static const zx_bind_inst_t fan1_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_THERMAL_FAN_1),
};
static const device_fragment_part_t scpi_fragment[] = {
    {std::size(root_match), root_match},
    {std::size(scpi_match), scpi_match},
};
static const device_fragment_part_t fan0_gpio_fragment[] = {
    {std::size(root_match), root_match},
    {std::size(fan0_gpio_match), fan0_gpio_match},
};
static const device_fragment_part_t fan1_gpio_fragment[] = {
    {std::size(root_match), root_match},
    {std::size(fan1_gpio_match), fan1_gpio_match},
};
static const device_fragment_t fragments[] = {
    {"scpi", std::size(scpi_fragment), scpi_fragment},
    {"gpio-fan-0", std::size(fan0_gpio_fragment), fan0_gpio_fragment},
    {"gpio-fan-1", std::size(fan1_gpio_fragment), fan1_gpio_fragment},
};

zx_status_t Vim::ThermalInit() {
  pbus_dev_t mailbox_dev = {};
  mailbox_dev.name = "mailbox";
  mailbox_dev.vid = PDEV_VID_AMLOGIC;
  mailbox_dev.pid = PDEV_PID_AMLOGIC_S912;
  mailbox_dev.did = PDEV_DID_AMLOGIC_MAILBOX;
  mailbox_dev.mmio_list = mailbox_mmios;
  mailbox_dev.mmio_count = countof(mailbox_mmios);
  mailbox_dev.irq_list = mailbox_irqs;
  mailbox_dev.irq_count = countof(mailbox_irqs);
  mailbox_dev.metadata_list = vim_thermal_metadata;
  mailbox_dev.metadata_count = countof(vim_thermal_metadata);

  zx_status_t status = pbus_.DeviceAdd(&mailbox_dev);

  if (status != ZX_OK) {
    zxlogf(ERROR, "ThermalInit: pbus_device_add failed: %d", status);
    return status;
  }

  // Add a composite device for thermal driver.
  constexpr zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_AMLOGIC},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_AMLOGIC_S912},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_VIM2_THERMAL},
  };

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = countof(props),
      .fragments = fragments,
      .fragments_count = countof(fragments),
      .coresident_device_index = 0,
      .metadata_list = nullptr,
      .metadata_count = 0,
  };

  status = DdkAddComposite("vim-thermal", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: device_add_composite failed: %d", __func__, status);
    return status;
  }
  return ZX_OK;
}
}  // namespace vim

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/aml-meson/sm1-clk.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d3/s905d3-hw.h>

#include "nelson-gpios.h"
#include "nelson.h"

namespace nelson {
constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t ref_out_i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, NELSON_I2C_3),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, I2C_AUDIO_CODEC_ADDR),
};
static const zx_bind_inst_t ref_out_codec_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CODEC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MAXIM),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_MAXIM_MAX98373),  // For Nelson P1.
};
static const zx_bind_inst_t ref_out_clk0_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, sm1_clk::CLK_HIFI_PLL),
};

static const device_component_part_t ref_out_i2c_component[] = {
    {countof(root_match), root_match},
    {countof(ref_out_i2c_match), ref_out_i2c_match},
};
static const device_component_part_t ref_out_codec_component[] = {
    {countof(root_match), root_match},
    {countof(ref_out_codec_match), ref_out_codec_match},
};

static const zx_bind_inst_t ref_out_enable_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_SOC_AUDIO_EN),
};
static const zx_bind_inst_t ref_out_fault_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_AUDIO_SOC_FAULT_L),
};
static const device_component_part_t ref_out_enable_gpio_component[] = {
    {countof(root_match), root_match},
    {countof(ref_out_enable_gpio_match), ref_out_enable_gpio_match},
};
static const device_component_part_t ref_out_fault_gpio_component[] = {
    {countof(root_match), root_match},
    {countof(ref_out_fault_gpio_match), ref_out_fault_gpio_match},
};
static const device_component_part_t ref_out_clk0_component[] = {
    {countof(root_match), root_match},
    {countof(ref_out_clk0_match), ref_out_clk0_match},
};

static const device_component_t codec_components[] = {
    {countof(ref_out_i2c_component), ref_out_i2c_component},
    {countof(ref_out_enable_gpio_component), ref_out_enable_gpio_component},
    {countof(ref_out_enable_gpio_component), ref_out_fault_gpio_component},
};
static const device_component_t controller_components[] = {
    {countof(ref_out_codec_component), ref_out_codec_component},
    {countof(ref_out_clk0_component), ref_out_clk0_component},
};
static const device_component_t in_components[] = {
    {countof(ref_out_clk0_component), ref_out_clk0_component},
};

zx_status_t Nelson::AudioInit() {
  const pbus_mmio_t mmios_out[] = {
      {
          .base = S905D3_EE_AUDIO_BASE,
          .length = S905D3_EE_AUDIO_LENGTH,
      },
  };

  const pbus_bti_t btis_out[] = {
      {
          .iommu_index = 0,
          .bti_id = BTI_AUDIO_OUT,
      },
  };

  pbus_dev_t controller_out = {};
  controller_out.name = "nelson-audio-out";
  controller_out.vid = PDEV_VID_AMLOGIC;
  controller_out.pid = PDEV_PID_AMLOGIC_S905D3;
  controller_out.did = PDEV_DID_AMLOGIC_TDM;
  controller_out.mmio_list = mmios_out;
  controller_out.mmio_count = countof(mmios_out);
  controller_out.bti_list = btis_out;
  controller_out.bti_count = countof(btis_out);

  const pbus_mmio_t mmios_in[] = {
      {
          .base = S905D3_EE_PDM_BASE,
          .length = S905D3_EE_PDM_LENGTH,
      },
      {
          .base = S905D3_EE_AUDIO_BASE,
          .length = S905D3_EE_AUDIO_LENGTH,
      },
  };

  const pbus_bti_t btis_in[] = {
      {
          .iommu_index = 0,
          .bti_id = BTI_AUDIO_IN,
      },
  };

  pbus_dev_t dev_in = {};
  dev_in.name = "nelson-audio-in";
  dev_in.vid = PDEV_VID_AMLOGIC;
  dev_in.pid = PDEV_PID_AMLOGIC_S905D3;
  dev_in.did = PDEV_DID_AMLOGIC_PDM;
  dev_in.mmio_list = mmios_in;
  dev_in.mmio_count = countof(mmios_in);
  dev_in.bti_list = btis_in;
  dev_in.bti_count = countof(btis_in);

  // TDM pin assignments.
  gpio_impl_.SetAltFunction(S905D2_GPIOA(1), S905D2_GPIOA_1_TDMB_SCLK_FN);
  gpio_impl_.SetAltFunction(S905D2_GPIOA(2), S905D2_GPIOA_2_TDMB_FS_FN);
  gpio_impl_.SetAltFunction(S905D2_GPIOA(3), S905D2_GPIOA_3_TDMB_D0_FN);
  gpio_impl_.SetAltFunction(S905D2_GPIOA(6), S905D2_GPIOA_6_TDMB_DIN3_FN);

  // CODEC pin assignments.
  gpio_impl_.ConfigOut(S905D2_GPIOA(5), 0);

  // PDM pin assignments
  gpio_impl_.SetAltFunction(S905D2_GPIOA(7), S905D2_GPIOA_7_PDM_DCLK_FN);
  gpio_impl_.SetAltFunction(S905D2_GPIOA(8), S905D2_GPIOA_8_PDM_DIN0_FN);

  // Output devices.
  constexpr zx_device_prop_t props[] = {{BIND_PLATFORM_DEV_VID, 0, PDEV_VID_MAXIM},
                                        {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_MAXIM_MAX98373}};

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = countof(props),
      .components = codec_components,
      .components_count = countof(codec_components),
      .coresident_device_index = UINT32_MAX,
      .metadata_list = nullptr,
      .metadata_count = 0,
  };

  auto status = DdkAddComposite("audio-max98373", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s DdkAddComposite failed %d\n", __FILE__, status);
    return status;
  }

  status = pbus_.CompositeDeviceAdd(&controller_out, controller_components,
                                    countof(controller_components), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s adding audio controller out device failed %d\n", __FILE__, status);
    return status;
  }

  // Input device.
  status = pbus_.CompositeDeviceAdd(&dev_in, in_components, countof(in_components), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s adding audio input device failed %d\n", __FILE__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace nelson

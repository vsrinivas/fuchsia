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
static const zx_bind_inst_t p2_out_i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, NELSON_I2C_3),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, I2C_AUDIO_CODEC_ADDR_P2),
};
static const zx_bind_inst_t ref_out_codec_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CODEC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MAXIM),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_MAXIM_MAX98373),  // For Nelson P1.
};
static const zx_bind_inst_t p2_out_codec_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CODEC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TI),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TI_TAS5805),  // For Nelson P2.
};
static const zx_bind_inst_t ref_out_clk0_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, sm1_clk::CLK_HIFI_PLL),
};

static const device_fragment_part_t ref_out_i2c_fragment[] = {
    {countof(root_match), root_match},
    {countof(ref_out_i2c_match), ref_out_i2c_match},
};
static const device_fragment_part_t p2_out_i2c_fragment[] = {
    {countof(root_match), root_match},
    {countof(p2_out_i2c_match), p2_out_i2c_match},
};
static const device_fragment_part_t ref_out_codec_fragment[] = {
    {countof(root_match), root_match},
    {countof(ref_out_codec_match), ref_out_codec_match},
};
static const device_fragment_part_t p2_out_codec_fragment[] = {
    {countof(root_match), root_match},
    {countof(p2_out_codec_match), p2_out_codec_match},
};

static const zx_bind_inst_t ref_out_enable_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_SOC_AUDIO_EN),
};
static const zx_bind_inst_t ref_out_fault_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_AUDIO_SOC_FAULT_L),
};
static const device_fragment_part_t ref_out_enable_gpio_fragment[] = {
    {countof(root_match), root_match},
    {countof(ref_out_enable_gpio_match), ref_out_enable_gpio_match},
};
static const device_fragment_part_t ref_out_fault_gpio_fragment[] = {
    {countof(root_match), root_match},
    {countof(ref_out_fault_gpio_match), ref_out_fault_gpio_match},
};
static const device_fragment_part_t ref_out_clk0_fragment[] = {
    {countof(root_match), root_match},
    {countof(ref_out_clk0_match), ref_out_clk0_match},
};

static const device_fragment_t ref_codec_fragments[] = {
    {countof(ref_out_i2c_fragment), ref_out_i2c_fragment},
    {countof(ref_out_enable_gpio_fragment), ref_out_enable_gpio_fragment},
    {countof(ref_out_enable_gpio_fragment), ref_out_fault_gpio_fragment},
};
static const device_fragment_t p2_codec_fragments[] = {
    {countof(ref_out_i2c_fragment), p2_out_i2c_fragment},
    {countof(ref_out_enable_gpio_fragment), ref_out_enable_gpio_fragment},
    {countof(ref_out_enable_gpio_fragment), ref_out_fault_gpio_fragment},
};
static const device_fragment_t ref_controller_fragments[] = {
    {countof(ref_out_codec_fragment), ref_out_codec_fragment},
    {countof(ref_out_clk0_fragment), ref_out_clk0_fragment},
};
static const device_fragment_t p2_controller_fragments[] = {
    {countof(p2_out_codec_fragment), p2_out_codec_fragment},
    {countof(ref_out_clk0_fragment), ref_out_clk0_fragment},
};
static const device_fragment_t in_fragments[] = {
    {countof(ref_out_clk0_fragment), ref_out_clk0_fragment},
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

  // PDM pin assignments
  gpio_impl_.SetAltFunction(S905D2_GPIOA(7), S905D2_GPIOA_7_PDM_DCLK_FN);
  gpio_impl_.SetAltFunction(S905D2_GPIOA(8), S905D2_GPIOA_8_PDM_DIN0_FN);

  // Board info.
  pdev_board_info_t board_info = {};
  auto status = pbus_.GetBoardInfo(&board_info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GetBoardInfo failed %d", __FILE__, status);
    return status;
  }

  // Output devices.

  if (board_info.board_revision < BOARD_REV_P2) {
    // CODEC pin assignments.
    gpio_impl_.SetAltFunction(S905D2_GPIOA(5), 0);  // GPIO
    gpio_impl_.ConfigOut(S905D2_GPIOA(5), 0);

    constexpr zx_device_prop_t props[] = {{BIND_PLATFORM_DEV_VID, 0, PDEV_VID_MAXIM},
                                          {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_MAXIM_MAX98373}};
    composite_device_desc_t codec_desc = {};
    codec_desc.props = props;
    codec_desc.props_count = countof(props);
    codec_desc.coresident_device_index = UINT32_MAX;
    codec_desc.fragments = ref_codec_fragments;
    codec_desc.fragments_count = countof(ref_codec_fragments);
    status = DdkAddComposite("audio-max98373", &codec_desc);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s DdkAddComposite failed %d", __FILE__, status);
      return status;
    }
    status = pbus_.CompositeDeviceAdd(&controller_out, ref_controller_fragments,
                                      countof(ref_controller_fragments), UINT32_MAX);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s adding audio controller out device failed %d", __FILE__, status);
      return status;
    }
  } else {
    // CODEC pin assignments.
    gpio_impl_.SetAltFunction(S905D2_GPIOA(0), 0);   // GPIO.
    gpio_impl_.ConfigOut(S905D2_GPIOA(0), 1);        // BOOST_EN_SOC.
    gpio_impl_.SetAltFunction(S905D2_GPIOA(12), 0);  // GPIO.
    gpio_impl_.ConfigOut(S905D2_GPIOA(12), 0);       // Set PDN_N to Low.
    // From the TAS5805m codec reference manual:
    // "9.5.3.1 Startup Procedures
    // 1. Configure ADR/FAULT pin with proper settings for I2C device address.
    // 2. Bring up power supplies (it does not matter if PVDD or DVDD comes up first).
    // 3. Once power supplies are stable, bring up PDN to High and wait 5ms at least, then
    // start SCLK, LRCLK.
    // 4. Once I2S clocks are stable, set the device into HiZ state and enable DSP via the I2C
    // control port.
    // 5. Wait 5ms at least. Then initialize the DSP Coefficient, then set the device to Play
    // state.
    // 6. The device is now in normal operation."
    // ADR/FAULT is hardwired to VIO18_PMU (always on).
    // PVDD is hardwired to DC_IN.
    // DVDD is hardwired to VIO18_PMU (always on).
    // Step 3 PDN setup and 5ms delay is executed below.
    gpio_impl_.SetAltFunction(S905D2_GPIOA(5), 0);  // GPIO
    gpio_impl_.ConfigOut(S905D2_GPIOA(5), 1);       // Set PDN_N to Low.
    zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));
    // I2S clocks are configured by the controller and the rest of the initialization is done
    // in the codec itself.

    constexpr zx_device_prop_t props[] = {{BIND_PLATFORM_DEV_VID, 0, PDEV_VID_TI},
                                          {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_TI_TAS5805}};
    composite_device_desc_t codec_desc = {};
    codec_desc.props = props;
    codec_desc.props_count = countof(props);
    codec_desc.coresident_device_index = UINT32_MAX;
    codec_desc.fragments = p2_codec_fragments;
    codec_desc.fragments_count = countof(p2_codec_fragments);
    status = DdkAddComposite("audio-tas5805", &codec_desc);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s DdkAddComposite failed %d", __FILE__, status);
      return status;
    }
    status = pbus_.CompositeDeviceAdd(&controller_out, p2_controller_fragments,
                                      countof(p2_controller_fragments), UINT32_MAX);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s adding audio controller out device failed %d", __FILE__, status);
      return status;
    }
  }

  // Input device.
  status = pbus_.CompositeDeviceAdd(&dev_in, in_fragments, countof(in_fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s adding audio input device failed %d", __FILE__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace nelson

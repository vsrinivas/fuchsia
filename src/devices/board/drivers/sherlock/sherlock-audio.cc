// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddktl/metadata/audio.h>
#include <soc/aml-common/aml-audio.h>
#include <soc/aml-meson/g12b-clk.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock-gpios.h"
#include "sherlock.h"

namespace sherlock {

zx_status_t Sherlock::AudioInit() {
  static constexpr pbus_mmio_t audio_mmios[] = {
      {
          .base = T931_EE_AUDIO_BASE,
          .length = T931_EE_AUDIO_LENGTH,
      },
      {
          .base = T931_GPIO_BASE,
          .length = T931_GPIO_LENGTH,
      },
      {
          .base = T931_GPIO_A0_BASE,
          .length = T931_GPIO_AO_LENGTH,
      },
  };

  static constexpr pbus_bti_t tdm_btis[] = {
      {
          .iommu_index = 0,
          .bti_id = BTI_AUDIO_OUT,
      },
  };

  pdev_board_info_t board_info = {};
  zx_status_t status = pbus_.GetBoardInfo(&board_info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GetBoardInfo failed %d", __FILE__, status);
    return status;
  }

  bool is_sherlock = board_info.pid == PDEV_PID_SHERLOCK;
  if (is_sherlock && board_info.board_revision < BOARD_REV_EVT1) {
    // For audio we don't support boards revision lower than EVT.
    zxlogf(WARNING, "%s: Board revision unsupported, skipping audio initialization.", __FILE__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  constexpr zx_bind_inst_t root_match[] = {
      BI_MATCH(),
  };
  constexpr zx_bind_inst_t enable_gpio_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_SOC_AUDIO_EN),
  };
  constexpr zx_bind_inst_t woofer_i2c_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
      BI_ABORT_IF(NE, BIND_I2C_BUS_ID, SHERLOCK_I2C_A0_0),
      BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x6f),
  };
  constexpr zx_bind_inst_t tweeter_left_i2c_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
      BI_ABORT_IF(NE, BIND_I2C_BUS_ID, SHERLOCK_I2C_A0_0),
      BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x6c),
  };
  constexpr zx_bind_inst_t tweeter_right_i2c_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
      BI_ABORT_IF(NE, BIND_I2C_BUS_ID, SHERLOCK_I2C_A0_0),
      BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x6d),
  };
  constexpr zx_bind_inst_t luis_codec_i2c_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
      BI_ABORT_IF(NE, BIND_I2C_BUS_ID, SHERLOCK_I2C_A0_0),
      BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x4c),
  };
  constexpr zx_bind_inst_t codec_woofer_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CODEC),
      BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TI),
      BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_TI_TAS5720),
      BI_MATCH_IF(EQ, BIND_CODEC_INSTANCE, 1),
  };
  constexpr zx_bind_inst_t codec_tweeter_left_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CODEC),
      BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TI),
      BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_TI_TAS5720),
      BI_MATCH_IF(EQ, BIND_CODEC_INSTANCE, 2),
  };
  constexpr zx_bind_inst_t codec_tweeter_right_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CODEC),
      BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TI),
      BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_TI_TAS5720),
      BI_MATCH_IF(EQ, BIND_CODEC_INSTANCE, 3),
  };
  constexpr zx_bind_inst_t luis_codec_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CODEC),
      BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TI),
      BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TI_TAS58xx),
  };

  const device_fragment_part_t enable_gpio_fragment[] = {
      {countof(root_match), root_match},
      {countof(enable_gpio_match), enable_gpio_match},
  };
  const device_fragment_part_t woofer_i2c_fragment[] = {
      {countof(root_match), root_match},
      {countof(woofer_i2c_match), woofer_i2c_match},
  };
  const device_fragment_part_t tweeter_left_i2c_fragment[] = {
      {countof(root_match), root_match},
      {countof(tweeter_left_i2c_match), tweeter_left_i2c_match},
  };
  const device_fragment_part_t tweeter_right_i2c_fragment[] = {
      {countof(root_match), root_match},
      {countof(tweeter_right_i2c_match), tweeter_right_i2c_match},
  };
  const device_fragment_part_t luis_codec_i2c_fragment[] = {
      {countof(root_match), root_match},
      {countof(luis_codec_i2c_match), luis_codec_i2c_match},
  };
  const device_fragment_part_t codec_woofer_fragment[] = {
      {countof(root_match), root_match},
      {countof(codec_woofer_match), codec_woofer_match},
  };
  const device_fragment_part_t codec_tweeter_left_fragment[] = {
      {countof(root_match), root_match},
      {countof(codec_tweeter_left_match), codec_tweeter_left_match},
  };
  const device_fragment_part_t codec_tweeter_right_fragment[] = {
      {countof(root_match), root_match},
      {countof(codec_tweeter_right_match), codec_tweeter_right_match},
  };
  const device_fragment_part_t luis_codec_fragment[] = {
      {countof(root_match), root_match},
      {countof(luis_codec_match), luis_codec_match},
  };
  const device_fragment_t woofer_fragments[] = {
      {countof(woofer_i2c_fragment), woofer_i2c_fragment},
  };
  const device_fragment_t tweeter_left_fragments[] = {
      {countof(tweeter_left_i2c_fragment), tweeter_left_i2c_fragment},
  };
  const device_fragment_t tweeter_right_fragments[] = {
      {countof(tweeter_right_i2c_fragment), tweeter_right_i2c_fragment},
  };
  const device_fragment_t luis_codec_fragments[] = {
      {countof(luis_codec_i2c_fragment), luis_codec_i2c_fragment},
  };
#ifdef ENABLE_BT
  static const device_fragment_t tdm_pcm_fragments[] = {};
#endif
  const device_fragment_t sherlock_tdm_i2s_fragments[] = {
      {countof(enable_gpio_fragment), enable_gpio_fragment},
      {countof(codec_woofer_fragment), codec_woofer_fragment},
      {countof(codec_tweeter_left_fragment), codec_tweeter_left_fragment},
      {countof(codec_tweeter_right_fragment), codec_tweeter_right_fragment},
  };
  const device_fragment_t luis_tdm_i2s_fragments[] = {
      {countof(enable_gpio_fragment), enable_gpio_fragment},
      {countof(luis_codec_fragment), luis_codec_fragment},
  };

  status = clk_impl_.Disable(g12b_clk::CLK_HIFI_PLL);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Disable(CLK_HIFI_PLL) failed, st = %d", __func__, status);
    return status;
  }

  status = clk_impl_.SetRate(g12b_clk::CLK_HIFI_PLL, T931_HIFI_PLL_RATE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: SetRate(CLK_HIFI_PLL) failed, st = %d", __func__, status);
    return status;
  }

  status = clk_impl_.Enable(g12b_clk::CLK_HIFI_PLL);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Enable(CLK_HIFI_PLL) failed, st = %d", __func__, status);
    return status;
  }

  // TDM pin assignments.
  gpio_impl_.SetAltFunction(T931_GPIOZ(7), T931_GPIOZ_7_TDMC_SCLK_FN);
  gpio_impl_.SetAltFunction(T931_GPIOZ(6), T931_GPIOZ_6_TDMC_FS_FN);
  gpio_impl_.SetAltFunction(T931_GPIOZ(2), T931_GPIOZ_2_TDMC_D0_FN);
  if (is_sherlock) {
    gpio_impl_.SetAltFunction(T931_GPIOZ(3), T931_GPIOZ_3_TDMC_D1_FN);
  } else {
    gpio_impl_.SetAltFunction(T931_GPIOZ(3), 0);
  }
  gpio_impl_.SetAltFunction(T931_GPIOAO(9), T931_GPIOAO_9_MCLK_FN);

  // PDM pin assignments.
  gpio_impl_.SetAltFunction(T931_GPIOA(7), T931_GPIOA_7_PDM_DCLK_FN);
  gpio_impl_.SetAltFunction(T931_GPIOA(8), T931_GPIOA_8_PDM_DIN0_FN);
  if (!is_sherlock) {
    gpio_impl_.SetAltFunction(T931_GPIOA(9), T931_GPIOA_9_PDM_DIN1_FN);
  }

  // Add TDM OUT to the codecs.
  if (is_sherlock) {
    gpio_impl_.ConfigOut(T931_GPIOH(7), 1);  // SOC_AUDIO_EN.
    zx_device_prop_t props[] = {{BIND_PLATFORM_DEV_VID, 0, PDEV_VID_TI},
                                {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_TI_TAS5720},
                                {BIND_CODEC_INSTANCE, 0, 1}};
    uint32_t instance_count = 1;
    const device_metadata_t codec_metadata[] = {
        {
            .type = DEVICE_METADATA_PRIVATE,
            .data = &instance_count,
            .length = sizeof(instance_count),
        },
    };

    composite_device_desc_t comp_desc = {};
    comp_desc.props = props;
    comp_desc.props_count = countof(props);
    comp_desc.coresident_device_index = UINT32_MAX;
    comp_desc.fragments = woofer_fragments;
    comp_desc.fragments_count = countof(woofer_fragments);
    comp_desc.metadata_list = codec_metadata;
    comp_desc.metadata_count = countof(codec_metadata);
    status = DdkAddComposite("audio-tas5720-woofer", &comp_desc);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s DdkAddComposite woofer failed %d", __FILE__, status);
      return status;
    }

    instance_count = 2;
    props[2].value = 2;
    comp_desc.fragments = tweeter_left_fragments;
    comp_desc.fragments_count = countof(tweeter_left_fragments);
    status = DdkAddComposite("audio-tas5720-left-tweeter", &comp_desc);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s DdkAddComposite left tweeter failed %d", __FILE__, status);
      return status;
    }

    instance_count = 3;
    props[2].value = 3;
    comp_desc.fragments = tweeter_right_fragments;
    comp_desc.fragments_count = countof(tweeter_right_fragments);
    status = DdkAddComposite("audio-tas5720-right-tweeter", &comp_desc);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s DdkAddComposite right tweeter failed %d", __FILE__, status);
      return status;
    }
  } else {  // Luis.
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
    // Step 3 PDN setup and 5ms delay is executed below.
    gpio_impl_.ConfigOut(T931_GPIOH(7), 1);  // SOC_AUDIO_EN, Set PDN_N to High.
    zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));
    // I2S clocks are configured by the controller and the rest of the initialization is done
    // in the codec itself.

    zx_device_prop_t props[] = {{BIND_PLATFORM_DEV_VID, 0, PDEV_VID_TI},
                                {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_TI_TAS58xx}};

    composite_device_desc_t comp_desc = {};
    comp_desc.props = props;
    comp_desc.props_count = countof(props);
    comp_desc.coresident_device_index = UINT32_MAX;
    comp_desc.fragments = luis_codec_fragments;
    comp_desc.fragments_count = countof(luis_codec_fragments);
    status = DdkAddComposite("audio-tas58xx", &comp_desc);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s DdkAddComposite failed %d", __FILE__, status);
      return status;
    }
  }
  metadata::AmlConfig metadata = {};
  snprintf(metadata.manufacturer, sizeof(metadata.manufacturer), "Spacely Sprockets");
  metadata.is_input = false;
  // Compatible clocks with other TDM drivers.
  metadata.mClockDivFactor = 125;
  metadata.sClockDivFactor = 4;
  metadata.bus = metadata::AmlBus::TDM_C;
  metadata.version = metadata::AmlVersion::kS905D2G;  // Also works with T931G.
  metadata.tdm.type = metadata::TdmType::I2s;
  if (is_sherlock) {
    snprintf(metadata.product_name, sizeof(metadata.product_name), "sherlock");
    metadata.tdm.number_of_codecs = 3;
    metadata.tdm.codecs[0] = metadata::Codec::Tas5720;
    metadata.tdm.codecs[1] = metadata::Codec::Tas5720;
    metadata.tdm.codecs[2] = metadata::Codec::Tas5720;
    metadata.number_of_channels = 4;
    metadata.swaps = 0x1032;
    metadata.lanes_enable_mask[0] = 3;
    metadata.lanes_enable_mask[1] = 3;
    // Boost the woofer above tweeters by 7.1db analog and 5.5db digital needed for this product.
    metadata.tdm.codecs_delta_gains[0] = 0.f;
    metadata.tdm.codecs_delta_gains[1] = -12.6f;
    metadata.tdm.codecs_delta_gains[2] = -12.6f;
    metadata.codecs_channels_mask[0] = (1 << 0);  // L tweeter.
    metadata.codecs_channels_mask[1] = (1 << 1);  // R tweeter.
    metadata.codecs_channels_mask[2] = (1 << 0);  // Woofer
    metadata.mix_mask = (1 << 1);                 // Mix lane 1's L + R for woofer.
  } else {                                        // Luis
    snprintf(metadata.product_name, sizeof(metadata.product_name), "luis");
    metadata.tdm.number_of_codecs = 1;
    metadata.tdm.codecs[0] = metadata::Codec::Tas58xx;
    metadata.number_of_channels = 2;
    metadata.swaps = 0x10;
    metadata.lanes_enable_mask[0] = 3;
    metadata.codecs_channels_mask[0] = (1 << 0) | (1 << 1);  // Woofer + Tweeter.
  }
  pbus_metadata_t tdm_metadata[] = {
      {
          .type = DEVICE_METADATA_PRIVATE,
          .data_buffer = &metadata,
          .data_size = sizeof(metadata),
      },
  };

  pbus_dev_t tdm_dev = {};
  if (is_sherlock) {
    tdm_dev.name = "sherlock-i2s-audio-out";
  } else {
    tdm_dev.name = "luis-i2s-audio-out";
  }
  tdm_dev.vid = PDEV_VID_AMLOGIC;
  tdm_dev.pid = PDEV_PID_AMLOGIC_T931;
  tdm_dev.did = PDEV_DID_AMLOGIC_TDM;
  tdm_dev.mmio_list = audio_mmios;
  tdm_dev.mmio_count = countof(audio_mmios);
  tdm_dev.bti_list = tdm_btis;
  tdm_dev.bti_count = countof(tdm_btis);
  tdm_dev.metadata_list = tdm_metadata;
  tdm_dev.metadata_count = countof(tdm_metadata);
  if (is_sherlock) {
    status = pbus_.CompositeDeviceAdd(&tdm_dev, sherlock_tdm_i2s_fragments,
                                      countof(sherlock_tdm_i2s_fragments), UINT32_MAX);
  } else {
    status = pbus_.CompositeDeviceAdd(&tdm_dev, luis_tdm_i2s_fragments,
                                      countof(luis_tdm_i2s_fragments), UINT32_MAX);
  }
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: I2S CompositeDeviceAdd failed: %d", __FILE__, status);
    return status;
  }

  // Add PDM IN.
  static constexpr pbus_mmio_t pdm_mmios[] = {
      {.base = T931_EE_PDM_BASE, .length = T931_EE_PDM_LENGTH},
      {.base = T931_EE_AUDIO_BASE, .length = T931_EE_AUDIO_LENGTH},
  };

  static constexpr pbus_bti_t pdm_btis[] = {
      {
          .iommu_index = 0,
          .bti_id = BTI_AUDIO_IN,
      },
  };

  uint8_t channel_count = 2;
  if (!is_sherlock) {
    channel_count = 3;
  }
  pbus_metadata_t pdm_metadata[] = {
      {
          .type = DEVICE_METADATA_PRIVATE,
          .data_buffer = &channel_count,
          .data_size = sizeof(channel_count),
      },
  };
  pbus_dev_t pdm_dev = {};
  if (is_sherlock) {
    pdm_dev.name = "sherlock-pdm-audio-in";
  } else {
    pdm_dev.name = "luis-pdm-audio-in";
  }
  pdm_dev.vid = PDEV_VID_AMLOGIC;
  pdm_dev.pid = PDEV_PID_AMLOGIC_T931;
  pdm_dev.did = PDEV_DID_SHERLOCK_PDM;
  pdm_dev.mmio_list = pdm_mmios;
  pdm_dev.mmio_count = countof(pdm_mmios);
  pdm_dev.bti_list = pdm_btis;
  pdm_dev.bti_count = countof(pdm_btis);
  pdm_dev.metadata_list = pdm_metadata;
  pdm_dev.metadata_count = countof(pdm_metadata);
  status = pbus_.DeviceAdd(&pdm_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s pbus_.DeviceAdd failed %d", __FUNCTION__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace sherlock

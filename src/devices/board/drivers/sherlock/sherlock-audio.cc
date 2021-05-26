// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <string.h>

#include <ddktl/metadata/audio.h>
#include <soc/aml-common/aml-audio.h>
#include <soc/aml-meson/g12b-clk.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>
#include <ti/ti-audio.h>

#include "sherlock-gpios.h"
#include "sherlock.h"

// Enables BT PCM audio.
#define ENABLE_BT

namespace sherlock {

zx_status_t Sherlock::AudioInit() {
  uint8_t tdm_instance_id = 1;
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
  constexpr pbus_irq_t frddr_b_irqs[] = {
      {
          .irq = T931_AUDIO_FRDDR_B,
          .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
      },
  };
  constexpr pbus_irq_t toddr_b_irqs[] = {
      {
          .irq = T931_AUDIO_TODDR_B,
          .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
      },
  };

  pdev_board_info_t board_info = {};
  zx_status_t status = pbus_.GetBoardInfo(&board_info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GetBoardInfo failed %d", __FILE__, status);
    return status;
  }

  bool is_sherlock = board_info.pid == PDEV_PID_SHERLOCK;
  bool is_ernie = board_info.pid != PDEV_PID_SHERLOCK && (board_info.board_revision & (1 << 4));
  if (is_sherlock && board_info.board_revision < BOARD_REV_EVT1) {
    // For audio we don't support boards revision lower than EVT.
    zxlogf(WARNING, "%s: Board revision unsupported, skipping audio initialization.", __FILE__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  const char* product_name = is_sherlock ? "sherlock" : (is_ernie ? "ernie" : "luis");
  constexpr size_t device_name_max_length = 32;

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
  constexpr zx_bind_inst_t ernie_woofer_codec_i2c_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
      BI_ABORT_IF(NE, BIND_I2C_BUS_ID, SHERLOCK_I2C_A0_0),
      BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x4c),
  };
  constexpr zx_bind_inst_t ernie_tweeter_codec_i2c_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
      BI_ABORT_IF(NE, BIND_I2C_BUS_ID, SHERLOCK_I2C_A0_0),
      BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x2d),
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
  constexpr zx_bind_inst_t ernie_codec_woofer_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CODEC),
      BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TI),
      BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_TI_TAS58xx),
      BI_MATCH_IF(EQ, BIND_CODEC_INSTANCE, 1),
  };
  constexpr zx_bind_inst_t ernie_codec_tweeter_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CODEC),
      BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TI),
      BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_TI_TAS58xx),
      BI_MATCH_IF(EQ, BIND_CODEC_INSTANCE, 2),
  };

  const device_fragment_part_t enable_gpio_fragment[] = {
      {countof(enable_gpio_match), enable_gpio_match},
  };

  // I2C matches to be used by the codec fragments below.
  const device_fragment_part_t woofer_i2c_fragment[] = {
      {countof(woofer_i2c_match), woofer_i2c_match},
  };
  const device_fragment_part_t tweeter_left_i2c_fragment[] = {
      {countof(tweeter_left_i2c_match), tweeter_left_i2c_match},
  };
  const device_fragment_part_t tweeter_right_i2c_fragment[] = {
      {countof(tweeter_right_i2c_match), tweeter_right_i2c_match},
  };
  const device_fragment_part_t luis_codec_i2c_fragment[] = {
      {countof(luis_codec_i2c_match), luis_codec_i2c_match},
  };
  const device_fragment_part_t ernie_woofer_codec_i2c_fragment[] = {
      {countof(ernie_woofer_codec_i2c_match), ernie_woofer_codec_i2c_match},
  };
  const device_fragment_part_t ernie_tweeter_codec_i2c_fragment[] = {
      {countof(ernie_tweeter_codec_i2c_match), ernie_tweeter_codec_i2c_match},
  };

  // Fragment to be used by the controller, pointing to the codecs.
  const device_fragment_part_t codec_woofer_fragment[] = {
      {countof(codec_woofer_match), codec_woofer_match},
  };
  const device_fragment_part_t codec_tweeter_left_fragment[] = {
      {countof(codec_tweeter_left_match), codec_tweeter_left_match},
  };
  const device_fragment_part_t codec_tweeter_right_fragment[] = {
      {countof(codec_tweeter_right_match), codec_tweeter_right_match},
  };
  const device_fragment_part_t luis_codec_fragment[] = {
      {countof(luis_codec_match), luis_codec_match},
  };
  const device_fragment_part_t ernie_codec_woofer_fragment[] = {
      {countof(ernie_codec_woofer_match), ernie_codec_woofer_match},
  };
  const device_fragment_part_t ernie_codec_tweeter_fragment[] = {
      {countof(ernie_codec_tweeter_match), ernie_codec_tweeter_match},
  };

  // Fragments to be used by the codecs.
  const device_fragment_t woofer_fragments[] = {
      {"i2c", countof(woofer_i2c_fragment), woofer_i2c_fragment},
  };
  const device_fragment_t tweeter_left_fragments[] = {
      {"i2c", countof(tweeter_left_i2c_fragment), tweeter_left_i2c_fragment},
  };
  const device_fragment_t tweeter_right_fragments[] = {
      {"i2c", countof(tweeter_right_i2c_fragment), tweeter_right_i2c_fragment},
  };
  const device_fragment_t luis_codec_fragments[] = {
      {"i2c", countof(luis_codec_i2c_fragment), luis_codec_i2c_fragment},
  };
  const device_fragment_t ernie_woofer_fragments[] = {
      {"i2c", countof(ernie_woofer_codec_i2c_fragment), ernie_woofer_codec_i2c_fragment},
  };
  const device_fragment_t ernie_tweeter_fragments[] = {
      {"i2c", countof(ernie_tweeter_codec_i2c_fragment), ernie_tweeter_codec_i2c_fragment},
  };

  const device_fragment_t sherlock_tdm_i2s_fragments[] = {
      {"gpio-enable", countof(enable_gpio_fragment), enable_gpio_fragment},
      {"codec-01", countof(codec_woofer_fragment), codec_woofer_fragment},
      {"codec-02", countof(codec_tweeter_left_fragment), codec_tweeter_left_fragment},
      {"codec-03", countof(codec_tweeter_right_fragment), codec_tweeter_right_fragment},
  };
  const device_fragment_t luis_tdm_i2s_fragments[] = {
      {"gpio-enable", countof(enable_gpio_fragment), enable_gpio_fragment},
      {"codec-01", countof(luis_codec_fragment), luis_codec_fragment},
  };
  const device_fragment_t ernie_tdm_i2s_fragments[] = {
      {"gpio-enable", countof(enable_gpio_fragment), enable_gpio_fragment},
      {"codec-01", countof(ernie_codec_woofer_fragment), ernie_codec_woofer_fragment},
      {"codec-02", countof(ernie_codec_tweeter_fragment), ernie_codec_tweeter_fragment},
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

  // TDM pin configuration.
  gpio_impl_.SetAltFunction(T931_GPIOZ(7), T931_GPIOZ_7_TDMC_SCLK_FN);
  gpio_impl_.SetAltFunction(T931_GPIOZ(6), T931_GPIOZ_6_TDMC_FS_FN);
  gpio_impl_.SetAltFunction(T931_GPIOZ(2), T931_GPIOZ_2_TDMC_D0_FN);
  constexpr uint64_t ua = 3000;
  gpio_impl_.SetDriveStrength(T931_GPIOZ(7), ua, nullptr);
  gpio_impl_.SetDriveStrength(T931_GPIOZ(6), ua, nullptr);
  gpio_impl_.SetDriveStrength(T931_GPIOZ(2), ua, nullptr);
  if (is_sherlock) {
    gpio_impl_.SetAltFunction(T931_GPIOZ(3), T931_GPIOZ_3_TDMC_D1_FN);
    gpio_impl_.SetDriveStrength(T931_GPIOZ(3), ua, nullptr);
  } else {
    gpio_impl_.SetAltFunction(T931_GPIOZ(3), 0);
  }

  gpio_impl_.SetAltFunction(T931_GPIOAO(9), T931_GPIOAO_9_MCLK_FN);
  gpio_impl_.SetDriveStrength(T931_GPIOAO(9), ua, nullptr);

#ifdef ENABLE_BT
  // PCM pin assignments.
  gpio_impl_.SetAltFunction(S905D2_GPIOX(8), S905D2_GPIOX_8_TDMA_DIN1_FN);
  gpio_impl_.SetAltFunction(S905D2_GPIOX(9), S905D2_GPIOX_8_TDMA_D0_FN);
  gpio_impl_.SetAltFunction(S905D2_GPIOX(10), S905D2_GPIOX_10_TDMA_FS_FN);
  gpio_impl_.SetAltFunction(S905D2_GPIOX(11), S905D2_GPIOX_11_TDMA_SCLK_FN);
  gpio_impl_.SetDriveStrength(T931_GPIOX(9), ua, nullptr);
  gpio_impl_.SetDriveStrength(T931_GPIOX(10), ua, nullptr);
  gpio_impl_.SetDriveStrength(T931_GPIOX(11), ua, nullptr);
#endif

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
  } else {  // Luis/Ernie.
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

    if (is_ernie) {
      zx_device_prop_t props[] = {{BIND_PLATFORM_DEV_VID, 0, PDEV_VID_TI},
                                  {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_TI_TAS58xx},
                                  {BIND_CODEC_INSTANCE, 0, 1}};
      metadata::ti::TasConfig metadata = {};
      metadata.instance_count = 1;

      const device_metadata_t codec_metadata[] = {
          {
              .type = DEVICE_METADATA_PRIVATE,
              .data = &metadata,
              .length = sizeof(metadata),
          },
      };

      composite_device_desc_t comp_desc = {};
      comp_desc.props = props;
      comp_desc.props_count = countof(props);
      comp_desc.coresident_device_index = UINT32_MAX;
      comp_desc.fragments = ernie_woofer_fragments;
      comp_desc.fragments_count = countof(ernie_woofer_fragments);
      comp_desc.metadata_list = codec_metadata;
      comp_desc.metadata_count = countof(codec_metadata);
      status = DdkAddComposite("audio-tas58xx-woofer", &comp_desc);
      if (status != ZX_OK) {
        zxlogf(ERROR, "%s DdkAddComposite woofer failed %d", __FILE__, status);
        return status;
      }

      metadata.instance_count = 2;
      props[2].value = 2;
      comp_desc.fragments = ernie_tweeter_fragments;
      comp_desc.fragments_count = countof(ernie_tweeter_fragments);
      status = DdkAddComposite("audio-tas58xx-tweeter", &comp_desc);
      if (status != ZX_OK) {
        zxlogf(ERROR, "%s DdkAddComposite left tweeter failed %d", __FILE__, status);
        return status;
      }
    } else {
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
  }
  metadata::AmlConfig metadata = {};
  snprintf(metadata.manufacturer, sizeof(metadata.manufacturer), "Spacely Sprockets");
  strncpy(metadata.product_name, product_name, sizeof(metadata.product_name));

  metadata.is_input = false;
  // Compatible clocks with other TDM drivers.
  metadata.mClockDivFactor = 10;
  metadata.sClockDivFactor = 25;
  metadata.unique_id = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS;
  metadata.bus = metadata::AmlBus::TDM_C;
  metadata.version = metadata::AmlVersion::kS905D2G;  // Also works with T931G.
  if (is_sherlock) {
    metadata.dai.type = metadata::DaiType::I2s;
    metadata.codecs.number_of_codecs = 3;
    metadata.codecs.types[0] = metadata::CodecType::Tas5720;
    metadata.codecs.types[1] = metadata::CodecType::Tas5720;
    metadata.codecs.types[2] = metadata::CodecType::Tas5720;
    metadata.ring_buffer.number_of_channels = 4;
    metadata.swaps = 0x1032;
    metadata.lanes_enable_mask[0] = 3;
    metadata.lanes_enable_mask[1] = 3;
#ifndef FACTORY_BUILD
    // Boost the woofer above tweeters by 7.1db analog and 5.5db digital needed for this product.
    // Only applicable in non-factory environments.
    metadata.codecs.delta_gains[0] = 0.f;
    metadata.codecs.delta_gains[1] = -12.6f;
    metadata.codecs.delta_gains[2] = -12.6f;
#endif                                                      // FACTORY_BUILD
    metadata.codecs.channels_to_use_bitmask[0] = (1 << 0);  // Woofer.
    metadata.codecs.channels_to_use_bitmask[1] = (1 << 1);  // L tweeter.
    metadata.codecs.channels_to_use_bitmask[2] = (1 << 0);  // R tweeter.
    metadata.mix_mask = (1 << 1);                           // Mix lane 1's L + R for woofer.
  } else if (is_ernie) {
    metadata.dai.type = metadata::DaiType::Tdm1;
    metadata.codecs.number_of_codecs = 2;
    metadata.codecs.types[0] = metadata::CodecType::Tas58xx;
    metadata.codecs.types[1] = metadata::CodecType::Tas58xx;
    metadata.dai.bits_per_sample = 16;
    metadata.dai.bits_per_slot = 16;
    metadata.ring_buffer.number_of_channels = 4;
    metadata.dai.number_of_channels = 4;
    metadata.swaps = 0x10;
    metadata.lanes_enable_mask[0] = 0xf;
    metadata.codecs.channels_to_use_bitmask[0] =
        (1 << 0) | (1 << 1);  // First 2 channels in a shared TDM.
    metadata.codecs.channels_to_use_bitmask[1] =
        (1 << 2) | (1 << 3);  // Second 2 channels in a shared TDM.
  } else {                    // Luis
    metadata.dai.type = metadata::DaiType::I2s;
    metadata.codecs.number_of_codecs = 1;
    metadata.codecs.types[0] = metadata::CodecType::Tas58xx;
    metadata.ring_buffer.number_of_channels = 2;
    metadata.swaps = 0x10;
    metadata.lanes_enable_mask[0] = 3;
    metadata.codecs.channels_to_use_bitmask[0] = (1 << 0) | (1 << 1);  // Woofer + Tweeter.
  }
  pbus_metadata_t tdm_metadata[] = {
      {
          .type = DEVICE_METADATA_PRIVATE,
          .data_buffer = reinterpret_cast<uint8_t*>(&metadata),
          .data_size = sizeof(metadata),
      },
  };

  pbus_dev_t tdm_dev = {};
  char name[device_name_max_length];
  snprintf(name, sizeof(name), "%s-i2s-audio-out", product_name);
  tdm_dev.name = name;
  tdm_dev.vid = PDEV_VID_AMLOGIC;
  tdm_dev.pid = PDEV_PID_AMLOGIC_T931;
  tdm_dev.did = PDEV_DID_AMLOGIC_TDM;
  tdm_dev.instance_id = tdm_instance_id++;
  tdm_dev.mmio_list = audio_mmios;
  tdm_dev.mmio_count = countof(audio_mmios);
  tdm_dev.bti_list = tdm_btis;
  tdm_dev.bti_count = countof(tdm_btis);
  tdm_dev.irq_list = frddr_b_irqs;
  tdm_dev.irq_count = countof(frddr_b_irqs);
  tdm_dev.metadata_list = tdm_metadata;
  tdm_dev.metadata_count = countof(tdm_metadata);
  if (is_sherlock) {
    status =
        pbus_.CompositeDeviceAdd(&tdm_dev, reinterpret_cast<uint64_t>(sherlock_tdm_i2s_fragments),
                                 countof(sherlock_tdm_i2s_fragments), UINT32_MAX);
  } else {
    if (is_ernie) {
      status =
          pbus_.CompositeDeviceAdd(&tdm_dev, reinterpret_cast<uint64_t>(ernie_tdm_i2s_fragments),
                                   countof(ernie_tdm_i2s_fragments), UINT32_MAX);
    } else {
      status =
          pbus_.CompositeDeviceAdd(&tdm_dev, reinterpret_cast<uint64_t>(luis_tdm_i2s_fragments),
                                   countof(luis_tdm_i2s_fragments), UINT32_MAX);
    }
  }
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: I2S CompositeDeviceAdd failed: %d", __FILE__, status);
    return status;
  }

#ifdef ENABLE_BT
  // Add TDM OUT for BT.
  {
    const pbus_bti_t pcm_out_btis[] = {
        {
            .iommu_index = 0,
            .bti_id = BTI_AUDIO_BT_OUT,
        },
    };
    metadata::AmlConfig metadata = {};
    snprintf(metadata.manufacturer, sizeof(metadata.manufacturer), "Spacely Sprockets");
    strncpy(metadata.product_name, product_name, sizeof(metadata.product_name));

    metadata.is_input = false;
    // Compatible clocks with other TDM drivers.
    metadata.mClockDivFactor = 10;
    metadata.sClockDivFactor = 25;
    metadata.unique_id = AUDIO_STREAM_UNIQUE_ID_BUILTIN_BT;
    metadata.bus = metadata::AmlBus::TDM_A;
    metadata.version = metadata::AmlVersion::kS905D2G;
    metadata.dai.type = metadata::DaiType::Tdm1;
    metadata.dai.sclk_on_raising = true;
    metadata.dai.bits_per_sample = 16;
    metadata.dai.bits_per_slot = 16;
    metadata.ring_buffer.number_of_channels = 1;
    metadata.dai.number_of_channels = 1;
    metadata.lanes_enable_mask[0] = 1;
    pbus_metadata_t tdm_metadata[] = {
        {
            .type = DEVICE_METADATA_PRIVATE,
            .data_buffer = reinterpret_cast<uint8_t*>(&metadata),
            .data_size = sizeof(metadata),
        },
    };

    pbus_dev_t tdm_dev = {};
    char tdm_name[device_name_max_length];
    snprintf(tdm_name, sizeof(tdm_name), "%s-pcm-dai-out", product_name);
    tdm_dev.name = tdm_name;
    tdm_dev.vid = PDEV_VID_AMLOGIC;
    tdm_dev.pid = PDEV_PID_AMLOGIC_T931;
    tdm_dev.did = PDEV_DID_AMLOGIC_DAI_OUT;
    tdm_dev.instance_id = tdm_instance_id++;
    tdm_dev.mmio_list = audio_mmios;
    tdm_dev.mmio_count = countof(audio_mmios);
    tdm_dev.bti_list = pcm_out_btis;
    tdm_dev.bti_count = countof(pcm_out_btis);
    tdm_dev.metadata_list = tdm_metadata;
    tdm_dev.metadata_count = countof(tdm_metadata);
    status = pbus_.DeviceAdd(&tdm_dev);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: PCM CompositeDeviceAdd failed: %d", __FILE__, status);
      return status;
    }
  }
#endif

  // Input device.
  {
    metadata::AmlPdmConfig metadata = {};
    snprintf(metadata.manufacturer, sizeof(metadata.manufacturer), "Spacely Sprockets");
    snprintf(metadata.product_name, sizeof(metadata.product_name), "sherlock");
    metadata.number_of_channels = is_sherlock ? 2 : 3;
    metadata.version = metadata::AmlVersion::kS905D2G;
    metadata.sysClockDivFactor = 4;
    metadata.dClockDivFactor = 250;
    pbus_metadata_t pdm_metadata[] = {
        {
            .type = DEVICE_METADATA_PRIVATE,
            .data_buffer = reinterpret_cast<uint8_t*>(&metadata),
            .data_size = sizeof(metadata),
        },
    };

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

    pbus_dev_t dev_in = {};
    char pdm_name[device_name_max_length];
    snprintf(pdm_name, sizeof(pdm_name), "%s-pdm-audio-in", product_name);
    dev_in.name = pdm_name;
    dev_in.vid = PDEV_VID_AMLOGIC;
    dev_in.pid = PDEV_PID_AMLOGIC_T931;
    dev_in.did = PDEV_DID_AMLOGIC_PDM;
    dev_in.mmio_list = pdm_mmios;
    dev_in.mmio_count = countof(pdm_mmios);
    dev_in.bti_list = pdm_btis;
    dev_in.bti_count = countof(pdm_btis);
    dev_in.irq_list = toddr_b_irqs;
    dev_in.irq_count = countof(toddr_b_irqs);
    dev_in.metadata_list = pdm_metadata;
    dev_in.metadata_count = countof(pdm_metadata);

    status = pbus_.DeviceAdd(&dev_in);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s adding audio input device failed %d", __FILE__, status);
      return status;
    }
  }

#ifdef ENABLE_BT
  // Add TDM IN for BT.
  {
    const pbus_bti_t pcm_in_btis[] = {
        {
            .iommu_index = 0,
            .bti_id = BTI_AUDIO_BT_IN,
        },
    };
    metadata::AmlConfig metadata = {};
    snprintf(metadata.manufacturer, sizeof(metadata.manufacturer), "Spacely Sprockets");
    strncpy(metadata.product_name, product_name, sizeof(metadata.product_name));

    metadata.is_input = true;
    // Compatible clocks with other TDM drivers.
    metadata.mClockDivFactor = 10;
    metadata.sClockDivFactor = 25;
    metadata.unique_id = AUDIO_STREAM_UNIQUE_ID_BUILTIN_BT;
    metadata.bus = metadata::AmlBus::TDM_A;
    metadata.version = metadata::AmlVersion::kS905D2G;
    metadata.dai.type = metadata::DaiType::Tdm1;
    metadata.dai.sclk_on_raising = true;
    metadata.dai.bits_per_sample = 16;
    metadata.dai.bits_per_slot = 16;
    metadata.ring_buffer.number_of_channels = 1;
    metadata.dai.number_of_channels = 1;
    metadata.swaps = 0x0200;
    metadata.lanes_enable_mask[1] = 1;
    pbus_metadata_t tdm_metadata[] = {
        {
            .type = DEVICE_METADATA_PRIVATE,
            .data_buffer = reinterpret_cast<uint8_t*>(&metadata),
            .data_size = sizeof(metadata),
        },
    };
    pbus_dev_t tdm_dev = {};
    char name[device_name_max_length];
    snprintf(name, sizeof(name), "%s-pcm-dai-in", product_name);
    tdm_dev.name = name;
    tdm_dev.vid = PDEV_VID_AMLOGIC;
    tdm_dev.pid = PDEV_PID_AMLOGIC_T931;
    tdm_dev.did = PDEV_DID_AMLOGIC_DAI_IN;
    tdm_dev.instance_id = tdm_instance_id++;
    tdm_dev.mmio_list = audio_mmios;
    tdm_dev.mmio_count = countof(audio_mmios);
    tdm_dev.bti_list = pcm_in_btis;
    tdm_dev.bti_count = countof(pcm_in_btis);
    tdm_dev.metadata_list = tdm_metadata;
    tdm_dev.metadata_count = countof(tdm_metadata);
    status = pbus_.DeviceAdd(&tdm_dev);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: PCM CompositeDeviceAdd failed: %d", __FILE__, status);
      return status;
    }
  }
#endif
  return ZX_OK;
}

}  // namespace sherlock

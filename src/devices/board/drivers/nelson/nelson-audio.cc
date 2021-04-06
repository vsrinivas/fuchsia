// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddktl/metadata/audio.h>
#include <soc/aml-common/aml-audio.h>
#include <soc/aml-meson/sm1-clk.h>
#include <soc/aml-s905d3/s905d3-gpio.h>
#include <soc/aml-s905d3/s905d3-hw.h>
#include <ti/ti-audio.h>

#include "nelson-gpios.h"
#include "nelson.h"

#ifdef TAS5805M_CONFIG_PATH
#include TAS5805M_CONFIG_PATH
#endif

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
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TI_TAS58xx),  // For Nelson P2.
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

static const device_fragment_t ref_codec_fragments[] = {
    {"i2c", countof(ref_out_i2c_fragment), ref_out_i2c_fragment},
    {"gpio-enable", countof(ref_out_enable_gpio_fragment), ref_out_enable_gpio_fragment},
    {"gpio-fault", countof(ref_out_fault_gpio_fragment), ref_out_fault_gpio_fragment},
};
static const device_fragment_t p2_codec_fragments[] = {
    {"i2c", countof(p2_out_i2c_fragment), p2_out_i2c_fragment},
};
static const device_fragment_t ref_controller_fragments[] = {
    {"gpio-enable", countof(ref_out_enable_gpio_fragment), ref_out_enable_gpio_fragment},
    {"codec-01", countof(ref_out_codec_fragment), ref_out_codec_fragment},
};
static const device_fragment_t p2_controller_fragments[] = {
    {"gpio-enable", countof(ref_out_enable_gpio_fragment), ref_out_enable_gpio_fragment},
    {"codec-01", countof(p2_out_codec_fragment), p2_out_codec_fragment},
};

zx_status_t Nelson::AudioInit() {
  zx_status_t status;

  status = clk_impl_.Disable(sm1_clk::CLK_HIFI_PLL);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Disable(CLK_HIFI_PLL) failed, st = %d", __func__, status);
    return status;
  }

  status = clk_impl_.SetRate(sm1_clk::CLK_HIFI_PLL, 768000000);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: SetRate(CLK_HIFI_PLL) failed, st = %d", __func__, status);
    return status;
  }

  status = clk_impl_.Enable(sm1_clk::CLK_HIFI_PLL);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Enable(CLK_HIFI_PLL) failed, st = %d", __func__, status);
    return status;
  }

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

  constexpr pbus_irq_t frddr_b_irqs[] = {
      {
          .irq = S905D3_AUDIO_FRDDR_B,
          .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
      },
  };
  constexpr pbus_irq_t toddr_b_irqs[] = {
      {
          .irq = S905D3_AUDIO_TODDR_B,
          .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
      },
  };

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

  // TDM pin assignments.
  gpio_impl_.SetAltFunction(S905D3_GPIOA(1), S905D3_GPIOA_1_TDMB_SCLK_FN);
  gpio_impl_.SetAltFunction(S905D3_GPIOA(2), S905D3_GPIOA_2_TDMB_FS_FN);
  gpio_impl_.SetAltFunction(S905D3_GPIOA(3), S905D3_GPIOA_3_TDMB_D0_FN);
  gpio_impl_.SetAltFunction(S905D3_GPIOA(6), S905D3_GPIOA_6_TDMB_DIN3_FN);
  constexpr uint64_t ua = 2500;
  gpio_impl_.SetDriveStrength(S905D3_GPIOA(1), ua, nullptr);
  gpio_impl_.SetDriveStrength(S905D3_GPIOA(2), ua, nullptr);
  gpio_impl_.SetDriveStrength(S905D3_GPIOA(3), ua, nullptr);

  // PDM pin assignments
  gpio_impl_.SetAltFunction(S905D3_GPIOA(7), S905D3_GPIOA_7_PDM_DCLK_FN);
  gpio_impl_.SetAltFunction(S905D3_GPIOA(8), S905D3_GPIOA_8_PDM_DIN0_FN);  // First 2 MICs.
  gpio_impl_.SetAltFunction(S905D3_GPIOA(9), S905D3_GPIOA_9_PDM_DIN1_FN);  // Third MIC.

  // Board info.
  pdev_board_info_t board_info = {};
  status = pbus_.GetBoardInfo(&board_info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GetBoardInfo failed %d", __FILE__, status);
    return status;
  }

  // Output devices.
  metadata::AmlConfig metadata = {};
  snprintf(metadata.manufacturer, sizeof(metadata.manufacturer), "Spacely Sprockets");
  snprintf(metadata.product_name, sizeof(metadata.product_name), "nelson");
  metadata.is_input = false;
  metadata.mClockDivFactor = 10;
  metadata.sClockDivFactor = 25;
  metadata.unique_id = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS;
  metadata.bus = metadata::AmlBus::TDM_B;
  metadata.version = metadata::AmlVersion::kS905D3G;
  metadata.dai.type = metadata::DaiType::I2s;
  metadata.ring_buffer.number_of_channels = 1;

  // Get the one ring buffer channel to output in the *left* I2S slot.
  metadata.swaps = 0x10;
  metadata.lanes_enable_mask[0] = 2;

  metadata.codecs.number_of_codecs = 1;
  metadata.codecs.types[0] = metadata::CodecType::Tas58xx;
  metadata.codecs.channels_to_use_bitmask[0] = 3;
  pbus_metadata_t tdm_metadata[] = {
      {
          .type = DEVICE_METADATA_PRIVATE,
          .data_buffer = reinterpret_cast<uint8_t*>(&metadata),
          .data_size = sizeof(metadata),
      },
  };

  pbus_dev_t controller_out = {};
  controller_out.name = "nelson-audio-i2s-out";
  controller_out.vid = PDEV_VID_AMLOGIC;
  controller_out.pid = PDEV_PID_AMLOGIC_S905D3;
  controller_out.did = PDEV_DID_AMLOGIC_TDM;
  controller_out.mmio_list = mmios_out;
  controller_out.mmio_count = countof(mmios_out);
  controller_out.bti_list = btis_out;
  controller_out.bti_count = countof(btis_out);
  controller_out.irq_list = frddr_b_irqs;
  controller_out.irq_count = countof(frddr_b_irqs);
  controller_out.metadata_list = tdm_metadata;
  controller_out.metadata_count = countof(tdm_metadata);

  if (board_info.board_revision < BOARD_REV_P2) {
    // CODEC pin assignments.
    gpio_impl_.SetAltFunction(S905D3_GPIOA(5), 0);  // GPIO
    gpio_impl_.ConfigOut(S905D3_GPIOA(5), 0);

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
    status = pbus_.CompositeDeviceAdd(&controller_out,
                                      reinterpret_cast<uint64_t>(ref_controller_fragments),
                                      countof(ref_controller_fragments), UINT32_MAX);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s adding audio controller out device failed %d", __FILE__, status);
      return status;
    }
  } else {
    // CODEC pin assignments.
    gpio_impl_.SetAltFunction(S905D3_GPIOA(0), 0);  // BOOST_EN_SOC as GPIO.
    gpio_impl_.ConfigOut(S905D3_GPIOA(0), 1);       // BOOST_EN_SOC to high.
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
    gpio_impl_.ConfigOut(S905D3_GPIOA(5), 1);  // Set PDN_N to high.
    zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));
    // I2S clocks are configured by the controller and the rest of the initialization is done
    // in the codec itself.

    constexpr zx_device_prop_t props[] = {{BIND_PLATFORM_DEV_VID, 0, PDEV_VID_TI},
                                          {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_TI_TAS58xx}};
    metadata::ti::TasConfig metadata = {};
    metadata.bridged = true;
#ifdef TAS5805M_CONFIG_PATH
    metadata.number_of_writes1 = sizeof(tas5805m_init_sequence1) / sizeof(cfg_reg);
    for (size_t i = 0; i < metadata.number_of_writes1; ++i) {
      metadata.init_sequence1[i].address = tas5805m_init_sequence1[i].offset;
      metadata.init_sequence1[i].value = tas5805m_init_sequence1[i].value;
    }
    metadata.number_of_writes2 = sizeof(tas5805m_init_sequence2) / sizeof(cfg_reg);
    for (size_t i = 0; i < metadata.number_of_writes2; ++i) {
      metadata.init_sequence2[i].address = tas5805m_init_sequence2[i].offset;
      metadata.init_sequence2[i].value = tas5805m_init_sequence2[i].value;
    }
#endif
    const device_metadata_t codec_metadata[] = {
        {
            .type = DEVICE_METADATA_PRIVATE,
            .data = reinterpret_cast<uint8_t*>(&metadata),
            .length = sizeof(metadata),
        },
    };
    composite_device_desc_t codec_desc = {};
    codec_desc.props = props;
    codec_desc.props_count = countof(props);
    codec_desc.coresident_device_index = UINT32_MAX;
    codec_desc.fragments = p2_codec_fragments;
    codec_desc.fragments_count = countof(p2_codec_fragments);
    codec_desc.metadata_list = codec_metadata;
    codec_desc.metadata_count = countof(codec_metadata);
    status = DdkAddComposite("audio-tas58xx", &codec_desc);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s DdkAddComposite failed %d", __FILE__, status);
      return status;
    }
    status = pbus_.CompositeDeviceAdd(&controller_out,
                                      reinterpret_cast<uint64_t>(p2_controller_fragments),
                                      countof(p2_controller_fragments), UINT32_MAX);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s adding audio controller out device failed %d", __FILE__, status);
      return status;
    }
  }

  // Input device.
  {
    metadata::AmlPdmConfig metadata = {};
    snprintf(metadata.manufacturer, sizeof(metadata.manufacturer), "Spacely Sprockets");
    snprintf(metadata.product_name, sizeof(metadata.product_name), "nelson");
    metadata.number_of_channels = 3;
    metadata.version = metadata::AmlVersion::kS905D3G;
    metadata.sysClockDivFactor = 4;
    metadata.dClockDivFactor = 250;
    pbus_metadata_t pdm_metadata[] = {
        {
            .type = DEVICE_METADATA_PRIVATE,
            .data_buffer = reinterpret_cast<uint8_t*>(&metadata),
            .data_size = sizeof(metadata),
        },
    };

    pbus_dev_t dev_in = {};
    dev_in.name = "nelson-audio-pdm-in";
    dev_in.vid = PDEV_VID_AMLOGIC;
    dev_in.pid = PDEV_PID_AMLOGIC_S905D3;
    dev_in.did = PDEV_DID_AMLOGIC_PDM;
    dev_in.mmio_list = mmios_in;
    dev_in.mmio_count = countof(mmios_in);
    dev_in.bti_list = btis_in;
    dev_in.bti_count = countof(btis_in);
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
  return ZX_OK;
}

}  // namespace nelson

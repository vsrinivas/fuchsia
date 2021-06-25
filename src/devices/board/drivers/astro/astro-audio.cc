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
#include <soc/aml-meson/g12a-clk.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <ti/ti-audio.h>

#include "astro-gpios.h"
#include "astro.h"

// Enables BT PCM audio.
#define ENABLE_BT
// Enable DAI mode for BT PCM audio.
#define ENABLE_DAI_MODE
// Enable DAI test.
//#define ENABLE_DAI_TEST

#ifdef TAS2770_CONFIG_PATH
#include TAS2770_CONFIG_PATH
#endif

namespace astro {

constexpr uint32_t kCodecVid = PDEV_VID_TI;
constexpr uint32_t kCodecDid = PDEV_DID_TI_TAS2770;

static const pbus_mmio_t audio_mmios[] = {
    {.base = S905D2_EE_AUDIO_BASE, .length = S905D2_EE_AUDIO_LENGTH},
};

constexpr pbus_irq_t frddr_b_irqs[] = {
    {
        .irq = S905D2_AUDIO_FRDDR_B,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};
constexpr pbus_irq_t toddr_b_irqs[] = {
    {
        .irq = S905D2_AUDIO_TODDR_B,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

#ifdef ENABLE_BT
#ifndef ENABLE_DAI_MODE
constexpr pbus_irq_t frddr_a_irqs[] = {
    {
        .irq = S905D2_AUDIO_FRDDR_A,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};
constexpr pbus_irq_t toddr_a_irqs[] = {
    {
        .irq = S905D2_AUDIO_TODDR_A,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};
#endif
#endif

static const pbus_bti_t tdm_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_AUDIO_OUT,
    },
};

static const zx_bind_inst_t i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, ASTRO_I2C_3),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, I2C_AUDIO_CODEC_ADDR),
};

static const zx_bind_inst_t fault_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_AUDIO_SOC_FAULT_L),
};

static const zx_bind_inst_t enable_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_SOC_AUDIO_EN),
};

static const zx_bind_inst_t codec_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CODEC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, kCodecVid),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, kCodecDid),
};
#ifdef ENABLE_BT
#ifdef ENABLE_DAI_MODE
#ifdef ENABLE_DAI_TEST
static const zx_bind_inst_t dai_out_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_DAI),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D2),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_DAI_OUT),
};
static const zx_bind_inst_t dai_in_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_DAI),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D2),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_DAI_IN),
};
#endif
#endif
#endif
static const device_fragment_part_t i2c_fragment[] = {
    {countof(i2c_match), i2c_match},
};

static const device_fragment_part_t fault_gpio_fragment[] = {
    {countof(fault_gpio_match), fault_gpio_match},
};

static const device_fragment_part_t enable_gpio_fragment[] = {
    {countof(enable_gpio_match), enable_gpio_match},
};
static const device_fragment_part_t codec_fragment[] = {
    {countof(codec_match), codec_match},
};
#ifdef ENABLE_BT
#ifdef ENABLE_DAI_MODE
#ifdef ENABLE_DAI_TEST
static const device_fragment_part_t dai_out_fragment[] = {
    {countof(dai_out_match), dai_out_match},
};
static const device_fragment_part_t dai_in_fragment[] = {
    {countof(dai_in_match), dai_in_match},
};
static const device_fragment_t dai_test_out_fragments[] = {
    {"dai-out", countof(dai_out_fragment), dai_out_fragment},
};
static const device_fragment_t dai_test_in_fragments[] = {
    {"dai-in", countof(dai_in_fragment), dai_in_fragment},
};
#endif
#else
static const device_fragment_t tdm_pcm_fragments[] = {};
#endif
#endif
static const device_fragment_t tdm_i2s_fragments[] = {
    {"gpio-enable", countof(enable_gpio_fragment), enable_gpio_fragment},
    {"codec-01", countof(codec_fragment), codec_fragment},
};
static const device_fragment_t codec_fragments[] = {
    {"i2c", countof(i2c_fragment), i2c_fragment},
    {"gpio", countof(fault_gpio_fragment), fault_gpio_fragment},
};

zx_status_t Astro::AudioInit() {
  zx_status_t status;
  uint8_t tdm_instance_id = 1;

  status = clk_impl_.Disable(g12a_clk::CLK_HIFI_PLL);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Disable(CLK_HIFI_PLL) failed, st = %d", __func__, status);
    return status;
  }

  status = clk_impl_.SetRate(g12a_clk::CLK_HIFI_PLL, 768000000);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: SetRate(CLK_HIFI_PLL) failed, st = %d", __func__, status);
    return status;
  }

  status = clk_impl_.Enable(g12a_clk::CLK_HIFI_PLL);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Enable(CLK_HIFI_PLL) failed, st = %d", __func__, status);
    return status;
  }

  // TDM pin assignments
  gpio_impl_.SetAltFunction(S905D2_GPIOA(1), S905D2_GPIOA_1_TDMB_SCLK_FN);
  gpio_impl_.SetAltFunction(S905D2_GPIOA(2), S905D2_GPIOA_2_TDMB_FS_FN);
  gpio_impl_.SetAltFunction(S905D2_GPIOA(3), S905D2_GPIOA_3_TDMB_D0_FN);
  gpio_impl_.SetAltFunction(S905D2_GPIOA(6), S905D2_GPIOA_6_TDMB_DIN3_FN);
  constexpr uint64_t ua = 3000;
  gpio_impl_.SetDriveStrength(S905D2_GPIOA(1), ua, nullptr);
  gpio_impl_.SetDriveStrength(S905D2_GPIOA(2), ua, nullptr);
  gpio_impl_.SetDriveStrength(S905D2_GPIOA(3), ua, nullptr);

#ifdef ENABLE_BT
  // PCM pin assignments.
  gpio_impl_.SetAltFunction(S905D2_GPIOX(8), S905D2_GPIOX_8_TDMA_DIN1_FN);
  gpio_impl_.SetAltFunction(S905D2_GPIOX(9), S905D2_GPIOX_8_TDMA_D0_FN);
  gpio_impl_.SetAltFunction(S905D2_GPIOX(10), S905D2_GPIOX_10_TDMA_FS_FN);
  gpio_impl_.SetAltFunction(S905D2_GPIOX(11), S905D2_GPIOX_11_TDMA_SCLK_FN);
  gpio_impl_.SetDriveStrength(S905D2_GPIOX(9), ua, nullptr);
  gpio_impl_.SetDriveStrength(S905D2_GPIOX(10), ua, nullptr);
  gpio_impl_.SetDriveStrength(S905D2_GPIOX(11), ua, nullptr);
#endif

  // PDM pin assignments
  gpio_impl_.SetAltFunction(S905D2_GPIOA(7), S905D2_GPIOA_7_PDM_DCLK_FN);
  gpio_impl_.SetAltFunction(S905D2_GPIOA(8), S905D2_GPIOA_8_PDM_DIN0_FN);

  // Hardware Reset of the codec.
  gpio_impl_.ConfigOut(S905D2_GPIOA(5), 0);
  zx::nanosleep(zx::deadline_after(zx::msec(1)));
  gpio_impl_.ConfigOut(S905D2_GPIOA(5), 1);

  // Output devices.
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
    snprintf(metadata.product_name, sizeof(metadata.product_name), "astro");
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

    // Add DAI or controller driver depending on ENABLE_DAI_MODE.
    pbus_dev_t tdm_dev = {};
    tdm_dev.vid = PDEV_VID_AMLOGIC;
    tdm_dev.pid = PDEV_PID_AMLOGIC_S905D2;
    tdm_dev.mmio_list = audio_mmios;
    tdm_dev.mmio_count = countof(audio_mmios);
    tdm_dev.bti_list = pcm_out_btis;
    tdm_dev.bti_count = countof(pcm_out_btis);
    tdm_dev.metadata_list = tdm_metadata;
    tdm_dev.metadata_count = countof(tdm_metadata);
#ifdef ENABLE_DAI_MODE
    tdm_dev.name = "astro-pcm-dai-out";
    tdm_dev.did = PDEV_DID_AMLOGIC_DAI_OUT;
    status = pbus_.DeviceAdd(&tdm_dev);
#else
    tdm_dev.name = "astro-pcm-audio-out";
    tdm_dev.did = PDEV_DID_AMLOGIC_TDM;
    tdm_dev.instance_id = tdm_instance_id++;
    tdm_dev.irq_list = frddr_a_irqs;
    tdm_dev.irq_count = countof(frddr_a_irqs);
    status = pbus_.CompositeDeviceAdd(&tdm_dev, reinterpret_cast<uint64_t>(tdm_pcm_fragments),
                                      countof(tdm_pcm_fragments), UINT32_MAX);
#endif
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: Add DAI/controller driver failed: %d", __FILE__, status);
      return status;
    }

#ifdef ENABLE_DAI_MODE
#ifdef ENABLE_DAI_TEST
    // Add test driver.
    bool is_input = false;
    const device_metadata_t test_metadata[] = {
        {
            .type = DEVICE_METADATA_PRIVATE,
            .data = &is_input,
            .length = sizeof(is_input),
        },
    };
    zx_device_prop_t props[] = {{BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
                                {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_DAI_TEST}};
    composite_device_desc_t comp_desc = {};
    comp_desc.props = props;
    comp_desc.props_count = countof(props);
    comp_desc.coresident_device_index = UINT32_MAX;
    comp_desc.fragments = dai_test_out_fragments;
    comp_desc.fragments_count = countof(dai_test_out_fragments);
    comp_desc.metadata_list = test_metadata;
    comp_desc.metadata_count = countof(test_metadata);
    status = DdkAddComposite("astro-dai-test-out", &comp_desc);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: PCM CompositeDeviceAdd failed: %d", __FILE__, status);
      return status;
    }
#endif
#endif
  }
#endif
  // Add TDM OUT to the codec.
  {
    zx_device_prop_t props[] = {{BIND_PLATFORM_DEV_VID, 0, kCodecVid},
                                {BIND_PLATFORM_DEV_DID, 0, kCodecDid}};

    metadata::ti::TasConfig metadata = {};
#ifdef TAS2770_CONFIG_PATH
    metadata.number_of_writes1 = sizeof(tas2770_init_sequence1) / sizeof(cfg_reg);
    for (size_t i = 0; i < metadata.number_of_writes1; ++i) {
      metadata.init_sequence1[i].address = tas2770_init_sequence1[i].offset;
      metadata.init_sequence1[i].value = tas2770_init_sequence1[i].value;
    }
    metadata.number_of_writes2 = sizeof(tas2770_init_sequence2) / sizeof(cfg_reg);
    for (size_t i = 0; i < metadata.number_of_writes2; ++i) {
      metadata.init_sequence2[i].address = tas2770_init_sequence2[i].offset;
      metadata.init_sequence2[i].value = tas2770_init_sequence2[i].value;
    }
#endif
    const device_metadata_t codec_metadata[] = {
        {
            .type = DEVICE_METADATA_PRIVATE,
            .data = reinterpret_cast<uint8_t*>(&metadata),
            .length = sizeof(metadata),
        },
    };

    composite_device_desc_t comp_desc = {};
    comp_desc.props = props;
    comp_desc.props_count = countof(props);
    comp_desc.coresident_device_index = UINT32_MAX;
    comp_desc.fragments = codec_fragments;
    comp_desc.fragments_count = countof(codec_fragments);
    comp_desc.metadata_list = codec_metadata;
    comp_desc.metadata_count = countof(codec_metadata);
    status = DdkAddComposite("audio-codec-tas27xx", &comp_desc);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s DdkAddComposite failed %d", __FILE__, status);
      return status;
    }
  }
  {
    metadata::AmlConfig metadata = {};
    snprintf(metadata.manufacturer, sizeof(metadata.manufacturer), "Spacely Sprockets");
    snprintf(metadata.product_name, sizeof(metadata.product_name), "astro");
    metadata.is_input = false;
    // Compatible clocks with other TDM drivers.
    metadata.mClockDivFactor = 10;
    metadata.sClockDivFactor = 25;
    metadata.unique_id = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS;
    metadata.bus = metadata::AmlBus::TDM_B;
    metadata.version = metadata::AmlVersion::kS905D2G;
    metadata.dai.type = metadata::DaiType::I2s;

    // We expose a mono ring buffer to clients. However we still use a 2 channels DAI to the codec
    // so we configure the audio engine to only take the one channel and put it in the right slot
    // going out to the codec via I2S.
    metadata.ring_buffer.number_of_channels = 1;
    metadata.lanes_enable_mask[0] = 1;  // One ring buffer channel goes into the right I2S slot.
    metadata.codecs.number_of_codecs = 1;
    metadata.codecs.channels_to_use_bitmask[0] = 1;  // Codec must use the right I2S slot.

    metadata.codecs.types[0] = metadata::CodecType::Tas27xx;
    // Report our external delay based on the chosen frame rate.  Note that these
    // delays were measured on Astro hardware, and should be pretty good, but they
    // will not be perfect.  One reason for this is that we are not taking any
    // steps to align our start time with start of a TDM frame, which will cause
    // up to 1 frame worth of startup error ever time that the output starts.
    // Also note that this is really nothing to worry about.  Hitting our target
    // to within 20.8uSec (for 48k) is pretty good.
    metadata.codecs.number_of_external_delays = 2;
    metadata.codecs.external_delays[0].frequency = 48'000;
    metadata.codecs.external_delays[0].nsecs = ZX_USEC(125);
    metadata.codecs.external_delays[1].frequency = 96'000;
    metadata.codecs.external_delays[1].nsecs = ZX_NSEC(83333);
    metadata.codecs.channels_to_use_bitmask[0] = 0x1;              // Single DAI right I2S channel.
    metadata.codecs.ring_buffer_channels_to_use_bitmask[0] = 0x1;  // Single speaker uses index 0.
    metadata.codecs.delta_gains[0] = -1.5f;
    pbus_metadata_t tdm_metadata[] = {
        {
            .type = DEVICE_METADATA_PRIVATE,
            .data_buffer = reinterpret_cast<uint8_t*>(&metadata),
            .data_size = sizeof(metadata),
        },
    };

    pbus_dev_t tdm_dev = {};
    tdm_dev.name = "astro-i2s-audio-out";
    tdm_dev.vid = PDEV_VID_AMLOGIC;
    tdm_dev.pid = PDEV_PID_AMLOGIC_S905D2;
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
    status = pbus_.CompositeDeviceAdd(&tdm_dev, reinterpret_cast<uint64_t>(tdm_i2s_fragments),
                                      countof(tdm_i2s_fragments), UINT32_MAX);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: I2S CompositeDeviceAdd failed: %d", __FILE__, status);
      return status;
    }
  }

  // Input devices.
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
    snprintf(metadata.product_name, sizeof(metadata.product_name), "astro");
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
    // Add DAI or controller driver depending on ENABLE_DAI_MODE.
    pbus_dev_t tdm_dev = {};
    tdm_dev.vid = PDEV_VID_AMLOGIC;
    tdm_dev.pid = PDEV_PID_AMLOGIC_S905D2;
    tdm_dev.mmio_list = audio_mmios;
    tdm_dev.mmio_count = countof(audio_mmios);
    tdm_dev.bti_list = pcm_in_btis;
    tdm_dev.bti_count = countof(pcm_in_btis);
    tdm_dev.metadata_list = tdm_metadata;
    tdm_dev.metadata_count = countof(tdm_metadata);
#ifdef ENABLE_DAI_MODE
    tdm_dev.name = "astro-pcm-dai-in";
    tdm_dev.did = PDEV_DID_AMLOGIC_DAI_IN;
    status = pbus_.DeviceAdd(&tdm_dev);
#else
    tdm_dev.name = "astro-pcm-audio-in";
    tdm_dev.did = PDEV_DID_AMLOGIC_TDM;
    tdm_dev.instance_id = tdm_instance_id++;
    tdm_dev.irq_list = toddr_a_irqs;
    tdm_dev.irq_count = countof(toddr_a_irqs);
    status = pbus_.CompositeDeviceAdd(&tdm_dev, reinterpret_cast<uint64_t>(tdm_pcm_fragments),
                                      countof(tdm_pcm_fragments), UINT32_MAX);
#endif
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: PCM CompositeDeviceAdd failed: %d", __FILE__, status);
      return status;
    }
  }

#ifdef ENABLE_DAI_MODE
#ifdef ENABLE_DAI_TEST
  // Add test driver.
  bool is_input = true;
  const device_metadata_t test_metadata[] = {
      {
          .type = DEVICE_METADATA_PRIVATE,
          .data = &is_input,
          .length = sizeof(is_input),
      },
  };
  zx_device_prop_t props[] = {{BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
                              {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_DAI_TEST}};
  composite_device_desc_t comp_desc = {};
  comp_desc.props = props;
  comp_desc.props_count = countof(props);
  comp_desc.coresident_device_index = UINT32_MAX;
  comp_desc.fragments = dai_test_in_fragments;
  comp_desc.fragments_count = countof(dai_test_in_fragments);
  comp_desc.metadata_list = test_metadata;
  comp_desc.metadata_count = countof(test_metadata);
  status = DdkAddComposite("astro-dai-test-in", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: PCM CompositeDeviceAdd failed: %d", __FILE__, status);
    return status;
  }
#endif
#endif
#endif

  // Input device.
  {
    metadata::AmlPdmConfig metadata = {};
    snprintf(metadata.manufacturer, sizeof(metadata.manufacturer), "Spacely Sprockets");
    snprintf(metadata.product_name, sizeof(metadata.product_name), "astro");
    metadata.number_of_channels = 2;
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

    static const pbus_mmio_t pdm_mmios[] = {
        {.base = S905D2_EE_PDM_BASE, .length = S905D2_EE_PDM_LENGTH},
        {.base = S905D2_EE_AUDIO_BASE, .length = S905D2_EE_AUDIO_LENGTH},
    };

    static const pbus_bti_t pdm_btis[] = {
        {
            .iommu_index = 0,
            .bti_id = BTI_AUDIO_IN,
        },
    };

    pbus_dev_t dev_in = {};
    dev_in.name = "astro-audio-pdm-in";
    dev_in.vid = PDEV_VID_AMLOGIC;
    dev_in.pid = PDEV_PID_AMLOGIC_S905D2;
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
  return ZX_OK;
}

}  // namespace astro

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddktl/metadata/audio.h>
#include <soc/aml-common/aml-audio.h>
#include <soc/aml-meson/g12a-clk.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro-gpios.h"
#include "astro.h"

// Enables BT PCM audio.
// #define ENABLE_BT

namespace astro {

constexpr uint32_t kCodecVid = PDEV_VID_TI;
constexpr uint32_t kCodecDid = PDEV_DID_TI_TAS2770;

static const pbus_mmio_t audio_mmios[] = {
    {.base = S905D2_EE_AUDIO_BASE, .length = S905D2_EE_AUDIO_LENGTH},
};

static const pbus_bti_t tdm_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_AUDIO_OUT,
    },
};

static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
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

static const device_fragment_part_t i2c_fragment[] = {
    {countof(root_match), root_match},
    {countof(i2c_match), i2c_match},
};

static const device_fragment_part_t fault_gpio_fragment[] = {
    {countof(root_match), root_match},
    {countof(fault_gpio_match), fault_gpio_match},
};

static const device_fragment_part_t enable_gpio_fragment[] = {
    {countof(root_match), root_match},
    {countof(enable_gpio_match), enable_gpio_match},
};
static const device_fragment_part_t codec_fragment[] = {
    {countof(root_match), root_match},
    {countof(codec_match), codec_match},
};

#ifdef ENABLE_BT
static const device_fragment_new_t tdm_pcm_fragments[] = {};
#endif
static const device_fragment_new_t tdm_i2s_fragments[] = {
    {"gpio", countof(enable_gpio_fragment), enable_gpio_fragment},
    {"codec", countof(codec_fragment), codec_fragment},
};
static const device_fragment_new_t codec_fragments[] = {
    {"i2c", countof(i2c_fragment), i2c_fragment},
    {"gpio", countof(fault_gpio_fragment), fault_gpio_fragment},
};

// PDM input configurations
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

static const pbus_dev_t pdm_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "astro-audio-in";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_S905D2;
  dev.did = PDEV_DID_AMLOGIC_PDM;
  dev.mmio_list = pdm_mmios;
  dev.mmio_count = countof(pdm_mmios);
  dev.bti_list = pdm_btis;
  dev.bti_count = countof(pdm_btis);
  return dev;
}();

zx_status_t Astro::AudioInit() {
  zx_status_t status;

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
    metadata.tdm.type = metadata::TdmType::Pcm;
    metadata.tdm.sclk_on_raising = true;
    metadata.tdm.bits_per_sample = 16;
    metadata.tdm.bits_per_slot = 16;
    metadata.number_of_channels = 1;
    metadata.dai_number_of_channels = 1;
    metadata.lanes_enable_mask[0] = 1;
    pbus_metadata_t tdm_metadata[] = {
        {
            .type = DEVICE_METADATA_PRIVATE,
            .data_buffer = &metadata,
            .data_size = sizeof(metadata),
        },
    };

    pbus_dev_t tdm_dev = {};
    tdm_dev.name = "astro-pcm-audio-out";
    tdm_dev.vid = PDEV_VID_AMLOGIC;
    tdm_dev.pid = PDEV_PID_AMLOGIC_S905D2;
    tdm_dev.did = PDEV_DID_AMLOGIC_TDM;
    tdm_dev.mmio_list = audio_mmios;
    tdm_dev.mmio_count = countof(audio_mmios);
    tdm_dev.bti_list = pcm_out_btis;
    tdm_dev.bti_count = countof(pcm_out_btis);
    tdm_dev.metadata_list = tdm_metadata;
    tdm_dev.metadata_count = countof(tdm_metadata);
    status = pbus_.CompositeDeviceAddNew(&tdm_dev, tdm_pcm_fragments, countof(tdm_pcm_fragments),
                                         UINT32_MAX);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: PCM CompositeDeviceAddNew failed: %d", __FILE__, status);
      return status;
    }
  }
#endif
  // Add TDM OUT to the codec.
  {
    zx_device_prop_t props[] = {{BIND_PLATFORM_DEV_VID, 0, kCodecVid},
                                {BIND_PLATFORM_DEV_DID, 0, kCodecDid}};

    composite_device_desc_new_t comp_desc = {};
    comp_desc.props = props;
    comp_desc.props_count = countof(props);
    comp_desc.coresident_device_index = UINT32_MAX;
    comp_desc.fragments = codec_fragments;
    comp_desc.fragments_count = countof(codec_fragments);
    status = DdkAddCompositeNew("audio-codec-tas27xx", &comp_desc);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s DdkAddCompositeNew failed %d", __FILE__, status);
      return status;
    }

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
    metadata.tdm.type = metadata::TdmType::I2s;
    metadata.number_of_channels = 1;
    metadata.lanes_enable_mask[0] = 1;
    metadata.tdm.number_of_codecs = 1;
    metadata.tdm.codecs[0] = metadata::Codec::Tas27xx;
    // Report our external delay based on the chosen frame rate.  Note that these
    // delays were measured on Astro hardware, and should be pretty good, but they
    // will not be perfect.  One reason for this is that we are not taking any
    // steps to align our start time with start of a TDM frame, which will cause
    // up to 1 frame worth of startup error ever time that the output starts.
    // Also note that this is really nothing to worry about.  Hitting our target
    // to within 20.8uSec (for 48k) is pretty good.
    metadata.tdm.number_of_external_delays = 2;
    metadata.tdm.external_delays[0].frequency = 48'000;
    metadata.tdm.external_delays[0].nsecs = ZX_USEC(125);
    metadata.tdm.external_delays[1].frequency = 96'000;
    metadata.tdm.external_delays[1].nsecs = ZX_NSEC(83333);
    metadata.codecs_channels_mask[0] = (1 << 0);
    pbus_metadata_t tdm_metadata[] = {
        {
            .type = DEVICE_METADATA_PRIVATE,
            .data_buffer = &metadata,
            .data_size = sizeof(metadata),
        },
    };

    pbus_dev_t tdm_dev = {};
    tdm_dev.name = "astro-i2s-audio-out";
    tdm_dev.vid = PDEV_VID_AMLOGIC;
    tdm_dev.pid = PDEV_PID_AMLOGIC_S905D2;
    tdm_dev.did = PDEV_DID_AMLOGIC_TDM;
    tdm_dev.mmio_list = audio_mmios;
    tdm_dev.mmio_count = countof(audio_mmios);
    tdm_dev.bti_list = tdm_btis;
    tdm_dev.bti_count = countof(tdm_btis);
    tdm_dev.metadata_list = tdm_metadata;
    tdm_dev.metadata_count = countof(tdm_metadata);
    status = pbus_.CompositeDeviceAddNew(&tdm_dev, tdm_i2s_fragments, countof(tdm_i2s_fragments),
                                         UINT32_MAX);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: I2S CompositeDeviceAddNew failed: %d", __FILE__, status);
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
    metadata.tdm.type = metadata::TdmType::Pcm;
    metadata.tdm.sclk_on_raising = true;
    metadata.tdm.bits_per_sample = 16;
    metadata.tdm.bits_per_slot = 16;
    metadata.number_of_channels = 1;
    metadata.dai_number_of_channels = 1;
    metadata.swaps = 0x0200;
    metadata.lanes_enable_mask[1] = 1;
    pbus_metadata_t tdm_metadata[] = {
        {
            .type = DEVICE_METADATA_PRIVATE,
            .data_buffer = &metadata,
            .data_size = sizeof(metadata),
        },
    };
    pbus_dev_t tdm_dev = {};
    tdm_dev.name = "astro-pcm-audio-in";
    tdm_dev.vid = PDEV_VID_AMLOGIC;
    tdm_dev.pid = PDEV_PID_AMLOGIC_S905D2;
    tdm_dev.did = PDEV_DID_AMLOGIC_TDM;
    tdm_dev.mmio_list = audio_mmios;
    tdm_dev.mmio_count = countof(audio_mmios);
    tdm_dev.bti_list = pcm_in_btis;
    tdm_dev.bti_count = countof(pcm_in_btis);
    tdm_dev.metadata_list = tdm_metadata;
    tdm_dev.metadata_count = countof(tdm_metadata);
    status = pbus_.CompositeDeviceAdd(&tdm_dev, tdm_pcm_fragments, countof(tdm_pcm_fragments),
                                      UINT32_MAX);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: PCM CompositeDeviceAdd failed: %d", __FILE__, status);
      return status;
    }
  }
#endif
  status = pbus_.DeviceAdd(&pdm_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: PDM DeviceAdd failed: %d", __FILE__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace astro

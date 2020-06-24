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
#include <soc/aml-meson/g12a-clk.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro-gpios.h"
#include "astro.h"

// TODO(52506): Re-enable when fxb/54011 is resolved.
//#define ENABLE_BT_PCM

namespace astro {

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

#ifdef ENABLE_BT_PCM
static const device_fragment_t tdm_pcm_fragments[] = {};
#endif
static const device_fragment_t tdm_i2s_fragments[] = {
    {countof(i2c_fragment), i2c_fragment},
    {countof(fault_gpio_fragment), fault_gpio_fragment},
    {countof(enable_gpio_fragment), enable_gpio_fragment},
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
  constexpr uint8_t medium_strength = 2;  // Not mA, unitless values from AMLogic.
  gpio_impl_.SetDriveStrength(S905D2_GPIOA(1), medium_strength);
  gpio_impl_.SetDriveStrength(S905D2_GPIOA(2), medium_strength);
  gpio_impl_.SetDriveStrength(S905D2_GPIOA(3), medium_strength);

#ifdef ENABLE_BT_PCM
  // PCM pin assignments.
  gpio_impl_.SetAltFunction(S905D2_GPIOX(8), 1);
  gpio_impl_.SetAltFunction(S905D2_GPIOX(9), 1);
  gpio_impl_.SetAltFunction(S905D2_GPIOX(10), 1);
  gpio_impl_.SetAltFunction(S905D2_GPIOX(11), 1);
  gpio_impl_.SetDriveStrength(S905D2_GPIOX(8), medium_strength);
  gpio_impl_.SetDriveStrength(S905D2_GPIOX(9), medium_strength);
  gpio_impl_.SetDriveStrength(S905D2_GPIOX(10), medium_strength);
  gpio_impl_.SetDriveStrength(S905D2_GPIOX(11), medium_strength);
#endif

  // PDM pin assignments
  gpio_impl_.SetAltFunction(S905D2_GPIOA(7), S905D2_GPIOA_7_PDM_DCLK_FN);
  gpio_impl_.SetAltFunction(S905D2_GPIOA(8), S905D2_GPIOA_8_PDM_DIN0_FN);

  gpio_impl_.ConfigOut(S905D2_GPIOA(5), 1);

#ifdef ENABLE_BT_PCM
  // Output devices.
  // Add PCM TDM.
  {
    metadata::Tdm metadata = {};
    metadata.type = metadata::TdmType::Pcm;
    metadata.codec = metadata::Codec::None;
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
    tdm_dev.bti_list = tdm_btis;
    tdm_dev.bti_count = countof(tdm_btis);
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
  // Add I2S TDM.
  {
    metadata::Tdm metadata = {};
    metadata.type = metadata::TdmType::I2s;
    metadata.codec = metadata::Codec::Tas2770;
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
    status = pbus_.CompositeDeviceAdd(&tdm_dev, tdm_i2s_fragments, countof(tdm_i2s_fragments),
                                      UINT32_MAX);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: I2S CompositeDeviceAdd failed: %d", __FILE__, status);
      return status;
    }
  }

  // Input devices.
  status = pbus_.DeviceAdd(&pdm_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: PDM DeviceAdd failed: %d", __FILE__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace astro

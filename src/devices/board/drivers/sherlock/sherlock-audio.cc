// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddktl/metadata/audio.h>
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

  // We treat EVT and higher the same (having 3 TAS5720s).
  metadata::Codec out_codec = metadata::Codec::Tas5720x3;
  if (board_info.board_revision < BOARD_REV_EVT1) {
    // For audio we don't support boards revision lower than EVT.
    zxlogf(WARNING, "%s: Board revision unsupported, skipping audio initialization.", __FILE__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  pbus_metadata_t out_metadata[] = {
      {
          .type = DEVICE_METADATA_PRIVATE,
          .data_buffer = &out_codec,
          .data_size = sizeof(out_codec),
      },
  };

  constexpr zx_bind_inst_t root_match[] = {
      BI_MATCH(),
  };
  constexpr zx_bind_inst_t fault_gpio_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_AUDIO_SOC_FAULT_L),
  };
  constexpr zx_bind_inst_t enable_gpio_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_SOC_AUDIO_EN),
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
  constexpr zx_bind_inst_t woofer_i2c_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
      BI_ABORT_IF(NE, BIND_I2C_BUS_ID, SHERLOCK_I2C_A0_0),
      BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x6f),
  };
  const device_fragment_part_t fault_gpio_fragment[] = {
      {countof(root_match), root_match},
      {countof(fault_gpio_match), fault_gpio_match},
  };
  const device_fragment_part_t enable_gpio_fragment[] = {
      {countof(root_match), root_match},
      {countof(enable_gpio_match), enable_gpio_match},
  };
  const device_fragment_part_t tweeter_left_i2c_fragment[] = {
      {countof(root_match), root_match},
      {countof(tweeter_left_i2c_match), tweeter_left_i2c_match},
  };
  const device_fragment_part_t tweeter_right_i2c_fragment[] = {
      {countof(root_match), root_match},
      {countof(tweeter_right_i2c_match), tweeter_right_i2c_match},
  };
  const device_fragment_part_t woofer_i2c_fragment[] = {
      {countof(root_match), root_match},
      {countof(woofer_i2c_match), woofer_i2c_match},
  };
  const device_fragment_t fragments[] = {
      {countof(fault_gpio_fragment), fault_gpio_fragment},
      {countof(enable_gpio_fragment), enable_gpio_fragment},
      {countof(tweeter_left_i2c_fragment), tweeter_left_i2c_fragment},
      {countof(tweeter_right_i2c_fragment), tweeter_right_i2c_fragment},
      {countof(woofer_i2c_fragment), woofer_i2c_fragment},
  };

  pbus_dev_t tdm_dev = {};
  tdm_dev.name = "SherlockAudio";
  tdm_dev.vid = PDEV_VID_AMLOGIC;
  tdm_dev.pid = PDEV_PID_AMLOGIC_T931;
  tdm_dev.did = PDEV_DID_AMLOGIC_TDM;
  tdm_dev.mmio_list = audio_mmios;
  tdm_dev.mmio_count = countof(audio_mmios);
  tdm_dev.bti_list = tdm_btis;
  tdm_dev.bti_count = countof(tdm_btis);
  tdm_dev.metadata_list = out_metadata;
  tdm_dev.metadata_count = countof(out_metadata);

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

  pbus_dev_t pdm_dev = {};
  pdm_dev.name = "SherlockAudioIn";
  pdm_dev.vid = PDEV_VID_AMLOGIC;
  pdm_dev.pid = PDEV_PID_AMLOGIC_T931;
  pdm_dev.did = PDEV_DID_SHERLOCK_PDM;
  pdm_dev.mmio_list = pdm_mmios;
  pdm_dev.mmio_count = countof(pdm_mmios);
  pdm_dev.bti_list = pdm_btis;
  pdm_dev.bti_count = countof(pdm_btis);

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
  gpio_impl_.SetAltFunction(T931_GPIOZ(3), T931_GPIOZ_3_TDMC_D1_FN);
  gpio_impl_.SetAltFunction(T931_GPIOAO(9), T931_GPIOAO_9_MCLK_FN);

  // PDM pin assignments.
  gpio_impl_.SetAltFunction(T931_GPIOA(7), T931_GPIOA_7_PDM_DCLK_FN);
  gpio_impl_.SetAltFunction(T931_GPIOA(8), T931_GPIOA_8_PDM_DIN0_FN);

  gpio_impl_.ConfigOut(T931_GPIOH(7), 1);  // SOC_AUDIO_EN.

  status = pbus_.CompositeDeviceAdd(&tdm_dev, fragments, countof(fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s pbus_.DeviceAdd failed %d", __FUNCTION__, status);
    return status;
  }
  status = pbus_.DeviceAdd(&pdm_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s pbus_.DeviceAdd failed %d", __FUNCTION__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace sherlock

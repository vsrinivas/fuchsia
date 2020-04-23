// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/mmio/mmio.h>
#include <limits.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio.h>
#include <ddktl/metadata/audio.h>
#include <ddktl/protocol/clockimpl.h>
#include <fbl/algorithm.h>
#include <hwreg/bitfields.h>
#include <soc/mt8167/mt8167-clk.h>
#include <soc/mt8167/mt8167-gpio.h>
#include <soc/mt8167/mt8167-hw.h>

#include "mt8167.h"

namespace board_mt8167 {

class WACS2_CMD : public hwreg::RegisterBase<WACS2_CMD, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<WACS2_CMD>(0x00A0); }

  DEF_BIT(31, WACS2_WRITE);
  DEF_FIELD(30, 16, WACS2_ADR);
  DEF_FIELD(15, 0, WACS2_WDATA);
};

class WACS2_RDATA : public hwreg::RegisterBase<WACS2_RDATA, uint32_t> {
 public:
  static constexpr uint32_t kStateIdle = 0;

  static auto Get() { return hwreg::RegisterAddr<WACS2_RDATA>(0x00A4); }

  DEF_FIELD(18, 16, status);
};

constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t in_i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 1),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x1B),
};
static const zx_bind_inst_t mt8167s_out_i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 2),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x48),
};
static const zx_bind_inst_t cleo_out_i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 2),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x2C),
};
static const zx_bind_inst_t mt8167s_out_codec_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CODEC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TI),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TI_TAS5782),
};
static const zx_bind_inst_t cleo_out_codec_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CODEC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TI),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TI_TAS5805),
};
static const device_fragment_part_t in_i2c_fragment[] = {
    {fbl::count_of(root_match), root_match},
    {fbl::count_of(in_i2c_match), in_i2c_match},
};
static const device_fragment_part_t mt8167s_out_i2c_fragment[] = {
    {fbl::count_of(root_match), root_match},
    {fbl::count_of(mt8167s_out_i2c_match), mt8167s_out_i2c_match},
};
static const device_fragment_part_t cleo_out_i2c_fragment[] = {
    {fbl::count_of(root_match), root_match},
    {fbl::count_of(cleo_out_i2c_match), cleo_out_i2c_match},
};
static const device_fragment_part_t cleo_out_codec_fragment[] = {
    {fbl::count_of(root_match), root_match},
    {fbl::count_of(cleo_out_codec_match), cleo_out_codec_match},
};
static const device_fragment_part_t mt8167s_out_codec_fragment[] = {
    {fbl::count_of(root_match), root_match},
    {fbl::count_of(mt8167s_out_codec_match), mt8167s_out_codec_match},
};

static const zx_bind_inst_t in_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_GPIO24_EINT24),
};
static const zx_bind_inst_t mt8167s_out_reset_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_GPIO107_MSDC1_DAT1),
};
static const zx_bind_inst_t mt8167s_out_mute_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_GPIO108_MSDC1_DAT2),
};
static const device_fragment_part_t in_gpio_fragment[] = {
    {countof(root_match), root_match},
    {countof(in_gpio_match), in_gpio_match},
};
static const device_fragment_part_t mt8167s_out_reset_gpio_fragment[] = {
    {countof(root_match), root_match},
    {countof(mt8167s_out_reset_gpio_match), mt8167s_out_reset_gpio_match},
};
static const device_fragment_part_t mt8167s_out_mute_gpio_fragment[] = {
    {countof(root_match), root_match},
    {countof(mt8167s_out_mute_gpio_match), mt8167s_out_mute_gpio_match},
};

static const device_fragment_t in_fragments[] = {
    {countof(in_i2c_fragment), in_i2c_fragment},
    {countof(in_gpio_fragment), in_gpio_fragment},
};
static const device_fragment_t mt8167s_codec_fragments[] = {
    {countof(mt8167s_out_i2c_fragment), mt8167s_out_i2c_fragment},
    {countof(mt8167s_out_reset_gpio_fragment), mt8167s_out_reset_gpio_fragment},
    {countof(mt8167s_out_mute_gpio_fragment), mt8167s_out_mute_gpio_fragment},
};
static const device_fragment_t mt8167s_controller_fragments[] = {
    {countof(mt8167s_out_codec_fragment), mt8167s_out_codec_fragment},
};
static const device_fragment_t cleo_codec_fragments[] = {
    {countof(cleo_out_i2c_fragment), cleo_out_i2c_fragment},
};
static const device_fragment_t cleo_controller_fragments[] = {
    {countof(cleo_out_codec_fragment), cleo_out_codec_fragment},
};

zx_status_t Mt8167::AudioInit() {
  if (board_info_.pid != PDEV_PID_MEDIATEK_8167S_REF && board_info_.pid != PDEV_PID_CLEO) {
    // We only support the boards listed above.
    return ZX_ERR_NOT_SUPPORTED;
  }
  constexpr pbus_mmio_t mmios[] = {
      {
          .base = MT8167_AUDIO_BASE,
          .length = MT8167_AUDIO_SIZE,
      },
      // MMIO for clocks.
      // TODO(andresoportus): Move this to a clock driver.
      {
          .base = MT8167_XO_BASE,
          .length = MT8167_XO_SIZE,
      },
      {
          .base = MT8167_PLL_BASE,
          .length = MT8167_PLL_SIZE,
      },
  };

  static constexpr pbus_bti_t btis_out[] = {
      {
          .iommu_index = 0,
          .bti_id = BTI_AUDIO_OUT,
      },
  };

  static constexpr pbus_bti_t btis_in[] = {
      {
          .iommu_index = 0,
          .bti_id = BTI_AUDIO_IN,
      },
  };

  metadata::Codec out_codec = metadata::Codec::Tas5782;  // Default to PDEV_PID_MEDIATEK_8167S_REF.
  if (board_info_.pid == PDEV_PID_CLEO) {
    out_codec = metadata::Codec::Tas5805;
  }
  pbus_metadata_t out_metadata[] = {
      {
          .type = DEVICE_METADATA_PRIVATE,
          .data_buffer = &out_codec,
          .data_size = sizeof(out_codec),
      },
  };

  pbus_dev_t controller_out = {};
  controller_out.name = "mt8167-audio-out";
  controller_out.vid = PDEV_VID_MEDIATEK;
  controller_out.pid = PDEV_PID_MEDIATEK_8167S_REF;
  controller_out.did = PDEV_DID_MEDIATEK_AUDIO_OUT;
  controller_out.mmio_list = mmios;
  controller_out.mmio_count = countof(mmios);
  controller_out.bti_list = btis_out;
  controller_out.bti_count = countof(btis_out);
  controller_out.metadata_list = out_metadata;
  controller_out.metadata_count = countof(out_metadata);

  pbus_dev_t dev_in = {};
  dev_in.name = "mt8167-audio-in";
  dev_in.vid = PDEV_VID_MEDIATEK;
  dev_in.pid = PDEV_PID_MEDIATEK_8167S_REF;
  dev_in.did = PDEV_DID_MEDIATEK_AUDIO_IN;
  dev_in.mmio_list = mmios;
  dev_in.mmio_count = countof(mmios);
  dev_in.bti_list = btis_in;
  dev_in.bti_count = countof(btis_in);

  // Output pin assignments.
  // Datasheet has 2 numberings for I2S engines: I2S[0-3] (used in GPIOs) and I2S[1-4] (other
  // registers). 8CH corresponds to I2S2 in the 1-4 range (MtAudioOutDevice::I2S2).
  gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO25_EINT25, MT8167_GPIO25_I2S2_MCK_FN);
  gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO55_I2S_DATA_IN, MT8167_GPIO55_I2S_8CH_DO1_FN);
  gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO56_I2S_LRCK, MT8167_GPIO56_I2S_8CH_LRCK_FN);
  gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO25_I2S_BCK, MT8167_GPIO57_I2S_8CH_BCK_FN);

  if (board_info_.pid == PDEV_PID_MEDIATEK_8167S_REF) {
    // ~AMP_RESET.
    gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO107_MSDC1_DAT1, MT8167_GPIO_GPIO_FN);
    gpio_impl_config_out(&gpio_impl_, MT8167_GPIO107_MSDC1_DAT1, 1);  // Set to "not reset".

    // ~AMP_MUTE.
    gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO108_MSDC1_DAT2, MT8167_GPIO_GPIO_FN);
    gpio_impl_config_out(&gpio_impl_, MT8167_GPIO108_MSDC1_DAT2, 1);  // Set to "not mute".
  } else {                                                            // Cleo.
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
    gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO107_MSDC1_DAT1, MT8167_GPIO_GPIO_FN);
    gpio_impl_config_out(&gpio_impl_, MT8167_GPIO107_MSDC1_DAT1, 1);  // Set PDN to High.
    zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));
    // I2S clocks are configured by the controller and the rest of the initialization is done
    // in the codec itself.
  }

  // Input pin assignments.
  gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO100_CMDAT0, MT8167_GPIO100_TDM_RX_MCK_FN);
  gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO101_CMDAT1, MT8167_GPIO101_TDM_RX_BCK_FN);
  if (board_info_.pid == PDEV_PID_MEDIATEK_8167S_REF) {
    gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO16_EINT16, MT8167_GPIO16_TDM_RX_LRCK_FN);
  } else {
    gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO102_CMMCLK, MT8167_GPIO102_TDM_RX_LRCK_FN);
  }
  gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO103_CMPCLK, MT8167_GPIO103_TDM_RX_DI_FN);

  // ~ADC_RESET.
  gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO24_EINT24, MT8167_GPIO_GPIO_FN);
  gpio_impl_config_out(&gpio_impl_, MT8167_GPIO24_EINT24, 1);  // Set to "not reset".

  // Please do not use get_root_resource() in new code. See ZX-1467.
  zx::unowned_resource root_resource(get_root_resource());
  std::optional<ddk::MmioBuffer> pmic_mmio;
  auto status =
      ddk::MmioBuffer::Create(MT8167_PMIC_WRAP_BASE, MT8167_PMIC_WRAP_SIZE, *root_resource,
                              ZX_CACHE_POLICY_UNCACHED_DEVICE, &pmic_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: PMIC MmioBuffer::Create failed %d", __FUNCTION__, status);
    return status;
  }

  // Wait for the PMIC to be IDLE.
  while (WACS2_RDATA::Get().ReadFrom(&(*pmic_mmio)).status() != WACS2_RDATA::kStateIdle) {
  }

  // Set the VCN 1.8 Volts by sending a command to the PMIC via the SOC's PMIC WRAP interface.
  constexpr uint32_t kDigLdoCon11 = 0x0512;
  constexpr uint16_t kVcn18Enable = 0x4001;
  auto pmic = WACS2_CMD::Get().ReadFrom(&(*pmic_mmio));
  // From the documentation "Wrapper access: Address[15:1]" hence the >> 1.
  pmic.set_WACS2_WRITE(1).set_WACS2_ADR(kDigLdoCon11 >> 1).set_WACS2_WDATA(kVcn18Enable);
  pmic.WriteTo(&(*pmic_mmio));

  // Enable clocks. These are needed by both the input and output drivers, so enable them here
  // instead of in those drivers.
  ddk::ClockImplProtocolClient clock = parent();
  if (!clock.is_valid()) {
    zxlogf(ERROR, "%s: could not get CLOCK_IMPL protocol", __func__);
    return ZX_ERR_INTERNAL;
  }
  clock.Enable(kClkRgAud1);
  clock.Enable(kClkRgAud2);

  if (board_info_.pid == PDEV_PID_MEDIATEK_8167S_REF) {
    constexpr zx_device_prop_t props[] = {{BIND_PLATFORM_DEV_VID, 0, PDEV_VID_TI},
                                          {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_TI_TAS5782}};

    const composite_device_desc_t comp_desc = {
        .props = props,
        .props_count = fbl::count_of(props),
        .fragments = mt8167s_codec_fragments,
        .fragments_count = countof(mt8167s_codec_fragments),
        .coresident_device_index = UINT32_MAX,
        .metadata_list = nullptr,
        .metadata_count = 0,
    };

    status = DdkAddComposite("audio-tas5782", &comp_desc);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: DdkAddComposite failed %d", __FUNCTION__, status);
      return status;
    }

    status = pbus_.CompositeDeviceAdd(&controller_out, mt8167s_controller_fragments,
                                      countof(mt8167s_controller_fragments), UINT32_MAX);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: pbus_.CompositeDeviceAdd failed %d", __FUNCTION__, status);
      return status;
    }
  } else {
    constexpr zx_device_prop_t props[] = {{BIND_PLATFORM_DEV_VID, 0, PDEV_VID_TI},
                                          {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_TI_TAS5805}};

    const composite_device_desc_t comp_desc = {
        .props = props,
        .props_count = fbl::count_of(props),
        .fragments = cleo_codec_fragments,
        .fragments_count = countof(cleo_codec_fragments),
        .coresident_device_index = UINT32_MAX,
        .metadata_list = nullptr,
        .metadata_count = 0,
    };

    status = DdkAddComposite("audio-tas5805", &comp_desc);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: DdkAddComposite failed %d", __FUNCTION__, status);
      return status;
    }

    status = pbus_.CompositeDeviceAdd(&controller_out, cleo_controller_fragments,
                                      countof(cleo_controller_fragments), UINT32_MAX);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: pbus_.CompositeDeviceAdd failed %d", __FUNCTION__, status);
      return status;
    }
  }
  status = pbus_.CompositeDeviceAdd(&dev_in, in_fragments, countof(in_fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: pbus_.CompositeDeviceAdd failed %d", __FUNCTION__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace board_mt8167

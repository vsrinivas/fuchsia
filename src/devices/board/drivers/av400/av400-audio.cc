// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/mmio/mmio.h>
#include <string.h>

#include <ddktl/metadata/audio.h>
#include <soc/aml-a5/a5-gpio.h>
#include <soc/aml-common/aml-audio.h>
#include <ti/ti-audio.h>

#include "av400.h"
#include "src/devices/board/drivers/av400/audio-tas5707-stereo-bind.h"
#include "src/devices/board/drivers/av400/tdm-i2s-bind.h"

namespace av400 {

static constexpr pbus_mmio_t audio_mmios[] = {{
    .base = A5_EE_AUDIO_BASE,
    .length = A5_EE_AUDIO_LENGTH,
}};

static constexpr pbus_bti_t tdm_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_AUDIO_OUT,
    },
};

static constexpr pbus_bti_t tdm_in_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_AUDIO_IN,
    },
};

static constexpr pbus_irq_t frddr_b_irqs[] = {
    {
        .irq = A5_AUDIO_FRDDR_B,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static constexpr pbus_irq_t toddr_a_irqs[] = {
    {
        .irq = A5_AUDIO_TODDR_A,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static constexpr pbus_mmio_t pdm_mmios[] = {{
                                                .base = A5_EE_PDM_BASE,
                                                .length = A5_EE_PDM_LENGTH,
                                            },
                                            {
                                                .base = A5_EE_AUDIO_BASE,
                                                .length = A5_EE_AUDIO_LENGTH,
                                            },
                                            {
                                                .base = A5_EE_AUDIO2_BASE,
                                                .length = A5_EE_AUDIO2_LENGTH,
                                            }};
static constexpr pbus_bti_t pdm_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_AUDIO_IN,
    },
};

static constexpr pbus_irq_t toddr_b_irqs[] = {
    {
        .irq = A5_AUDIO_TODDR_B,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static zx_status_t InitAudioTop(void) {
  // For some amlogic chips, they has Audio Top Clock Gating Control.
  // This part will affect audio registers access, to avoid bus hang,
  // we need call it before we access the registers.
  zx_status_t status;
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx::unowned_resource resource(get_root_resource());
  std::optional<fdf::MmioBuffer> buf;
  status = fdf::MmioBuffer::Create(A5_EE_AUDIO2_BASE_ALIGN, A5_EE_AUDIO2_LENGTH_ALIGN, *resource,
                                   ZX_CACHE_POLICY_UNCACHED_DEVICE, &buf);
  if (status != ZX_OK) {
    zxlogf(ERROR, "MmioBuffer::Create failed %s", zx_status_get_string(status));
    return status;
  }

  // Auido clock gate
  // Bit 7    : top clk gate
  // Bit 6 ~ 5: reserved.
  // Bit 4    : tovad clk gate
  // Bit 3    : toddr_vad clk gate
  // Bit 2    : tdmin_vad clk gate
  // Bit 1    : pdm clk gate
  // Bit 0    : ddr_arb clk gate
  constexpr uint32_t clkgate = 0xff;
  buf->Write32(clkgate, A5_EE_AUDIO2_CLK_GATE_EN0);
  zxlogf(INFO, "Enable Audio Top");

  return ZX_OK;
}

static zx_status_t HifiPllInit(void) {
  zx_status_t status;
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx::unowned_resource resource(get_root_resource());
  std::optional<fdf::MmioBuffer> buf;
  status = fdf::MmioBuffer::Create(A5_ANACTRL_BASE, A5_ANACTRL_LENGTH, *resource,
                                   ZX_CACHE_POLICY_UNCACHED_DEVICE, &buf);
  if (status != ZX_OK) {
    zxlogf(ERROR, "MmioBuffer::Create failed %s", zx_status_get_string(status));
    return status;
  }

  uint32_t lock_check = PLL_LOCK_CHECK_MAX;
  uint32_t val = 0;
  constexpr uint32_t hifi_ctl0 = (128 << 0) |  // dco_m  [7:0]   = 128
                                 (1 << 10) |   // dco_n  [14:10] = 1
                                 (2 << 16) |   // dco_od [17:16] = 2
                                 (1 << 28) |   // dco_en [28]    = 1 (enable)
                                 (1 << 29);    // reset  [29]    = 1 (hold reset)

  constexpr uint32_t hifi_ctl1 = 0x6aab;      // frac [18:0]
  constexpr uint32_t hifi_ctl2 = 0x0;         // ss mode ctrl - unset
  constexpr uint32_t hifi_ctl3 = 0x6a285c00;  // hifi_ctl3 ~ hifi_ctl6
  constexpr uint32_t hifi_ctl4 = 0x65771290;  //
  constexpr uint32_t hifi_ctl5 = 0x39272000;  // default setting (don't modify it)
  constexpr uint32_t hifi_ctl6 = 0x56540000;  //
  do {
    /*
     * DCO = 24M * (M + 0) / N
     * CLK = DCO / (1 << od)
     *
     * A5_ANACTRL_HIFIPLL_CTRL0
     * => HIFI_DCO = 24M * 128 / 1 = 3072M
     * => HIFI_CLK = 3072M / (1 << 2) = 768M
     */
    buf->Write32(hifi_ctl0, A5_ANACTRL_HIFIPLL_CTRL0);
    buf->Write32(hifi_ctl1, A5_ANACTRL_HIFIPLL_CTRL1);
    buf->Write32(hifi_ctl2, A5_ANACTRL_HIFIPLL_CTRL2);
    buf->Write32(hifi_ctl3, A5_ANACTRL_HIFIPLL_CTRL3);
    buf->Write32(hifi_ctl4, A5_ANACTRL_HIFIPLL_CTRL4);
    buf->Write32(hifi_ctl5, A5_ANACTRL_HIFIPLL_CTRL5);
    buf->Write32(hifi_ctl6, A5_ANACTRL_HIFIPLL_CTRL6);

    // Write bit31 from 1 to 0:
    // Add some delay, make the PLL more stable, if not,
    // It will probably fail locking
    val = buf->Read32(A5_ANACTRL_HIFIPLL_CTRL0);
    buf->Write32(val | (1 << 29), A5_ANACTRL_HIFIPLL_CTRL0);
    zx_nanosleep(zx_deadline_after(ZX_USEC(10)));

    val = buf->Read32(A5_ANACTRL_HIFIPLL_CTRL0);
    buf->Write32(val & (~(1 << 29)), A5_ANACTRL_HIFIPLL_CTRL0);
    zx_nanosleep(zx_deadline_after(ZX_USEC(100)));

    val = !((buf->Read32(A5_ANACTRL_HIFIPLL_STS) >> 31) & 1);
  } while (val && --lock_check);

  if (lock_check == 0) {
    zxlogf(ERROR, "hifi_pll lock failed");
    return ZX_ERR_INVALID_ARGS;
  }

  zxlogf(INFO, "hifi_pll lock ok!");
  return ZX_OK;
}

zx_status_t Av400::AudioInit() {
  uint8_t tdm_instance_id = 1;
  zx_status_t status;

  HifiPllInit();
  status = InitAudioTop();
  if (status != ZX_OK)
    return status;

  // Av400 - tas5707 amplifier
  // There has a GPIOD_9 connected tas5707's RESET pin
  // RESET = 1, wait at least 13.5ms.
  gpio_impl_.SetAltFunction(A5_GPIOD(9), 0);  // RESET
  gpio_impl_.ConfigOut(A5_GPIOD(9), 0);
  zx::nanosleep(zx::deadline_after(zx::msec(1)));
  gpio_impl_.ConfigOut(A5_GPIOD(9), 1);
  zx::nanosleep(zx::deadline_after(zx::msec(15)));
  gpio_impl_.SetDriveStrength(A5_GPIOD(9), 2500, nullptr);

  // Av400 - tdmb - I2s
  // D613 SPK Board has 2x Tas5707 Codecs. (4 channels)
  // We use 1 codec for test here.
  constexpr uint64_t ua = 3000;
  // setup pinmux for tdmb arbiter.
  gpio_impl_.SetAltFunction(A5_GPIOC(2), A5_GPIOC_2_TDMB_FS_1_FN);  // LRCLK
  gpio_impl_.SetDriveStrength(A5_GPIOC(2), ua, nullptr);

  gpio_impl_.SetAltFunction(A5_GPIOC(3), A5_GPIOC_3_TDMB_SCLK_1_FN);  // SCLK
  gpio_impl_.SetDriveStrength(A5_GPIOC(3), ua, nullptr);

  gpio_impl_.SetAltFunction(A5_GPIOC(4), A5_GPIOC_4_MCLK_1_FN);  // MCLK
  gpio_impl_.SetDriveStrength(A5_GPIOC(4), ua, nullptr);

  gpio_impl_.SetAltFunction(A5_GPIOC(5),
                            A5_GPIOC_5_TDMB_D4_FN);  // OUT2 (D613 SPK Board - SPK_CH_01)
  gpio_impl_.SetDriveStrength(A5_GPIOC(5), ua, nullptr);

  // Av400 - tdma - I2S
  // Reference board has line-in interface. (ES7241 chip)
  // Support 1x I2S in
  gpio_impl_.SetAltFunction(A5_GPIOT(0), A5_GPIOT_0_TDMC_FS_2_FN);  // LRCLK2
  gpio_impl_.SetDriveStrength(A5_GPIOT(0), ua, nullptr);

  gpio_impl_.SetAltFunction(A5_GPIOT(1), A5_GPIOT_1_TDMC_SCLK_2_FN);  // SCLK2
  gpio_impl_.SetDriveStrength(A5_GPIOT(1), ua, nullptr);

  gpio_impl_.SetAltFunction(A5_GPIOT(2), A5_GPIOT_2_TDMC_D8_FN);  // IN0 - TDM_D8
  gpio_impl_.SetDriveStrength(A5_GPIOT(2), ua, nullptr);

  gpio_impl_.SetAltFunction(A5_GPIOT(6), A5_GPIOT_6_MCLK_2_FN);  // MCLK
  gpio_impl_.SetDriveStrength(A5_GPIOT(6), ua, nullptr);

  // Config I2S Codec
  zx_device_prop_t props[] = {{BIND_PLATFORM_DEV_VID, 0, PDEV_VID_TI},
                              {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_TI_TAS5707},
                              {BIND_CODEC_INSTANCE, 0, 1}};

  metadata::ti::TasConfig codec_config = {};
  codec_config.instance_count = 1;
  const device_metadata_t codec_metadata[] = {
      {
          .type = DEVICE_METADATA_PRIVATE,
          .data = reinterpret_cast<void*>(&codec_config),
          .length = sizeof(codec_config),
      },
  };

  composite_device_desc_t codec_desc = {};
  codec_desc.props = props;
  codec_desc.props_count = std::size(props);
  codec_desc.spawn_colocated = false;
  codec_desc.fragments = audio_tas5707_stereo_fragments;
  codec_desc.fragments_count = std::size(audio_tas5707_stereo_fragments);
  codec_desc.primary_fragment = "i2c";
  codec_desc.metadata_list = codec_metadata;
  codec_desc.metadata_count = std::size(codec_metadata);
  status = DdkAddComposite("audio-tas5707", &codec_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s DdkAddComposite failed %d", __FILE__, status);
    return status;
  }

  // Config Tdmout Playback Device
  metadata::AmlConfig metadata = {};
  snprintf(metadata.manufacturer, sizeof(metadata.manufacturer), "Amlogic");
  snprintf(metadata.product_name, sizeof(metadata.product_name), "av400");

  metadata.is_input = false;
  metadata.mClockDivFactor = 62;  // mclk = 768M / 62 = 12.19M (tas5707 limitation)
  metadata.sClockDivFactor = 4;   // sclk = 12.19M / 4 = 3.09M
  metadata.unique_id = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS;
  metadata.bus = metadata::AmlBus::TDM_B;

  metadata.is_custom_tdm_clk_sel = true;
  metadata.tdm_clk_sel = metadata::AmlTdmclk::CLK_A;  // you can select A ~ D
  metadata.is_custom_tdm_mpad_sel = true;
  metadata.mpad_sel = metadata::AmlTdmMclkPad::MCLK_PAD_1;  // mclk_pad1 <-> MCLK1 (A5_GPIOC_4)
  metadata.is_custom_tdm_spad_sel = true;
  metadata.spad_sel =
      metadata::AmlTdmSclkPad::SCLK_PAD_1;  // sclk/lrclk_pad1  <-> SCLK1/LRCLK1 (A5_GPIOC_2/3)
  metadata.dpad_mask = 1 << 0;
  metadata.dpad_sel[0] = metadata::AmlTdmDatPad::TDM_D4;  // lane0 <-> TDM_D4(A5_GPIOC_5)

  metadata.version = metadata::AmlVersion::kA5;
  metadata.dai.type = metadata::DaiType::I2s;
  metadata.codecs.number_of_codecs = 1;
  metadata.codecs.types[0] = metadata::CodecType::Tas5707;
  metadata.ring_buffer.number_of_channels = 2;
  metadata.swaps = 0x10;
  metadata.lanes_enable_mask[0] = 3;
  metadata.codecs.channels_to_use_bitmask[0] = 0x1;
  metadata.codecs.ring_buffer_channels_to_use_bitmask[0] = 0x3;

  pbus_metadata_t tdm_metadata[] = {
      {
          .type = DEVICE_METADATA_PRIVATE,
          .data_buffer = reinterpret_cast<uint8_t*>(&metadata),
          .data_size = sizeof(metadata),
      },
  };

  pbus_dev_t tdm_dev = {};
  char name[32];
  snprintf(name, sizeof(name), "av400-i2s-audio-out");
  tdm_dev.name = name;
  tdm_dev.vid = PDEV_VID_AMLOGIC;
  tdm_dev.pid = PDEV_PID_AMLOGIC_A5;
  tdm_dev.did = PDEV_DID_AMLOGIC_TDM;
  tdm_dev.instance_id = tdm_instance_id++;
  tdm_dev.mmio_list = audio_mmios;
  tdm_dev.mmio_count = std::size(audio_mmios);
  tdm_dev.bti_list = tdm_btis;
  tdm_dev.bti_count = std::size(tdm_btis);
  tdm_dev.irq_list = frddr_b_irqs;
  tdm_dev.irq_count = std::size(frddr_b_irqs);
  tdm_dev.metadata_list = tdm_metadata;
  tdm_dev.metadata_count = std::size(tdm_metadata);

  status = pbus_.CompositeDeviceAdd(&tdm_dev, reinterpret_cast<uint64_t>(tdm_i2s_fragments),
                                    std::size(tdm_i2s_fragments), nullptr);

  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: I2S CompositeDeviceAdd failed: %d", __FILE__, status);
    return status;
  }

  {
    // Config Tdmin Capture Device
    metadata::AmlConfig metadata = {};
    snprintf(metadata.manufacturer, sizeof(metadata.manufacturer), "Amlogic");
    snprintf(metadata.product_name, sizeof(metadata.product_name), "av400");

    metadata.is_input = true;
    metadata.mClockDivFactor = 62;  // mclk = 768M / 62 = 12.19M
    metadata.sClockDivFactor = 4;   // sclk = 12.19M / 4 = 3.09M
    metadata.bus = metadata::AmlBus::TDM_A;

    metadata.is_custom_tdm_clk_sel = true;
    metadata.tdm_clk_sel = metadata::AmlTdmclk::CLK_B;  // you can select A ~ D
    metadata.is_custom_tdm_mpad_sel = true;
    metadata.mpad_sel = metadata::AmlTdmMclkPad::MCLK_PAD_2;  // mclk_pad2 <-> MCLK2 (A5_GPIOT_6)
    metadata.is_custom_tdm_spad_sel = true;
    metadata.spad_sel =
        metadata::AmlTdmSclkPad::SCLK_PAD_2;  // sclk/lrclk_pad2  <-> SCLK2/LRCLK2 (A5_GPIOT_1/0)
    metadata.dpad_mask = 1 << 0;
    metadata.dpad_sel[0] = metadata::AmlTdmDatPad::TDM_D8;  // lane0 <-> TDM_D8(A5_GPIOT_2)

    metadata.version = metadata::AmlVersion::kA5;
    metadata.dai.type = metadata::DaiType::I2s;
    metadata.dai.bits_per_sample = 16;
    metadata.dai.bits_per_slot = 32;
    metadata.ring_buffer.number_of_channels = 2;
    metadata.swaps = 0x10;
    metadata.lanes_enable_mask[0] = 3;

    pbus_metadata_t tdm_metadata[] = {
        {
            .type = DEVICE_METADATA_PRIVATE,
            .data_buffer = reinterpret_cast<uint8_t*>(&metadata),
            .data_size = sizeof(metadata),
        },
    };

    pbus_dev_t tdm_dev = {};
    char name[32];
    snprintf(name, sizeof(name), "av400-i2s-audio-in");
    tdm_dev.name = name;
    tdm_dev.vid = PDEV_VID_AMLOGIC;
    tdm_dev.pid = PDEV_PID_AMLOGIC_A5;
    tdm_dev.did = PDEV_DID_AMLOGIC_TDM;
    tdm_dev.instance_id = tdm_instance_id++;
    tdm_dev.mmio_list = audio_mmios;
    tdm_dev.mmio_count = std::size(audio_mmios);
    tdm_dev.bti_list = tdm_in_btis;
    tdm_dev.bti_count = std::size(tdm_in_btis);
    tdm_dev.irq_list = toddr_a_irqs;
    tdm_dev.irq_count = std::size(toddr_a_irqs);
    tdm_dev.metadata_list = tdm_metadata;
    tdm_dev.metadata_count = std::size(tdm_metadata);

    status = pbus_.CompositeDeviceAdd(&tdm_dev, 0, 0, nullptr);
    if (status != ZX_OK) {
      zxlogf(ERROR, "CompositeDeviceAdd failed: %s", zx_status_get_string(status));
      return status;
    }
  }

  {
    // Av400 - d604_mic board has 6+1 mic (can record 4 channels pdm data)
    // DIN_1 connect 2x mic (AMIC3,4)
    // DIN_0 connect 2x mic (AMIC1,2)
    // DIN_3 connect 1x mic (AMIC7)
    // DIN_2 connect 2x mic (AMIC5,6)
    // For test, we use 2 channels here.
    gpio_impl_.SetAltFunction(A5_GPIOH(0), A5_GPIOH_0_PDMA_DIN_1_FN);
    gpio_impl_.SetAltFunction(A5_GPIOH(1), A5_GPIOH_1_PDMA_DIN_0_FN);
    gpio_impl_.SetAltFunction(A5_GPIOH(2), A5_GPIOH_2_PDMA_DCLK_FN);

    metadata::AmlPdmConfig metadata = {};
    snprintf(metadata.manufacturer, sizeof(metadata.manufacturer), "Amlogic");
    snprintf(metadata.product_name, sizeof(metadata.product_name), "av400");
    metadata.number_of_channels = 2;
    metadata.version = metadata::AmlVersion::kA5;
    metadata.sysClockDivFactor = 6;  // 770Mhz / 6   = 125Mhz
    metadata.dClockDivFactor = 250;  // 770Mhz / 250 = 3.072Mhz
    pbus_metadata_t pdm_metadata[] = {
        {
            .type = DEVICE_METADATA_PRIVATE,
            .data_buffer = reinterpret_cast<uint8_t*>(&metadata),
            .data_size = sizeof(metadata),
        },
    };

    pbus_dev_t pdm_dev = {};
    char pdm_name[32];
    snprintf(pdm_name, sizeof(pdm_name), "av400-pdm-audio-in");
    pdm_dev.name = pdm_name;
    pdm_dev.vid = PDEV_VID_AMLOGIC;
    pdm_dev.pid = PDEV_PID_AMLOGIC_A5;
    pdm_dev.did = PDEV_DID_AMLOGIC_PDM;
    pdm_dev.mmio_list = pdm_mmios;
    pdm_dev.mmio_count = std::size(pdm_mmios);
    pdm_dev.bti_list = pdm_btis;
    pdm_dev.bti_count = std::size(pdm_btis);
    // pdm use toddr_b by default; (src/media/audio/drivers/aml-g12-pdm/audio-stream-in.cc)
    pdm_dev.irq_list = toddr_b_irqs;
    pdm_dev.irq_count = std::size(toddr_b_irqs);
    pdm_dev.metadata_list = pdm_metadata;
    pdm_dev.metadata_count = std::size(pdm_metadata);

    status = pbus_.DeviceAdd(&pdm_dev);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s adding audio input device failed %d", __FILE__, status);
      return status;
    }
  }

  return ZX_OK;
}

}  // namespace av400

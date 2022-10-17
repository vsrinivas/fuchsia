// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/mmio/mmio.h>
#include <string.h>

#include <ddktl/metadata/audio.h>
#include <soc/aml-a5/a5-gpio.h>
#include <soc/aml-common/aml-audio.h>
#include <soc/aml-meson/a5-clk.h>
#include <ti/ti-audio.h>

#include "av400.h"

#ifdef TEST_CODEC
#include "src/devices/board/drivers/av400/audio-tas5707-stereo-bind.h"
#include "src/devices/board/drivers/av400/tdm-i2s-test-codec-bind.h"
#else
#include "src/devices/board/drivers/av400/tdm-i2s-bind.h"
#endif

#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace av400 {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> audio_mmios{
    {{
        .base = A5_EE_AUDIO_BASE,
        .length = A5_EE_AUDIO_LENGTH,
    }},
};

static const std::vector<fpbus::Bti> tdm_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_AUDIO_OUT,
    }},
};

static const std::vector<fpbus::Bti> tdm_in_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_AUDIO_IN,
    }},
};

static const std::vector<fpbus::Irq> frddr_b_irqs{
    {{
        .irq = A5_AUDIO_FRDDR_B,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const std::vector<fpbus::Irq> toddr_a_irqs{
    {{
        .irq = A5_AUDIO_TODDR_A,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const std::vector<fpbus::Mmio> pdm_mmios{
    {{
        .base = A5_EE_PDM_BASE,
        .length = A5_EE_PDM_LENGTH,
    }},
    {{
        .base = A5_EE_AUDIO_BASE,
        .length = A5_EE_AUDIO_LENGTH,
    }},
    {{
        .base = A5_EE_AUDIO2_BASE,
        .length = A5_EE_AUDIO2_LENGTH,
    }},
};
static const std::vector<fpbus::Bti> pdm_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_AUDIO_IN,
    }},
};

static const std::vector<fpbus::Irq> toddr_b_irqs{
    {{
        .irq = A5_AUDIO_TODDR_B,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
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

zx_status_t Av400::AudioInit() {
  uint8_t tdm_instance_id = 1;

  // For pdm
  uint32_t clock_id = a5_clk::CLK_HIFI_PLL;
  uint32_t clock_rate = 768'000'000;
  zx_status_t status = clk_impl_.Disable(clock_id);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Disable failed: %s", zx_status_get_string(status));
    return status;
  }

  status = clk_impl_.SetRate(clock_id, clock_rate);
  if (status != ZX_OK) {
    zxlogf(ERROR, "SetRate failed: %s", zx_status_get_string(status));
    return status;
  }

  status = clk_impl_.Enable(clock_id);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Enable failed: %s", zx_status_get_string(status));
    return status;
  }

  // For tdm in/out
  clock_id = a5_clk::CLK_MPLL0;
  clock_rate = 491'520'000;
  status = clk_impl_.Disable(clock_id);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Disable failed: %s", zx_status_get_string(status));
    return status;
  }

  status = clk_impl_.SetRate(clock_id, clock_rate);
  if (status != ZX_OK) {
    zxlogf(ERROR, "SetRate failed: %s", zx_status_get_string(status));
    return status;
  }

  status = clk_impl_.Enable(clock_id);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Enable failed: %s", zx_status_get_string(status));
    return status;
  }

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

#ifdef TEST_CODEC
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
#endif

  // Config Tdmout Playback Device
  metadata::AmlConfig metadata = {};
  snprintf(metadata.manufacturer, sizeof(metadata.manufacturer), "Amlogic");
  snprintf(metadata.product_name, sizeof(metadata.product_name), "av400");

  metadata.is_input = false;
  // Use mp0_pll as the MCLK source clock to make MCLK more accurate
  metadata.is_custom_tdm_src_clk_sel = true;
  metadata.mClockDivFactor = 40;  // mclk = 491'520'000 / 40 = 12'288'000 hz
  metadata.sClockDivFactor = 4;   // sclk = 12'288'000 / 4 = 3'072'000 hz
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
  metadata.dai.bits_per_sample = 16;
  metadata.dai.bits_per_slot = 32;
  metadata.ring_buffer.number_of_channels = 2;
  metadata.swaps = 0x10;
  metadata.lanes_enable_mask[0] = 3;
#ifdef TEST_CODEC
  metadata.codecs.number_of_codecs = 1;
  metadata.codecs.types[0] = metadata::CodecType::Tas5707;
  metadata.codecs.channels_to_use_bitmask[0] = 0x1;
  metadata.codecs.ring_buffer_channels_to_use_bitmask[0] = 0x3;
#endif

  std::vector<fpbus::Metadata> tdm_metadata{
      {{
          .type = DEVICE_METADATA_PRIVATE,
          .data =
              std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&metadata),
                                   reinterpret_cast<const uint8_t*>(&metadata) + sizeof(metadata)),
      }},
  };

  fpbus::Node tdm_dev;
  char name[32];
  snprintf(name, sizeof(name), "av400-i2s-audio-out");
  tdm_dev.name() = name;
  tdm_dev.vid() = PDEV_VID_AMLOGIC;
  tdm_dev.pid() = PDEV_PID_AMLOGIC_A5;
  tdm_dev.did() = PDEV_DID_AMLOGIC_TDM;
  tdm_dev.instance_id() = tdm_instance_id++;
  tdm_dev.mmio() = audio_mmios;
  tdm_dev.bti() = tdm_btis;
  tdm_dev.irq() = frddr_b_irqs;
  tdm_dev.metadata() = tdm_metadata;

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('AUDI');
  auto result = pbus_.buffer(arena)->AddCompositeImplicitPbusFragment(
      fidl::ToWire(fidl_arena, tdm_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, tdm_i2s_fragments,
                                               std::size(tdm_i2s_fragments)),
      {});
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Audio(tdm_dev) request failed: %s",
           __func__, result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Audio(tdm_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  {
    // Config Tdmin Capture Device
    metadata::AmlConfig metadata = {};
    snprintf(metadata.manufacturer, sizeof(metadata.manufacturer), "Amlogic");
    snprintf(metadata.product_name, sizeof(metadata.product_name), "av400");

    metadata.is_input = true;
    // Use mp0_pll as the MCLK source clock to make MCLK more accurate
    metadata.is_custom_tdm_src_clk_sel = true;
    metadata.mClockDivFactor = 40;  // mclk = 491'520'000 / 40 = 12'288'000 hz
    metadata.sClockDivFactor = 4;   // sclk = 12'288'000 / 4 = 3'072'000 hz
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

    std::vector<fpbus::Metadata> tdm_metadata{
        {{
            .type = DEVICE_METADATA_PRIVATE,
            .data = std::vector<uint8_t>(
                reinterpret_cast<const uint8_t*>(&metadata),
                reinterpret_cast<const uint8_t*>(&metadata) + sizeof(metadata)),
        }},
    };

    fpbus::Node tdm_dev;
    char name[32];
    snprintf(name, sizeof(name), "av400-i2s-audio-in");
    tdm_dev.name() = name;
    tdm_dev.vid() = PDEV_VID_AMLOGIC;
    tdm_dev.pid() = PDEV_PID_AMLOGIC_A5;
    tdm_dev.did() = PDEV_DID_AMLOGIC_TDM;
    tdm_dev.instance_id() = tdm_instance_id++;
    tdm_dev.mmio() = audio_mmios;
    tdm_dev.bti() = tdm_in_btis;
    tdm_dev.irq() = toddr_a_irqs;
    tdm_dev.metadata() = tdm_metadata;

    result = pbus_.buffer(arena)->AddCompositeImplicitPbusFragment(
        fidl::ToWire(fidl_arena, tdm_dev), {}, {});
    if (!result.ok()) {
      zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Audio(tdm_dev) request failed: %s",
             __func__, result.FormatDescription().data());
      return result.status();
    }
    if (result->is_error()) {
      zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Audio(tdm_dev) failed: %s", __func__,
             zx_status_get_string(result->error_value()));
      return result->error_value();
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
    std::vector<fpbus::Metadata> pdm_metadata{
        {{
            .type = DEVICE_METADATA_PRIVATE,
            .data = std::vector<uint8_t>(
                reinterpret_cast<const uint8_t*>(&metadata),
                reinterpret_cast<const uint8_t*>(&metadata) + sizeof(metadata)),
        }},
    };

    fpbus::Node pdm_dev;
    char pdm_name[32];
    snprintf(pdm_name, sizeof(pdm_name), "av400-pdm-audio-in");
    pdm_dev.name() = pdm_name;
    pdm_dev.vid() = PDEV_VID_AMLOGIC;
    pdm_dev.pid() = PDEV_PID_AMLOGIC_A5;
    pdm_dev.did() = PDEV_DID_AMLOGIC_PDM;
    pdm_dev.mmio() = pdm_mmios;
    pdm_dev.bti() = pdm_btis;
    // pdm use toddr_b by default; (src/media/audio/drivers/aml-g12-pdm/audio-stream-in.cc)
    pdm_dev.irq() = toddr_b_irqs;
    pdm_dev.metadata() = pdm_metadata;

    auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, pdm_dev));
    if (!result.ok()) {
      zxlogf(ERROR, "%s: NodeAdd Audio(pdm_dev) request failed: %s", __func__,
             result.FormatDescription().data());
      return result.status();
    }
    if (result->is_error()) {
      zxlogf(ERROR, "%s: NodeAdd Audio(pdm_dev) failed: %s", __func__,
             zx_status_get_string(result->error_value()));
      return result->error_value();
    }
  }

  return ZX_OK;
}

}  // namespace av400

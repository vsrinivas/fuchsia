// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fuchsia/hardware/gpio/c/banjo.h>
#include <fuchsia/hardware/shareddma/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <limits.h>

#include <ddktl/metadata/audio.h>
#include <fbl/algorithm.h>
#include <soc/as370/as370-clk.h>
#include <soc/as370/as370-gpio.h>
#include <soc/as370/as370-hw.h>
#include <soc/as370/as370-i2c.h>

#include "as370.h"
#include "src/devices/board/drivers/as370/as370-bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace board_as370 {
namespace fpbus = fuchsia_hardware_platform_bus;

static const zx_bind_inst_t ref_out_i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 0),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x31),
};
static const zx_bind_inst_t ref_out_codec_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CODEC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MAXIM),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_MAXIM_MAX98373),
};
static const zx_bind_inst_t dma_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SHARED_DMA),
};
static const zx_bind_inst_t ref_out_clk0_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, as370::As370Clk::kClkAvpll0),
};

static const device_fragment_part_t ref_out_i2c_fragment[] = {
    {std::size(ref_out_i2c_match), ref_out_i2c_match},
};
static const device_fragment_part_t ref_out_codec_fragment[] = {
    {std::size(ref_out_codec_match), ref_out_codec_match},
};
static const device_fragment_part_t dma_fragment[] = {
    {std::size(dma_match), dma_match},
};

static const zx_bind_inst_t ref_out_enable_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, 17),
};
static const device_fragment_part_t ref_out_enable_gpio_fragment[] = {
    {std::size(ref_out_enable_gpio_match), ref_out_enable_gpio_match},
};
static const device_fragment_part_t ref_out_clk0_fragment[] = {
    {std::size(ref_out_clk0_match), ref_out_clk0_match},
};

static const device_fragment_t codec_fragments[] = {
    {"i2c", std::size(ref_out_i2c_fragment), ref_out_i2c_fragment},
    {"gpio-enable", std::size(ref_out_enable_gpio_fragment), ref_out_enable_gpio_fragment},
};
static const device_fragment_t controller_fragments[] = {
    {"dma", std::size(dma_fragment), dma_fragment},
    {"codec", std::size(ref_out_codec_fragment), ref_out_codec_fragment},
    {"clock", std::size(ref_out_clk0_fragment), ref_out_clk0_fragment},
};
static const device_fragment_t in_fragments[] = {
    {"dma", std::size(dma_fragment), dma_fragment},
    {"clock", std::size(ref_out_clk0_fragment), ref_out_clk0_fragment},
};

zx_status_t As370::AudioInit() {
  static const std::vector<fpbus::Mmio> mmios_out{
      {{
          .base = as370::kGlobalBase,
          .length = as370::kGlobalSize,
      }},
      {{
          .base = as370::kAudioGlobalBase,
          .length = as370::kAudioGlobalSize,
      }},
      {{
          .base = as370::kAudioI2sBase,
          .length = as370::kAudioI2sSize,
      }},
  };

  fpbus::Node controller_out;
  controller_out.name() = "as370-audio-out";
  controller_out.vid() = PDEV_VID_SYNAPTICS;
  controller_out.pid() = PDEV_PID_SYNAPTICS_AS370;
  controller_out.did() = PDEV_DID_AS370_AUDIO_OUT;
  controller_out.mmio() = mmios_out;

  static const std::vector<fpbus::Mmio> mmios_in{
      {{
          .base = as370::kGlobalBase,
          .length = as370::kGlobalSize,
      }},
      {{
          .base = as370::kAudioGlobalBase,
          .length = as370::kAudioGlobalSize,
      }},
      {{
          .base = as370::kAudioI2sBase,
          .length = as370::kAudioI2sSize,
      }},
  };

  fpbus::Node dev_in;
  dev_in.name() = "as370-audio-in";
  dev_in.vid() = PDEV_VID_SYNAPTICS;
  dev_in.pid() = PDEV_PID_SYNAPTICS_AS370;
  dev_in.did() = PDEV_DID_AS370_AUDIO_IN;
  dev_in.mmio() = mmios_in;

  static const std::vector<fpbus::Mmio> mmios_dhub{
      {{
          .base = as370::kAudioDhubBase,
          .length = as370::kAudioDhubSize,
      }},
  };

  static const std::vector<fpbus::Irq> irqs_dhub{
      {{
          .irq = as370::kDhubIrq,
          .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
      }},
  };
  static const std::vector<fpbus::Bti> btis_dhub{
      {{
          .iommu_index = 0,
          .bti_id = BTI_AUDIO_DHUB,
      }},
  };

  fpbus::Node dhub;
  dhub.name() = "as370-dhub";
  dhub.vid() = PDEV_VID_SYNAPTICS;
  dhub.pid() = PDEV_PID_SYNAPTICS_AS370;
  dhub.did() = PDEV_DID_AS370_DHUB;
  dhub.mmio() = mmios_dhub;
  dhub.irq() = irqs_dhub;
  dhub.bti() = btis_dhub;

  // Output pin assignments.
  gpio_impl_.SetAltFunction(17, 0);  // AMP_EN, mode 0 to set as GPIO.
  gpio_impl_.ConfigOut(17, 0);

  // Input pin assignments.
  gpio_impl_.SetAltFunction(13, 1);  // mode 1 to set as PDM_CLKO.
  gpio_impl_.SetAltFunction(14, 1);  // mode 1 to set as PDM_DI[0].
  gpio_impl_.SetAltFunction(15, 1);  // mode 1 to set as PDM_DI[1].

  // DMA device.
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('AUDI');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, dhub));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd Audio(dhub) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd Audio(dhub) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  // Output devices.
  constexpr zx_device_prop_t props[] = {{BIND_PLATFORM_DEV_VID, 0, PDEV_VID_MAXIM},
                                        {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_MAXIM_MAX98373}};

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = std::size(props),
      .fragments = codec_fragments,
      .fragments_count = std::size(codec_fragments),
      .primary_fragment = "i2c",
      .spawn_colocated = false,
      .metadata_list = nullptr,
      .metadata_count = 0,
  };

  zx_status_t status = DdkAddComposite("audio-max98373", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s DdkAddComposite failed %d", __FILE__, status);
    return status;
  }

  // Share devhost with DHub.
  {
    auto result = pbus_.buffer(arena)->AddCompositeImplicitPbusFragment(
        fidl::ToWire(fidl_arena, controller_out),
        platform_bus_composite::MakeFidlFragment(fidl_arena, controller_fragments,
                                                 std::size(controller_fragments)),
        "dma");
    if (!result.ok()) {
      zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Audio(controller_out) request failed: %s",
             __func__, result.FormatDescription().data());
      return result.status();
    }
    if (result->is_error()) {
      zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Audio(controller_out) failed: %s",
             __func__, zx_status_get_string(result->error_value()));
      return result->error_value();
    }

    // Input device.
    // Share devhost with DHub.
    result = pbus_.buffer(arena)->AddCompositeImplicitPbusFragment(
        fidl::ToWire(fidl_arena, dev_in),
        platform_bus_composite::MakeFidlFragment(fidl_arena, in_fragments, std::size(in_fragments)),
        "dma");
    if (!result.ok()) {
      zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Audio(dev_in) request failed: %s",
             __func__, result.FormatDescription().data());
      return result.status();
    }
    if (result->is_error()) {
      zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Audio(dev_in) failed: %s", __func__,
             zx_status_get_string(result->error_value()));
      return result->error_value();
    }
  }
  return ZX_OK;
}

}  // namespace board_as370

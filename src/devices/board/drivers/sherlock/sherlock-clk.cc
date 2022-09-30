// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/clock.h>
#include <soc/aml-meson/g12b-clk.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"

namespace sherlock {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> clk_mmios{
    {{
        .base = T931_HIU_BASE,
        .length = T931_HIU_LENGTH,
    }},
    {{
        .base = T931_DOS_BASE,
        .length = T931_DOS_LENGTH,
    }},
    {{
        .base = T931_MSR_CLK_BASE,
        .length = T931_MSR_CLK_LENGTH,
    }},
};

static const clock_id_t clock_ids[] = {
    // For Camera Sensor.
    {g12b_clk::G12B_CLK_CAM_INCK_24M},
    // For cpu driver.
    {g12b_clk::G12B_CLK_SYS_PLL_DIV16},
    {g12b_clk::G12B_CLK_SYS_CPU_CLK_DIV16},
    {g12b_clk::G12B_CLK_SYS_PLLB_DIV16},
    {g12b_clk::G12B_CLK_SYS_CPUB_CLK_DIV16},
    {g12b_clk::CLK_SYS_CPU_BIG_CLK},
    {g12b_clk::CLK_SYS_CPU_LITTLE_CLK},
    // For video decoder/encoder
    {g12b_clk::G12B_CLK_DOS_GCLK_VDEC},
    {g12b_clk::G12B_CLK_DOS_GCLK_HCODEC},
    {g12b_clk::G12B_CLK_DOS},
};

static const std::vector<fpbus::Metadata> clock_metadata{
    {{
        .type = DEVICE_METADATA_CLOCK_IDS,
        .data =
            std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&clock_ids),
                                 reinterpret_cast<const uint8_t*>(&clock_ids) + sizeof(clock_ids)),
    }},
};

static const fpbus::Node clk_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "sherlock-clk";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.did() = PDEV_DID_AMLOGIC_G12B_CLK;
  dev.mmio() = clk_mmios;
  dev.metadata() = clock_metadata;
  return dev;
}();

zx_status_t Sherlock::ClkInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('CLK_');
  auto result = pbus_.buffer(arena)->ProtocolNodeAdd(ZX_PROTOCOL_CLOCK_IMPL,
                                                     fidl::ToWire(fidl_arena, clk_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: ProtocolNodeAdd Clk(clk_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: ProtocolNodeAdd Clk(clk_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  clk_impl_ = ddk::ClockImplProtocolClient(parent());
  if (!clk_impl_.is_valid()) {
    zxlogf(ERROR, "%s: ClockImplProtocolClient failed", __func__);
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

}  // namespace sherlock

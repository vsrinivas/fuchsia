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
#include <soc/aml-meson/g12a-clk.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro.h"

namespace astro {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> clk_mmios{
    {{
        .base = S905D2_HIU_BASE,
        .length = S905D2_HIU_LENGTH,
    }},
    {{
        .base = S905D2_DOS_BASE,
        .length = S905D2_DOS_LENGTH,
    }},
    {{
        .base = S905D2_MSR_CLK_BASE,
        .length = S905D2_MSR_CLK_LENGTH,
    }},
};

constexpr clock_id_t clock_ids[] = {
    // For CPU device.
    {g12a_clk::CLK_SYS_PLL_DIV16},
    {g12a_clk::CLK_SYS_CPU_CLK_DIV16},
    {g12a_clk::CLK_SYS_CPU_CLK},
    // For video decoder
    {g12a_clk::CLK_DOS_GCLK_VDEC},
    {g12a_clk::CLK_DOS},
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
  dev.name() = "astro-clk";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_S905D2;
  dev.did() = PDEV_DID_AMLOGIC_G12A_CLK;
  dev.mmio() = clk_mmios;
  dev.metadata() = clock_metadata;
  return dev;
}();

zx_status_t Astro::ClkInit() {
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

}  // namespace astro

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/fuse-config.h"

#include <lib/mmio/mmio.h>
#include <zircon/assert.h>

#include "lib/ddk/debug.h"
#include "src/graphics/display/drivers/intel-i915-tgl/pci-ids.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers.h"

namespace i915_tgl {

namespace {

FuseConfig ReadFuseConfigTigerLake(fdf::MmioBuffer& mmio_space) {
  auto dfsm_register = tgl_registers::DisplayFuses::Get().ReadFrom(&mmio_space);
  return FuseConfig{
      .core_clock_limit_khz = 652'800,  // No CDCLK limit fuses.
      .graphics_enabled = true,         // No global disable fuse.
      .pipe_enabled =
          {
              !dfsm_register.pipe_a_disabled(),
              !dfsm_register.pipe_b_disabled(),
              !dfsm_register.pipe_c_disabled(),
              !dfsm_register.pipe_d_disabled(),
          },
      .edp_enabled = !dfsm_register.edp_disabled(),
      .display_capture_enabled = !dfsm_register.display_capture_disabled(),
      .display_stream_compression_enabled = !dfsm_register.display_stream_compression_disabled(),
      .frame_buffer_compression_enabled = !dfsm_register.power_management_disabled(),
      .display_power_savings_enabled = !dfsm_register.power_management_disabled(),
  };
}

int CoreClockLimitKhzSkylake(tgl_registers::DisplayFuses::CoreClockLimit clock_limit) {
  static_assert(tgl_registers::DisplayFuses::CoreClockLimit::k675Mhz <
                tgl_registers::DisplayFuses::CoreClockLimit::k337_5Mhz);
  ZX_DEBUG_ASSERT_MSG(clock_limit >= tgl_registers::DisplayFuses::CoreClockLimit::k675Mhz,
                      "clock_limit should be a 2-bit field, but is %d", clock_limit);
  ZX_DEBUG_ASSERT_MSG(clock_limit <= tgl_registers::DisplayFuses::CoreClockLimit::k337_5Mhz,
                      "clock_limit should be a 2-bit field, but is %d", clock_limit);

  switch (clock_limit) {
    case tgl_registers::DisplayFuses::CoreClockLimit::k675Mhz:
      return 675'000;
    case tgl_registers::DisplayFuses::CoreClockLimit::k540Mhz:
      return 540'000;
    case tgl_registers::DisplayFuses::CoreClockLimit::k450Mhz:
      return 450'000;
    case tgl_registers::DisplayFuses::CoreClockLimit::k337_5Mhz:
      return 337'500;
  };

  // We should never get here, per the asserts above. If we do... returning the
  // most conservative limit minimizes the harm that may come from feeding this
  // result into clocking decisions.
  return 337'500;
}

FuseConfig ReadFuseConfigSkylake(fdf::MmioBuffer& mmio_space) {
  auto dfsm_register = tgl_registers::DisplayFuses::Get().ReadFrom(&mmio_space);
  return FuseConfig{
      .core_clock_limit_khz = CoreClockLimitKhzSkylake(dfsm_register.GetCoreClockLimit()),
      .graphics_enabled = !dfsm_register.graphics_disabled(),
      .pipe_enabled =
          {
              !dfsm_register.pipe_a_disabled(), !dfsm_register.pipe_b_disabled(),
              !dfsm_register.pipe_c_disabled(),
              false,  // No pipe D on these models.
          },
      .edp_enabled = !dfsm_register.edp_disabled(),
      .display_capture_enabled = !dfsm_register.display_capture_disabled(),
      .display_stream_compression_enabled = true,  // No DSC fuse.
      .frame_buffer_compression_enabled = !dfsm_register.power_management_disabled(),
      .display_power_savings_enabled = !dfsm_register.power_management_disabled(),
  };
}

}  // namespace

FuseConfig FuseConfig::ReadFrom(fdf::MmioBuffer& mmio_space, int device_id) {
  if (is_tgl(device_id))
    return ReadFuseConfigTigerLake(mmio_space);
  if (is_skl(device_id) || is_kbl(device_id))
    return ReadFuseConfigSkylake(mmio_space);

  if (is_test_device(device_id))
    return FuseConfig{};

  ZX_DEBUG_ASSERT_MSG(false, "Unsupported PCI device ID: %x", device_id);
}

void FuseConfig::Log() {
  if (!graphics_enabled)
    zxlogf(WARNING, "Unusual fuse state - Graphics disabled");

  for (size_t i = 0; i < std::size(pipe_enabled); ++i) {
    if (!pipe_enabled[i])
      zxlogf(WARNING, "Unusual fuse state - Pipe %zu disabled", i);
  }

  if (!edp_enabled)
    zxlogf(WARNING, "Unusual fuse state - eDP disabled");
  if (!display_capture_enabled)
    zxlogf(WARNING, "Unusual fuse state - WD (display capture) disabled");
  if (!display_stream_compression_enabled)
    zxlogf(WARNING, "Unusual fuse state - DSC disabled");
  if (!frame_buffer_compression_enabled)
    zxlogf(WARNING, "Unusual fuse state - FBC disabled");
  if (!display_power_savings_enabled)
    zxlogf(WARNING, "Unusual fuse state - DPST disabled");

  zxlogf(TRACE, "Maximum clock: %d kHz", core_clock_limit_khz);
}

}  // namespace i915_tgl

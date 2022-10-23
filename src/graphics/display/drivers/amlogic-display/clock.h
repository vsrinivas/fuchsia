// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_CLOCK_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_CLOCK_H_

#include <fuchsia/hardware/dsiimpl/c/banjo.h>
#include <lib/ddk/driver.h>
#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/result.h>
#include <unistd.h>
#include <zircon/compiler.h>

#include <optional>

#include <ddktl/device.h>
#include <hwreg/mmio.h>

#include "src/graphics/display/drivers/amlogic-display/common.h"
#include "src/graphics/display/drivers/amlogic-display/dsi.h"
#include "src/graphics/display/drivers/amlogic-display/hhi-regs.h"
#include "src/graphics/display/drivers/amlogic-display/vpu-regs.h"

namespace amlogic_display {

class Clock {
 public:
  // Map all necessary resources. This method does not change hardware state,
  // and is therefore safe to use when adopting a bootloader initialized device.
  static zx::result<std::unique_ptr<Clock>> Create(ddk::PDev& pdev, bool already_enabled);

  zx_status_t Enable(const display_setting_t& d);
  void Disable();
  void Dump();

  void SetVideoOn(bool on);

  // This is only safe to call when the clock is Enable'd.
  uint32_t GetBitrate() const {
    ZX_DEBUG_ASSERT(clock_enabled_);
    return pll_cfg_.bitrate;
  }

  static LcdTiming CalculateLcdTiming(const display_setting_t& disp_setting);
  // This function calculates the required pll configurations needed to generate
  // the desired lcd clock
  static zx::result<PllConfig> GenerateHPLL(const display_setting_t& disp_setting);

 private:
  // This function wait for hdmi_pll to lock. The retry algorithm is
  // undocumented and comes from U-Boot.
  zx_status_t PllLockWait();

  std::optional<fdf::MmioBuffer> vpu_mmio_;
  std::optional<fdf::MmioBuffer> hhi_mmio_;

  PllConfig pll_cfg_;
  LcdTiming lcd_timing_;
  display_setting_t last_valid_display_settings_ = {};

  bool clock_enabled_ = false;
};

}  // namespace amlogic_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_CLOCK_H_

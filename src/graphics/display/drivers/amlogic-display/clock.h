// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AMLOGIC_CLOCK_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AMLOGIC_CLOCK_H_

#include <fuchsia/hardware/dsiimpl/c/banjo.h>
#include <lib/ddk/driver.h>
#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/status.h>
#include <unistd.h>
#include <zircon/compiler.h>

#include <optional>

#include <ddktl/device.h>
#include <hwreg/mmio.h>

#include "dsi.h"
#include "common.h"
#include "hhi-regs.h"
#include "vpu-regs.h"

namespace amlogic_display {

class Clock {
 public:
  // Map all necessary resources. This method does not change hardware state,
  // and is therefore safe to use when adopting a bootloader initialized device.
  // Returns nullptr on failure.
  static zx::status<std::unique_ptr<Clock>> Create(ddk::PDev& pdev);

  zx_status_t Enable(const display_setting_t& d);
  void Disable();
  void Dump();

  // This is only safe to call when the clock is Enable'd.
  uint32_t GetBitrate() const {
    ZX_DEBUG_ASSERT(clock_enabled_);
    return pll_cfg_.bitrate;
  }

 private:
  void CalculateLcdTiming(const display_setting_t& disp_setting);

  // This function wait for hdmi_pll to lock. The retry algorithm is
  // undocumented and comes from U-Boot.
  zx_status_t PllLockWait();

  // This function calculates the required pll configurations needed to generate
  // the desired lcd clock
  zx_status_t GenerateHPLL(const display_setting_t& disp_setting);

  std::optional<ddk::MmioBuffer> vpu_mmio_;
  std::optional<ddk::MmioBuffer> hhi_mmio_;

  PllConfig pll_cfg_;
  LcdTiming lcd_timing_;

  bool clock_enabled_ = false;
};

}  // namespace amlogic_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AMLOGIC_CLOCK_H_

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AMLOGIC_CLOCK_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AMLOGIC_CLOCK_H_

#include <lib/device-protocol/platform-device.h>
#include <lib/mmio/mmio.h>
#include <unistd.h>
#include <zircon/compiler.h>

#include <optional>

#include <ddk/driver.h>
#include <ddk/protocol/dsiimpl.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <hwreg/mmio.h>

#include "aml-dsi.h"
#include "common.h"
#include "hhi-regs.h"
#include "vpu-regs.h"

namespace amlogic_display {

class AmlogicDisplayClock {
 public:
  AmlogicDisplayClock() {}
  zx_status_t Init(zx_device_t* parent);
  zx_status_t Enable(const display_setting_t& d);
  void Disable();
  void Dump();

  uint32_t GetBitrate() { return pll_cfg_.bitrate; }

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
  pdev_protocol_t pdev_ = {nullptr, nullptr};

  PllConfig pll_cfg_;
  LcdTiming lcd_timing_;

  bool initialized_ = false;
  bool clock_enabled_ = false;
};

}  // namespace amlogic_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AMLOGIC_CLOCK_H_

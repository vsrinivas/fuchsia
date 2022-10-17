// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_LCD_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_LCD_H_

#include <fuchsia/hardware/dsiimpl/cpp/banjo.h>
#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <lib/fit/function.h>
#include <unistd.h>
#include <zircon/compiler.h>

#include <fbl/alloc_checker.h>
#include <hwreg/mmio.h>

namespace amlogic_display {

// An Lcd controls the panel attached to a MIPI-DSI endpoint.
class Lcd {
 public:
  Lcd(uint32_t panel_type, fit::function<void(bool)> set_signal_power)
      : panel_type_(panel_type), set_signal_power_(std::move(set_signal_power)) {}

  // Create an Lcd to control the panel at `dsiimpl`. Panel type detection is
  // performed using `gpio`. If `already_enabled`, there will be no attempt to
  // power the LCD on or probe its panel type for correctness.
  // `set_signal_power(bool on)` will be called when the DSI should be turned on
  // or off.
  static zx::result<Lcd*> Create(fbl::AllocChecker* ac, uint32_t panel_type,
                                 cpp20::span<const uint8_t> dsi_on,
                                 cpp20::span<const uint8_t> dsi_off,
                                 fit::function<void(bool)> set_signal_power,
                                 ddk::DsiImplProtocolClient dsiimpl, ddk::GpioProtocolClient gpio,
                                 bool already_enabled);

  // Turn the panel on
  zx_status_t Enable();

  // Turn the panel off
  zx_status_t Disable();

  // Fetch the panel ID, storing it in the lower 24 bits of id_out. Assumes that
  // dsiimpl is in DSI_COMMAND_MODE.
  static zx_status_t GetDisplayId(ddk::DsiImplProtocolClient dsiimpl, uint32_t* id_out);

 private:
  zx_status_t LoadInitTable(cpp20::span<const uint8_t> buffer);
  // Print the display ID to the console.
  zx_status_t GetDisplayId();

  uint32_t panel_type_;
  fit::function<void(bool)> set_signal_power_;
  ddk::GpioProtocolClient gpio_;

  // Init and shutdown sequences for the fixed panel.
  cpp20::span<const uint8_t> dsi_on_;
  cpp20::span<const uint8_t> dsi_off_;
  ddk::DsiImplProtocolClient dsiimpl_;

  bool enabled_ = false;
};

}  // namespace amlogic_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_LCD_H_

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_PANEL_CONFIG_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_PANEL_CONFIG_H_

#include <zircon/types.h>

namespace amlogic_display {

// To simplify compatibility checks, DsiOpcode and PowerOpcode should match the
// AMLogic MIPI-DSI tuning guide.

enum DsiOpcode : uint8_t {
  // Turn the DSI phy off/on.
  // <op> <size=0>
  kDsiOpPhyPowerOff = 0x22,
  kDsiOpPhyPowerOn = 0x32,

  // Drive a GPIO pin.
  //
  // <op> <size=2|3> <gpio_id=0> <value> [delay_ms]
  kDsiOpGpio = 0xf0,

  // Attempt to read MIPI-DSI reg.
  //
  // <op> <size=2> <reg> <value!=0>
  kDsiOpReadReg = 0xfc,

  // Odd extended delay command to take several delays and gather them into
  // one big sleep. Behaves as an exit if byte 1 is 0xff or 0x0.
  //
  // <op> <size> <sleep_ms_1> <sleep_ms_2> ... <sleep_ms_N>
  kDsiOpDelay = 0xfd,

  // Simple sleep for N millis, or exit if N=0xff || N=0x0.
  //
  // <op> <sleep_ms>
  kDsiOpSleep = 0xff,

  // Everything else is potentially a DSI command.
};

enum PowerOpcode : uint8_t {
  // Drive a GPIO pin.
  kPowerOpGpio = 0,
  // Turn the device on/off.
  kPowerOpSignal = 2,
  // Wait for a GPIO input to reach a value.
  kPowerOpAwaitGpio = 4,
  kPowerOpExit = 0xff,
};

struct PowerOp {
  enum PowerOpcode op;
  uint8_t index;
  uint8_t value;
  uint8_t sleep_ms;
};

struct PanelConfig {
  const char* name;
  const cpp20::span<const uint8_t> dsi_on;
  const cpp20::span<const uint8_t> dsi_off;
  const cpp20::span<const PowerOp> power_on;
  const cpp20::span<const PowerOp> power_off;
};

}  // namespace amlogic_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_PANEL_CONFIG_H_

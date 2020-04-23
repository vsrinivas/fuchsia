// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_SYNAPTICS_GPIO_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_SYNAPTICS_GPIO_H_

#include <stdint.h>

namespace synaptics {

constexpr uint8_t kGpiosPerPort = 32;
constexpr uint8_t kMaxGpioPorts = 4;

struct PinmuxEntry {
  // The type of pin this entry represents. Some can be muxed but not used as GPIOs.
  enum : uint8_t {
    kInvalid = 0,
    kGpio = 1,
    kMuxOnly = 2,
  } type = kInvalid;
  // The index of the MMIO that is used for muxing this GPIO.
  uint8_t pinmux_mmio;
  // The index of the pinmux field in the MMIO, assuming kPinmuxPinsPerReg fields per 32-bit
  // register.
  uint8_t pinmux_index;
};

struct PinmuxMetadata {
  // The number of pinmux MMIOs the driver should expect. Any MMIOs after this will be interpreted
  // as GPIO ports. One interrupt is expected per port, and if there are fewer interrupts than ports
  // then it is assumed that the interrupts correspond to the first n ports. Specifying more
  // interrupts than ports will cause the driver to return an error.
  uint8_t muxes = 0;
  PinmuxEntry pinmux_map[kMaxGpioPorts * kGpiosPerPort] = {};
};

}  // namespace synaptics

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_SYNAPTICS_GPIO_H_

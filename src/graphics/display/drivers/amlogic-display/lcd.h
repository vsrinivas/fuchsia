// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_LCD_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_LCD_H_

#include <unistd.h>
#include <zircon/compiler.h>

#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/protocol/dsiimpl.h>
#include <hwreg/mmio.h>

namespace amlogic_display {

class Lcd {
 public:
  Lcd(uint32_t panel_type) : panel_type_(panel_type) {}

  zx_status_t Init(zx_device_t* dsi_dev, zx_device_t* gpio_dev);
  zx_status_t Enable();
  zx_status_t Disable();

 private:
  zx_status_t LoadInitTable(const uint8_t* buffer, size_t size);
  zx_status_t GetDisplayId();

  uint32_t panel_type_;
  gpio_protocol_t gpio_ = {};
  ddk::DsiImplProtocolClient dsiimpl_;

  bool initialized_ = false;
  bool enabled_ = false;
};

}  // namespace amlogic_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_LCD_H_

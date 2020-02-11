// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_ASTRO_DISPLAY_LCD_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_ASTRO_DISPLAY_LCD_H_

#include <unistd.h>
#include <zircon/compiler.h>

#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/protocol/dsiimpl.h>
#include <hwreg/mmio.h>

namespace astro_display {

class Lcd {
 public:
  Lcd(uint8_t panel_type) : panel_type_(panel_type) {}

  zx_status_t Init(zx_device_t* dsi_dev, zx_device_t* gpio_dev);
  zx_status_t Enable();
  zx_status_t Disable();

 private:
  zx_status_t LoadInitTable(const uint8_t* buffer, size_t size);
  zx_status_t GetDisplayId();

  uint8_t panel_type_;
  gpio_protocol_t gpio_ = {};
  ddk::DsiImplProtocolClient dsiimpl_;

  bool initialized_ = false;
  bool enabled_ = false;
};

}  // namespace astro_display

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_ASTRO_DISPLAY_LCD_H_

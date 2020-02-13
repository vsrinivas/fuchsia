// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_HIKEY_DISPLAY_ADV7533_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_HIKEY_DISPLAY_ADV7533_H_

#include "edid.h"
#include "hidisplay.h"

namespace hi_display {

struct Adv7533I2c {
  i2c_protocol_t i2c_main;
  i2c_protocol_t i2c_cec;
  i2c_protocol_t i2c_edid;
};

class Adv7533 {
 public:
  void Adv7533MainChannelWrite(uint8_t d1, uint8_t d2);
  void Adv7533MainChannelRead(uint8_t d1, uint8_t len);
  void Adv7533CecChannelWrite(uint8_t d1, uint8_t d2);
  void Adv7533EdidChannelRead(uint8_t d1, uint8_t len);
  zx_status_t Adv7533Init(pdev_protocol_t* pdev);

 private:
  void Adv7533EnableTestMode();
  void HdmiInit();
  void HdmiGpioInit();

  gpio_protocol_t gpios[GPIO_COUNT];
  Adv7533I2c i2c_dev;
  char write_buf_[60];
};
}  // namespace hi_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_HIKEY_DISPLAY_ADV7533_H_

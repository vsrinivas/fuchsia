// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_HIKEY_DISPLAY_HIDISPLAY_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_HIKEY_DISPLAY_HIDISPLAY_H_

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/i2c.h>
#include <lib/device-protocol/platform-device.h>

enum {
  GPIO_MUX,
  GPIO_PD,
  GPIO_INT,
  GPIO_COUNT,
};

struct DisplayTiming {
  uint16_t pixel_clk;
  uint16_t HActive;
  uint16_t HBlanking;
  uint16_t VActive;
  uint16_t VBlanking;
  uint16_t HSyncOffset;
  uint16_t HSyncPulseWidth;
  uint8_t VSyncOffset;
  uint8_t VSyncPulseWidth;
  uint16_t HImageSize;
  uint16_t VImageSize;
  uint8_t HBorder;
  uint8_t VBorder;
  uint8_t Flags;
  uint8_t align[9];
};

struct DetailedTiming {
  uint8_t raw_pixel_clk[2]; /* LSB first */
  uint8_t raw_Hact;
  uint8_t raw_HBlank;
  uint8_t raw_Hact_HBlank;
  uint8_t raw_Vact;
  uint8_t raw_VBlank;
  uint8_t raw_Vact_VBlank;
  uint8_t raw_HSyncOff;
  uint8_t raw_HSyncPW;
  uint8_t raw_VSyncOff_VSyncPW;
  uint8_t raw_HSync_VSync_OFF_PW;
  uint8_t raw_HImageSize;
  uint8_t raw_VImageSize;
  uint8_t raw_H_V_ImageSize;
  uint8_t raw_HBorder;
  uint8_t raw_VBorder;
  uint8_t raw_Flags;
};

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_HIKEY_DISPLAY_HIDISPLAY_H_

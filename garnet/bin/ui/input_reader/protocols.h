// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_PROTOCOLS_H_
#define GARNET_BIN_UI_INPUT_READER_PROTOCOLS_H_

#include <zircon/types.h>

namespace mozart {
enum class Protocol : uint32_t {
  Other,
  Keyboard,
  Mouse,
  Touch,
  Touchpad,
  Gamepad,
  Sensor,
  Stylus,
  LightSensor,
  MediaButtons,
  // The ones below are hacks that need to be removed.
  BootMouse,
  Acer12Touch,
  SamsungTouch,
  ParadiseV1Touch,
  ParadiseV2Touch,
  ParadiseV3Touch,
  EgalaxTouch,
  ParadiseV1TouchPad,
  ParadiseV2TouchPad,
  ParadiseSensor,
  EyoyoTouch,
  Ft3x27Touch,
};

enum class TouchDeviceType {
  NONE,
  HID,
  ACER12,
  PARADISEv1,
  PARADISEv2,
  PARADISEv3,
  SAMSUNG,
  EGALAX,
  EYOYO,
  FT3X27,
};

enum class MouseDeviceType {
  NONE,
  BOOT,
  HID,
  TOUCH,
  PARADISEv1,
  PARADISEv2,
  GAMEPAD
};

enum class SensorDeviceType {
  NONE,
  HID,
  PARADISE,
  AMBIENT_LIGHT,
};
}  // namespace mozart

#endif  // GARNET_BIN_UI_INPUT_READER_PROTOCOLS_H_

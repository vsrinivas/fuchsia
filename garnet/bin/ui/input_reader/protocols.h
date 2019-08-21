// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_PROTOCOLS_H_
#define GARNET_BIN_UI_INPUT_READER_PROTOCOLS_H_

#include <zircon/types.h>

namespace ui_input {
enum class Protocol : uint32_t {
  Other,
  Keyboard,
  Mouse,
  Pointer,
  Touch,
  Touchpad,
  Gamepad,
  Sensor,
  Stylus,
  LightSensor,
  MediaButtons,
  // The ones below are hacks that need to be removed.
  BootMouse,
  ParadiseSensor,
};

enum class TouchDeviceType {
  NONE,
  HID,
};

enum class MouseDeviceType { NONE, BOOT, HID, TOUCH, PARADISEv1, PARADISEv2, GAMEPAD };

enum class SensorDeviceType {
  NONE,
  HID,
  PARADISE,
  AMBIENT_LIGHT,
};
}  // namespace ui_input

#endif  // GARNET_BIN_UI_INPUT_READER_PROTOCOLS_H_

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_UI_INPUT_DEVICE_STATE_H_
#define SRC_LIB_UI_INPUT_DEVICE_STATE_H_

#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>
#include <stdint.h>

#include <vector>

#include <hid/hid.h>
#include <hid/usages.h>

#include "src/lib/fxl/memory/ref_counted.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace ui_input {

using OnEventCallback = fit::function<void(fuchsia::ui::input::InputEvent event)>;

// In contrast to keyboard and mouse devices, which require extra state to
// correctly interpret their data, sensor devices are simpler, so we just pass
// through the raw InputReport. We do need a device_id to understand which
// sensor the report came from.
using OnSensorEventCallback =
    fit::function<void(uint32_t device_id, fuchsia::ui::input::InputReport event)>;

using OnMediaButtonsEventCallback = fit::function<void(fuchsia::ui::input::InputReport event)>;

class DeviceState;

class State {
 public:
  void OnRegistered() {}
  void OnUnregistered() {}
};

class KeyboardState : public State {
 public:
  KeyboardState(DeviceState* device_state);
  void Update(fuchsia::ui::input::InputReport report);

 private:
  void SendEvent(fuchsia::ui::input::KeyboardEventPhase phase, uint32_t key, uint64_t modifiers,
                 uint64_t timestamp);
  void Repeat(uint64_t sequence);
  void ScheduleRepeat(uint64_t sequence, zx::duration delta);

  DeviceState* device_state_;
  const keychar_t* keymap_;  // assigned to a global static qwerty_map or dvorak_map

  std::vector<uint32_t> keys_;
  std::vector<uint32_t> repeat_keys_;
  uint64_t modifiers_ = 0;
  uint64_t repeat_sequence_ = 0;

  fxl::WeakPtrFactory<KeyboardState> weak_ptr_factory_;
};

class MouseState : public State {
 public:
  MouseState(DeviceState* device_state) : device_state_(device_state) {}
  void Update(fuchsia::ui::input::InputReport report, fuchsia::math::Size display_size);
  void OnRegistered();
  void OnUnregistered();

 private:
  void SendEvent(float rel_x, float rel_y, int64_t timestamp,
                 fuchsia::ui::input::PointerEventPhase phase, uint32_t buttons);

  DeviceState* device_state_;
  uint8_t buttons_ = 0;
  fuchsia::math::PointF position_;
};

class StylusState : public State {
 public:
  StylusState(DeviceState* device_state) : device_state_(device_state) {}
  void Update(fuchsia::ui::input::InputReport report, fuchsia::math::Size display_size);

 private:
  void SendEvent(int64_t timestamp, fuchsia::ui::input::PointerEventPhase phase,
                 fuchsia::ui::input::PointerEventType type, float x, float y, uint32_t buttons);

  DeviceState* device_state_;
  bool stylus_down_ = false;
  bool stylus_in_range_ = false;
  bool inverted_stylus_ = false;
  fuchsia::ui::input::PointerEvent stylus_;
};

class TouchscreenState : public State {
 public:
  TouchscreenState(DeviceState* device_state) : device_state_(device_state) {}
  void Update(fuchsia::ui::input::InputReport report, fuchsia::math::Size display_size);

 private:
  DeviceState* device_state_;
  std::vector<fuchsia::ui::input::PointerEvent> pointers_;
};

class SensorState : public State {
 public:
  SensorState(DeviceState* device_state) : device_state_(device_state) {}
  void Update(fuchsia::ui::input::InputReport report);

 private:
  DeviceState* device_state_;
  // TODO(fxbug.dev/23853): Remember sampling frequency and physical units.
};

class MediaButtonState : public State {
 public:
  MediaButtonState(DeviceState* device_state) : device_state_(device_state) {}
  void Update(fuchsia::ui::input::InputReport report);

 private:
  DeviceState* device_state_;
};

class DeviceState {
 public:
  DeviceState(uint32_t device_id, fuchsia::ui::input::DeviceDescriptor* descriptor,
              OnEventCallback callback);
  DeviceState(uint32_t device_id, fuchsia::ui::input::DeviceDescriptor* descriptor,
              OnSensorEventCallback callback);
  DeviceState(uint32_t device_id, fuchsia::ui::input::DeviceDescriptor* descriptor,
              OnMediaButtonsEventCallback callback);
  ~DeviceState();

  void OnRegistered();
  void OnUnregistered();

  void Update(fuchsia::ui::input::InputReport report, fuchsia::math::Size display_size);

  uint32_t device_id() { return device_id_; }
  const OnEventCallback& callback() { return callback_; }
  const OnSensorEventCallback& sensor_callback() { return sensor_callback_; }
  const OnMediaButtonsEventCallback& media_buttons_callback() { return media_buttons_callback_; }

  fuchsia::ui::input::KeyboardDescriptor* keyboard_descriptor() {
    return descriptor_->keyboard.get();
  }
  fuchsia::ui::input::MouseDescriptor* mouse_descriptor() { return descriptor_->mouse.get(); }
  fuchsia::ui::input::StylusDescriptor* stylus_descriptor() { return descriptor_->stylus.get(); }
  fuchsia::ui::input::TouchscreenDescriptor* touchscreen_descriptor() {
    return descriptor_->touchscreen.get();
  }
  fuchsia::ui::input::SensorDescriptor* sensor_descriptor() { return descriptor_->sensor.get(); }
  fuchsia::ui::input::MediaButtonsDescriptor* media_buttons_descriptor() {
    return descriptor_->media_buttons.get();
  }

 private:
  uint32_t device_id_;
  fuchsia::ui::input::DeviceDescriptor* descriptor_;

  KeyboardState keyboard_;
  MouseState mouse_;
  StylusState stylus_;
  TouchscreenState touchscreen_;
  OnEventCallback callback_;

  SensorState sensor_;
  OnSensorEventCallback sensor_callback_;

  OnMediaButtonsEventCallback media_buttons_callback_;
  MediaButtonState media_buttons_;
};

}  // namespace ui_input

#endif  // SRC_LIB_UI_INPUT_DEVICE_STATE_H_

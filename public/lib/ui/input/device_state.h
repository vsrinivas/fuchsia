// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_INPUT_DEVICE_STATE_H_
#define LIB_UI_INPUT_DEVICE_STATE_H_

#include <hid/hid.h>
#include <hid/usages.h>
#include <stdint.h>

#include <vector>

#include <fuchsia/cpp/geometry.h>
#include <fuchsia/cpp/input.h>
#include "lib/fxl/memory/ref_counted.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fsl/tasks/message_loop.h"

namespace mozart {

using OnEventCallback = std::function<void(input::InputEvent event)>;

class DeviceState;

class State {
 public:
  void OnRegistered() {}
  void OnUnregistered() {}
};

class KeyboardState : public State {
 public:
  KeyboardState(DeviceState* device_state);
  void Update(input::InputReportPtr report);

 private:
  void SendEvent(input::KeyboardEventPhase phase,
                 uint32_t key,
                 uint64_t modifiers,
                 uint64_t timestamp);
  void Repeat(uint64_t sequence);
  void ScheduleRepeat(uint64_t sequence, fxl::TimeDelta delta);

  DeviceState* device_state_;
  keychar_t* keymap_;  // assigned to a global static qwerty_map or dvorak_map
  fxl::WeakPtrFactory<KeyboardState> weak_ptr_factory_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;

  std::vector<uint32_t> keys_;
  std::vector<uint32_t> repeat_keys_;
  uint64_t modifiers_ = 0;
  uint64_t repeat_sequence_ = 0;
};

class MouseState : public State {
 public:
  MouseState(DeviceState* device_state) : device_state_(device_state) {}
  void Update(input::InputReportPtr report, geometry::Size display_size);
  void OnRegistered();
  void OnUnregistered();

 private:
  void SendEvent(float rel_x,
                 float rel_y,
                 int64_t timestamp,
                 input::PointerEventPhase phase,
                 uint32_t buttons);

  DeviceState* device_state_;
  uint8_t buttons_ = 0;
  geometry::PointF position_;
};

class StylusState : public State {
 public:
  StylusState(DeviceState* device_state) : device_state_(device_state) {}
  void Update(input::InputReportPtr report, geometry::Size display_size);

 private:
  void SendEvent(int64_t timestamp,
                 input::PointerEventPhase phase,
                 input::PointerEventType type,
                 float x,
                 float y,
                 uint32_t buttons);

  DeviceState* device_state_;
  bool stylus_down_ = false;
  bool stylus_in_range_ = false;
  bool inverted_stylus_ = false;
  input::PointerEvent stylus_;
};

class TouchscreenState : public State {
 public:
  TouchscreenState(DeviceState* device_state) : device_state_(device_state) {}
  void Update(input::InputReportPtr report, geometry::Size display_size);

 private:
  DeviceState* device_state_;
  std::vector<input::PointerEvent> pointers_;
};

class DeviceState {
 public:
  DeviceState(uint32_t device_id,
              input::DeviceDescriptor* descriptor,
              OnEventCallback callback);
  ~DeviceState();

  void OnRegistered();
  void OnUnregistered();

  void Update(input::InputReportPtr report, geometry::Size display_size);

  uint32_t device_id() { return device_id_; }
  OnEventCallback callback() { return callback_; }

  input::KeyboardDescriptor* keyboard_descriptor() {
    return descriptor_->keyboard.get();
  }
  input::MouseDescriptor* mouse_descriptor() {
    return descriptor_->mouse.get();
  }
  input::StylusDescriptor* stylus_descriptor() {
    return descriptor_->stylus.get();
  }
  input::TouchscreenDescriptor* touchscreen_descriptor() {
    return descriptor_->touchscreen.get();
  }

 private:
  uint32_t device_id_;
  input::DeviceDescriptor* descriptor_;
  OnEventCallback callback_;
  KeyboardState keyboard_;
  MouseState mouse_;
  StylusState stylus_;
  TouchscreenState touchscreen_;
};

}  // namespace mozart

#endif  // LIB_UI_INPUT_DEVICE_STATE_H_

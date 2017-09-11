// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_LIB_INPUT_DEVICE_STATE_H_
#define APPS_MOZART_LIB_INPUT_DEVICE_STATE_H_

#include <hid/hid.h>
#include <hid/usages.h>
#include <stdint.h>

#include <vector>

#include "lib/ui/input/fidl/input_events.fidl.h"
#include "lib/ui/input/fidl/input_device_registry.fidl.h"
#include "lib/ui/input/fidl/input_reports.fidl.h"
#include "lib/fxl/memory/ref_counted.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/mtl/tasks/message_loop.h"

namespace mozart {

using OnEventCallback = std::function<void(mozart::InputEventPtr event)>;

class DeviceState;

class State {
 public:
  void OnRegistered() {}
  void OnUnregistered() {}
};

class KeyboardState : public State {
 public:
  KeyboardState(DeviceState* device_state);
  void Update(mozart::InputReportPtr report);

 private:
  void SendEvent(mozart::KeyboardEvent::Phase phase,
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
  void Update(mozart::InputReportPtr report, mozart::Size display_size);
  void OnRegistered();
  void OnUnregistered();

 private:
  void SendEvent(float rel_x,
                 float rel_y,
                 int64_t timestamp,
                 mozart::PointerEvent::Phase phase,
                 uint32_t buttons);

  DeviceState* device_state_;
  uint8_t buttons_ = 0;
  mozart::PointF position_;
};

class StylusState : public State {
 public:
  StylusState(DeviceState* device_state) : device_state_(device_state) {}
  void Update(mozart::InputReportPtr report, mozart::Size display_size);

 private:
  void SendEvent(int64_t timestamp,
                 mozart::PointerEvent::Phase phase,
                 mozart::PointerEvent::Type type,
                 float x,
                 float y,
                 uint32_t buttons);

  DeviceState* device_state_;
  bool stylus_down_ = false;
  bool stylus_in_range_ = false;
  bool inverted_stylus_ = false;
  mozart::PointerEvent stylus_;
};

class TouchscreenState : public State {
 public:
  TouchscreenState(DeviceState* device_state) : device_state_(device_state) {}
  void Update(mozart::InputReportPtr report, mozart::Size display_size);

 private:
  DeviceState* device_state_;
  std::vector<mozart::PointerEvent> pointers_;
};

class DeviceState {
 public:
  DeviceState(uint32_t device_id,
              mozart::DeviceDescriptor* descriptor,
              OnEventCallback callback);
  ~DeviceState();

  void OnRegistered();
  void OnUnregistered();

  void Update(mozart::InputReportPtr report, mozart::Size display_size);

  uint32_t device_id() { return device_id_; }
  OnEventCallback callback() { return callback_; }

  mozart::KeyboardDescriptor* keyboard_descriptor() {
    return descriptor_->keyboard.get();
  }
  mozart::MouseDescriptor* mouse_descriptor() {
    return descriptor_->mouse.get();
  }
  mozart::StylusDescriptor* stylus_descriptor() {
    return descriptor_->stylus.get();
  }
  mozart::TouchscreenDescriptor* touchscreen_descriptor() {
    return descriptor_->touchscreen.get();
  }

 private:
  uint32_t device_id_;
  mozart::DeviceDescriptor* descriptor_;
  OnEventCallback callback_;
  KeyboardState keyboard_;
  MouseState mouse_;
  StylusState stylus_;
  TouchscreenState touchscreen_;
};

}  // namespace mozart

#endif  // APPS_MOZART_LIB_INPUT_DEVICE_STATE_H_

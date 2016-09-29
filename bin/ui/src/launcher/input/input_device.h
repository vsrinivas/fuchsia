// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_LAUNCHER_INPUT_INPUT_DEVICE_H_
#define APPS_MOZART_SRC_LAUNCHER_INPUT_INPUT_DEVICE_H_

#include <thread>
#include <vector>

#include <hid/hid.h>
#include <magenta/device/input.h>
#include <magenta/types.h>

#include "apps/mozart/services/input/interfaces/input_events.mojom.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/time/time_delta.h"

#define MAX_HANDLERS 127

#define MOD_LSHIFT (1 << 0)
#define MOD_RSHIFT (1 << 1)
#define MOD_LALT (1 << 2)
#define MOD_RALT (1 << 3)
#define MOD_LCTRL (1 << 4)
#define MOD_RCTRL (1 << 5)

#define MOD_SHIFT (MOD_LSHIFT | MOD_RSHIFT)
#define MOD_ALT (MOD_LALT | MOD_RALT)
#define MOD_CTRL (MOD_LCTRL | MOD_RCTRL)

namespace launcher {

using OnEventCallback = std::function<void(mozart::EventPtr event)>;

struct InputDevice {
  int fd_;
  mx_handle_t handle_;
  char name_[128];
  int protocol_;
  size_t report_desc_len_;
  uint8_t* report_desc_;
  size_t num_reports_;
  input_report_id_t* ids_;
  input_report_size_t max_report_len_;
  uint8_t* report_;

  InputDevice();
  virtual ~InputDevice();

  void Read(const OnEventCallback& callback, const mojo::Size& display_size);
  virtual void Parse(const OnEventCallback& callback,
                     const mojo::Size& display_size) = 0;

  static InputDevice* BuildInputDevice(int fd, const char* name);
};

struct KeyboardInputDevice : InputDevice {
  hid_keys_t key_state_[2];
  hid_keys_t key_delta_;
  int current_index_ = 0;
  int previous_index_ = 1;
  int modifiers_ = 0;
  keychar_t* keymap_;

  KeyboardInputDevice();
  void Parse(const OnEventCallback& callback, const mojo::Size& display_size);
};

struct MouseInputDevice : InputDevice {
  int32_t x_ = 0;
  int32_t y_ = 0;
  uint8_t buttons_ = 0;

  void Parse(const OnEventCallback& callback, const mojo::Size& display_size);
  void SendEvent(const OnEventCallback& callback,
                 int64_t timestamp,
                 mozart::EventType type,
                 mozart::EventFlags flags);
};

struct Acer12InputDevice : InputDevice {
  void Parse(const OnEventCallback& callback, const mojo::Size& display_size);

 private:
  void ParseStylus(const OnEventCallback& callback,
                   const mojo::Size& display_size);
  void ParseTouchscreen(const OnEventCallback& callback,
                        const mojo::Size& display_size);

  std::vector<mozart::PointerData> pointers_;
};

class InputDeviceMonitor {
 public:
  InputDeviceMonitor();
  ~InputDeviceMonitor();
  void Start();
  void CheckInput(const OnEventCallback& callback,
                  const mojo::Size& display_size);

 private:
  void DeviceAdded(int dirfd, const char* fn);

  std::thread monitor_thread_;
  std::vector<InputDevice*> devices_;
  std::mutex devices_lock_;

  mx_handle_t handles_[MAX_HANDLERS + 1];
  mx_signals_t wsigs_[MAX_HANDLERS + 1];
  mx_signals_state_t states_[MAX_HANDLERS + 1];

  //  ftl::RefPtr<ftl::TaskRunner> hid_task_runner_;
  FTL_DISALLOW_COPY_AND_ASSIGN(InputDeviceMonitor);
};
}

#endif  // APPS_MOZART_SRC_LAUNCHER_INPUT_INPUT_DEVICE_H_

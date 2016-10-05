// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_LAUNCHER_INPUT_INPUT_DEVICE_H_
#define APPS_MOZART_SRC_LAUNCHER_INPUT_INPUT_DEVICE_H_

#include <map>
#include <thread>
#include <vector>

#include <hid/hid.h>
#include <magenta/device/input.h>
#include <magenta/types.h>

#include "apps/mozart/services/input/interfaces/input_events.mojom.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/tasks/message_loop_handler.h"
#include "mojo/public/cpp/system/handle.h"

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
  // TODO(jpoichet) use mojo::ScopedEventHandle once available
  mojo::ScopedHandleBase<mojo::Handle> event_handle_;
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

  bool Read(const OnEventCallback& callback);
  virtual void Parse(const OnEventCallback& callback) = 0;

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
  void Parse(const OnEventCallback& callback);
};

struct MouseInputDevice : InputDevice {
  uint8_t buttons_ = 0;

  void Parse(const OnEventCallback& callback);
  void SendEvent(const OnEventCallback& callback,
                 float rel_x,
                 float rel_y,
                 int64_t timestamp,
                 mozart::EventType type,
                 mozart::EventFlags flags);
};

struct Acer12InputDevice : InputDevice {
  void Parse(const OnEventCallback& callback);

 private:
  void ParseStylus(const OnEventCallback& callback);
  void ParseTouchscreen(const OnEventCallback& callback);

  std::vector<mozart::PointerData> pointers_;
  bool stylus_down_ = false;
  mozart::PointerData stylus_;
};

class InputReader : mtl::MessageLoopHandler {
 public:
  InputReader();
  ~InputReader();
  void Start(const OnEventCallback& callback);

 private:
  InputDevice* OpenDevice(int dirfd, const char* fn);

  InputDevice* GetDevice(MojoHandle handle);
  void DeviceAdded(InputDevice* device);
  void DeviceRemoved(InputDevice* device);

  void OnDirectoryHandleReady(MojoHandle handle);
  void OnDeviceHandleReady(MojoHandle handle);

  // |mtl::MessageLoopHandler|:
  void OnHandleReady(MojoHandle handle);
  void OnHandleError(MojoHandle handle, MojoResult result);

  mtl::MessageLoop* main_loop_;
  mtl::MessageLoop::HandlerKey input_directory_key_;
  int input_directory_fd_;
  MojoHandle input_directory_handle_;

  std::map<MojoHandle, std::pair<InputDevice*, mtl::MessageLoop::HandlerKey>>
      devices_;
  OnEventCallback callback_;

  FTL_DISALLOW_COPY_AND_ASSIGN(InputReader);
};
}

#endif  // APPS_MOZART_SRC_LAUNCHER_INPUT_INPUT_DEVICE_H_

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_VIRTCON_KEYBOARD_H_
#define SRC_BRINGUP_BIN_VIRTCON_KEYBOARD_H_

#include <fidl/fuchsia.input.report/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/zx/timer.h>
#include <stdint.h>
#include <zircon/types.h>

#include <array>
#include <optional>

#include "vc.h"

#define MOD_LSHIFT (1 << 0)
#define MOD_RSHIFT (1 << 1)
#define MOD_LALT (1 << 2)
#define MOD_RALT (1 << 3)
#define MOD_LCTRL (1 << 4)
#define MOD_RCTRL (1 << 5)
#define MOD_CAPSLOCK (1 << 6)

#define MOD_SHIFT (MOD_LSHIFT | MOD_RSHIFT)
#define MOD_ALT (MOD_LALT | MOD_RALT)
#define MOD_CTRL (MOD_LCTRL | MOD_RCTRL)

// Global function which sets up the global keyboard watcher.
zx_status_t setup_keyboard_watcher(async_dispatcher_t* dispatcher, keypress_handler_t handler,
                                   bool repeat_keys);

// A |Keyboard| is created with a callback to handle keypresses.
// The Keyboard is responsible for watching the keyboard device, parsing
// events, handling key-repeats/modifiers, and sending keypresses to
// the |keypress_handler_t|.
//
// If |Setup| is to be used, |Keyboard| must be allocated with `new`, and will
// own itself. When errors occur during watching, it will attempt to reconnect
// using the provider server `fuchsia.input.report/InputDevice`. |Keyboard|
// deletes itself if reconnection fails.
class Keyboard : public fidl::WireAsyncEventHandler<fuchsia_input_report::InputReportsReader> {
 public:
  Keyboard(async_dispatcher_t* dispatcher, keypress_handler_t handler, bool repeat_keys)
      : dispatcher_(dispatcher), handler_(handler), repeat_enabled_(repeat_keys) {}

  // Have the keyboard start watching a given device.
  // |caller| represents the keyboard device.
  zx_status_t Setup(fidl::WireSyncClient<fuchsia_input_report::InputDevice> keyboard_client);

  // Process a given set of keys and send them to the handler.
  void ProcessInput(const fuchsia_input_report::wire::InputReport& report);

 private:
  // The callback for when key-repeat is triggered.
  void TimerCallback(async_dispatcher_t* dispatcher, async::TaskBase* task, zx_status_t status);
  void InputCallback(fuchsia_input_report::wire::InputReportsReaderReadInputReportsResult result);

  // Handle error in |reader_client_|.
  void on_fidl_error(::fidl::UnbindInfo info) override;

  // Attempt to connect to an InputReportsReader and start a ReadInputReports call.
  zx_status_t StartReading();

  // Send a report to the device that enables/disables the capslock LED.
  void SetCapsLockLed(bool caps_lock);

  async_dispatcher_t* dispatcher_;
  async::TaskMethod<Keyboard, &Keyboard::TimerCallback> timer_task_{this};

  keypress_handler_t handler_ = {};

  zx::duration repeat_interval_ = zx::duration::infinite();
  std::optional<fidl::WireSyncClient<fuchsia_input_report::InputDevice>> keyboard_client_;
  fidl::WireSharedClient<fuchsia_input_report::InputReportsReader> reader_client_;

  int modifiers_ = 0;
  bool repeat_enabled_ = true;
  bool is_repeating_ = false;
  uint8_t repeating_keycode_;
  std::array<fuchsia_input::wire::Key, fuchsia_input_report::wire::kKeyboardMaxPressedKeys>
      last_pressed_keys_;
  size_t last_pressed_keys_size_ = 0;
};

// A |KeyboardWatcher| opens a directory and will watch for new input devices.
// It will create a |Keyboard| for each input device that is a keyboard.
class KeyboardWatcher {
 public:
  zx_status_t Setup(async_dispatcher_t* dispatcher, keypress_handler_t handler, bool repeat_keys);

 private:
  // Callback when a new file is created in the directory.
  void DirCallback(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                   const zx_packet_signal_t* signal);

  // Attempts to open the file and create a new Keyboard.
  zx_status_t OpenFile(uint8_t evt, char* name);

  // The channel representing the directory this is watching.
  fidl::UnownedClientEnd<fuchsia_io::Directory> Directory() { return dir_caller_.directory(); }

  bool repeat_keys_ = true;
  keypress_handler_t handler_ = {};

  fdio_cpp::FdioCaller dir_caller_;
  async_dispatcher_t* dispatcher_ = nullptr;
  async::WaitMethod<KeyboardWatcher, &KeyboardWatcher::DirCallback> dir_wait_{this};
};

#endif  // SRC_BRINGUP_BIN_VIRTCON_KEYBOARD_H_

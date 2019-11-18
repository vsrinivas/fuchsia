// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_VIRTCON_KEYBOARD_H_
#define ZIRCON_SYSTEM_CORE_VIRTCON_KEYBOARD_H_

#include <fuchsia/io/c/fidl.h>
#include <lib/fzl/fdio.h>
#include <lib/zx/timer.h>
#include <stdint.h>
#include <zircon/types.h>

#include <array>

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
zx_status_t setup_keyboard_watcher(keypress_handler_t handler, bool repeat_keys);

// A |Keyboard| is created with a callback to handle keypresses.
// The Keyboard is responsible for watching the keyboard device, parsing
// events, handling key-repeats/modifiers, and sending keypresses to
// the |keypress_handler_t|.
class Keyboard {
 public:
  Keyboard(keypress_handler_t handler, bool repeat_keys)
      : handler_(handler), repeat_enabled_(repeat_keys) {}
  ~Keyboard();

  // Have the keyboard start watching a given device.
  // |caller| represents the keyboard device.
  zx_status_t Setup(fzl::FdioCaller caller);

  // Process a given set of keys and send them to the handler.
  void ProcessInput(hid_keys_t state);

 private:
  // The callback for when key-repeat is triggered.
  zx_status_t TimerCallback(zx_signals_t signals, uint32_t evt);
  // The callback for when the device has a new input event.
  zx_status_t InputCallback(unsigned pollevt, uint32_t evt);
  // Send a report to the device that enables/disables the capslock LED.
  void SetCapsLockLed(bool caps_lock);

  fzl::FdioCaller caller_;
  port_fd_handler_t input_notifier_ = {};
  port_handler_t timer_notifier_ = {};
  zx::timer timer_;

  keypress_handler_t handler_ = {};

  hid_keys_t previous_state_ = {};
  int modifiers_ = 0;

  bool repeat_enabled_ = true;
  bool is_repeating_ = false;
  uint8_t repeating_key_ = 0;
  zx::duration repeat_interval_ = zx::duration::infinite();
};

// A |KeyboardWatcher| opens a directory and will watch for new input devices.
// It will create a |Keyboard| for each input device that is a keyboard.
class KeyboardWatcher {
 public:
  zx_status_t Setup(keypress_handler_t handler, bool repeat_keys);

  // Callback when a new file is created in the directory.
  zx_status_t DirCallback(port_handler_t* ph, zx_signals_t signals, uint32_t evt);

 private:
  // Attempts to open the file and create a new Keyboard.
  zx_status_t OpenFile(uint8_t evt, char* name);

  // The Fd() representing the directory this is watching.
  int Fd() { return dir_caller_.fd().get(); }

  bool repeat_keys_ = true;
  keypress_handler_t handler_ = {};

  fzl::FdioCaller dir_caller_;
  port_handler_t dir_handler_ = {};
};

#endif  // ZIRCON_SYSTEM_CORE_VIRTCON_KEYBOARD_H_

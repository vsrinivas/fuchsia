// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "keyboard.h"

#include <fcntl.h>
#include <fuchsia/hardware/input/c/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/watcher.h>
#include <lib/fzl/fdio.h>
#include <lib/zx/channel.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/syscalls.h>

#include <utility>

#include <hid/hid.h>
#include <hid/usages.h>

namespace {

constexpr zx::duration kSlackDuration = zx::msec(1);
constexpr zx::duration kHighRepeatKeyFreq = zx::msec(50);
constexpr zx::duration kLowRepeatKeyFreq = zx::msec(250);

// TODO(dgilhooley): this global watcher is necessary because the ports we are using
// take a raw function pointer. I think once we move this library to libasync we can
// remove the global watcher and use lambdas.
KeyboardWatcher main_watcher;

zx_status_t keyboard_main_callback(port_handler_t* ph, zx_signals_t signals, uint32_t evt) {
  return main_watcher.DirCallback(ph, signals, evt);
}

int modifiers_from_keycode(uint8_t keycode) {
  switch (keycode) {
    case HID_USAGE_KEY_LEFT_SHIFT:
      return MOD_LSHIFT;
    case HID_USAGE_KEY_RIGHT_SHIFT:
      return MOD_RSHIFT;
    case HID_USAGE_KEY_LEFT_ALT:
      return MOD_LALT;
    case HID_USAGE_KEY_RIGHT_ALT:
      return MOD_RALT;
    case HID_USAGE_KEY_LEFT_CTRL:
      return MOD_LCTRL;
    case HID_USAGE_KEY_RIGHT_CTRL:
      return MOD_RCTRL;
  }
  return 0;
}

bool keycode_is_modifier(uint8_t keycode) { return modifiers_from_keycode(keycode) != 0; }

}  // namespace

zx_status_t setup_keyboard_watcher(keypress_handler_t handler, bool repeat_keys) {
  return main_watcher.Setup(handler, repeat_keys);
}

zx_status_t Keyboard::TimerCallback(zx_signals_t signals, uint32_t evt) {
  handler_(repeating_key_, modifiers_);

  // increase repeat rate if we're not yet at the fastest rate
  if ((repeat_interval_ = repeat_interval_ * 3 / 4) < kHighRepeatKeyFreq) {
    repeat_interval_ = kHighRepeatKeyFreq;
  }

  timer_.set(zx::deadline_after(repeat_interval_), kSlackDuration);

  return ZX_OK;
}

void Keyboard::SetCapsLockLed(bool caps_lock) {
  if (!caller_) {
    return;
  }
  // The following bit to set is specified in "Device Class Definition
  // for Human Interface Devices (HID)", Version 1.11,
  // http://www.usb.org/developers/hidpage/HID1_11.pdf.  Zircon leaves
  // USB keyboards in boot mode, so the relevant section is Appendix B,
  // "Boot Interface Descriptors", "B.1 Protocol 1 (Keyboard)".
  const uint8_t kUsbCapsLockBit = 1 << 1;
  const uint8_t report_body[1] = {static_cast<uint8_t>(caps_lock ? kUsbCapsLockBit : 0)};

  zx_status_t call_status;
  zx_status_t status = fuchsia_hardware_input_DeviceSetReport(
      caller_.borrow_channel(), fuchsia_hardware_input_ReportType_OUTPUT, 0, report_body,
      sizeof(report_body), &call_status);
  if (status != ZX_OK || call_status != ZX_OK) {
    printf("fuchsia.hardware.input.Device.SetReport() failed (returned %d, %d)\n", status,
           call_status);
  }
}

// returns true if key was pressed and none were released
void Keyboard::ProcessInput(hid_keys_t state) {
  // Process the pressed keys.
  uint8_t keycode;
  hid_keys_t keys;
  hid_kbd_pressed_keys(&previous_state_, &state, &keys);
  hid_for_every_key(&keys, keycode) {
    if (keycode == HID_USAGE_KEY_ERROR_ROLLOVER) {
      return;
    }
    modifiers_ |= modifiers_from_keycode(keycode);
    if (keycode == HID_USAGE_KEY_CAPSLOCK) {
      modifiers_ ^= MOD_CAPSLOCK;
      SetCapsLockLed(modifiers_ & MOD_CAPSLOCK);
    }

    if (repeat_enabled_ && !keycode_is_modifier(keycode)) {
      is_repeating_ = true;
      repeating_key_ = keycode;
      repeat_interval_ = kLowRepeatKeyFreq;
      timer_.cancel();
      timer_.set(zx::deadline_after(repeat_interval_), kSlackDuration);
    }
    handler_(keycode, modifiers_);
  }

  // Process the released keys.
  hid_kbd_released_keys(&previous_state_, &state, &keys);
  hid_for_every_key(&keys, keycode) {
    modifiers_ &= ~modifiers_from_keycode(keycode);

    if (repeat_enabled_ && is_repeating_ && (repeating_key_ == keycode)) {
      is_repeating_ = false;
      repeating_key_ = 0;
      timer_.cancel();
    }
  }

  // Store the previous state.
  previous_state_ = state;
}

Keyboard::~Keyboard() {
  if (input_notifier_.fdio_context != nullptr) {
    port_fd_handler_done(&input_notifier_);
  }
  if (timer_notifier_.func != nullptr) {
    port_cancel(&port, &timer_notifier_);
  }
}

zx_status_t Keyboard::InputCallback(unsigned pollevt, uint32_t evt) {
  if (!(pollevt & POLLIN)) {
    return ZX_ERR_STOP;
  }

  uint8_t report[8];
  ssize_t r = read(caller_.fd().get(), report, sizeof(report));
  if (r <= 0) {
    return ZX_ERR_STOP;
  }
  if ((size_t)(r) != sizeof(report)) {
    repeat_interval_ = zx::duration::infinite();
    return ZX_OK;
  }

  hid_keys_t state = {};
  hid_kbd_parse_report(report, &state);

  ProcessInput(state);
  return ZX_OK;
}

zx_status_t Keyboard::Setup(fzl::FdioCaller caller) {
  caller_ = std::move(caller);

  zx_status_t status;
  if ((status = zx::timer::create(ZX_TIMER_SLACK_LATE, ZX_CLOCK_MONOTONIC, &timer_)) != ZX_OK) {
    return status;
  }

  input_notifier_.func = [](port_fd_handler* input_notifier, unsigned pollevt, uint32_t evt) {
    Keyboard* kbd = containerof(input_notifier, Keyboard, input_notifier_);
    zx_status_t status = kbd->InputCallback(pollevt, evt);
    if (status == ZX_ERR_STOP) {
      delete kbd;
    }
    return status;
  };

  if ((status = port_fd_handler_init(&input_notifier_, caller_.fd().get(),
                                     POLLIN | POLLHUP | POLLRDHUP)) < 0) {
    return status;
  }

  if ((status = port_wait(&port, &input_notifier_.ph)) != ZX_OK) {
    port_fd_handler_done(&input_notifier_);
    return status;
  }

  timer_notifier_.handle = timer_.get();
  timer_notifier_.waitfor = ZX_TIMER_SIGNALED;
  timer_notifier_.func = [](port_handler_t* ph, zx_signals_t signals, uint32_t evt) {
    Keyboard* kbd = containerof(ph, Keyboard, timer_notifier_);
    return kbd->TimerCallback(signals, evt);
  };
  port_wait(&port, &timer_notifier_);
  return ZX_OK;
}

zx_status_t KeyboardWatcher::OpenFile(uint8_t evt, char* name) {
  if ((evt != fuchsia_io_WATCH_EVENT_EXISTING) && (evt != fuchsia_io_WATCH_EVENT_ADDED)) {
    return ZX_OK;
  }

  int fd;
  if ((fd = openat(Fd(), name, O_RDONLY)) < 0) {
    return ZX_OK;
  }

  fzl::FdioCaller caller = fzl::FdioCaller(fbl::unique_fd(fd));
  uint32_t proto = fuchsia_hardware_input_BootProtocol_NONE;
  zx_status_t status =
      fuchsia_hardware_input_DeviceGetBootProtocol(caller.borrow_channel(), &proto);
  // skip devices that aren't keyboards
  if ((status != ZX_OK) || (proto != fuchsia_hardware_input_BootProtocol_KBD)) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  printf("vc: new input device /dev/class/input/%s\n", name);

  // This is not a memory leak, because keyboards free themselves when their underlying
  // devices close.
  Keyboard* keyboard = new Keyboard(handler_, repeat_keys_);
  status = keyboard->Setup(std::move(caller));
  if (status != ZX_OK) {
    delete keyboard;
    return status;
  }
  return ZX_OK;
}

zx_status_t KeyboardWatcher::DirCallback(port_handler_t* ph, zx_signals_t signals, uint32_t evt) {
  if (!(signals & ZX_CHANNEL_READABLE)) {
    printf("vc: device directory died\n");
    return ZX_ERR_STOP;
  }

  // Buffer contains events { Opcode, Len, Name[Len] }
  // See zircon/device/vfs.h for more detail
  // extra byte is for temporary NUL
  std::array<uint8_t, fuchsia_io_MAX_BUF + 1> buffer;
  uint32_t len;
  if (zx_channel_read(ph->handle, 0, buffer.data(), NULL, buffer.size() - 1, 0, &len, NULL) < 0) {
    printf("vc: failed to read from device directory\n");
    return ZX_ERR_STOP;
  }

  uint32_t index = 0;
  while (index + 2 <= len) {
    uint8_t event = buffer[index];
    uint8_t namelen = buffer[index + 1];
    index += 2;
    if ((namelen + index) > len) {
      printf("vc: malformed device directory message\n");
      return ZX_ERR_STOP;
    }
    // add temporary nul
    uint8_t tmp = buffer[index + namelen];
    buffer[index + namelen] = 0;
    OpenFile(event, reinterpret_cast<char*>(&buffer[index]));
    buffer[index + namelen] = tmp;
    index += namelen;
  }
  return ZX_OK;
}

zx_status_t KeyboardWatcher::Setup(keypress_handler_t handler, bool repeat_keys) {
  repeat_keys_ = repeat_keys;
  handler_ = handler;
  fbl::unique_fd fd(open("/dev/class/input", O_DIRECTORY | O_RDONLY));
  if (!fd) {
    return ZX_ERR_IO;
  }
  zx::channel client, server;
  zx_status_t status;
  if ((status = zx::channel::create(0, &client, &server)) != ZX_OK) {
    return status;
  }

  dir_caller_ = fzl::FdioCaller(std::move(fd));
  zx_status_t io_status = fuchsia_io_DirectoryWatch(
      dir_caller_.borrow_channel(), fuchsia_io_WATCH_MASK_ALL, 0, server.release(), &status);
  if (io_status != ZX_OK || status != ZX_OK) {
    return io_status;
  }

  dir_handler_.handle = client.release();
  dir_handler_.waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
  dir_handler_.func = keyboard_main_callback;
  port_wait(&port, &dir_handler_);

  return ZX_OK;
}

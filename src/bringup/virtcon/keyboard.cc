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

#include <ddk/device.h>
#include <hid/hid.h>
#include <hid/usages.h>

#include "src/ui/lib/key_util/key_util.h"

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

int modifiers_from_fuchsia_key(llcpp::fuchsia::ui::input2::Key key) {
  switch (key) {
    case llcpp::fuchsia::ui::input2::Key::LEFT_SHIFT:
      return MOD_LSHIFT;
    case llcpp::fuchsia::ui::input2::Key::RIGHT_SHIFT:
      return MOD_RSHIFT;
    case llcpp::fuchsia::ui::input2::Key::LEFT_ALT:
      return MOD_LALT;
    case llcpp::fuchsia::ui::input2::Key::RIGHT_ALT:
      return MOD_RALT;
    case llcpp::fuchsia::ui::input2::Key::LEFT_CTRL:
      return MOD_LCTRL;
    case llcpp::fuchsia::ui::input2::Key::RIGHT_CTRL:
      return MOD_RCTRL;
    default:
      return 0;
  }
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

bool is_key_in_set(llcpp::fuchsia::ui::input2::Key key,
                   fidl::VectorView<llcpp::fuchsia::ui::input2::Key> set) {
  for (auto s : set) {
    if (key == s) {
      return true;
    }
  }
  return false;
}

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
  if (!keyboard_client_) {
    return;
  }

  // Generate the OutputReport.
  auto report_builder = llcpp::fuchsia::input::report::OutputReport::Build();
  auto keyboard_report_builder = llcpp::fuchsia::input::report::KeyboardOutputReport::Build();
  fidl::VectorView<llcpp::fuchsia::input::report::LedType> led_view;
  llcpp::fuchsia::input::report::LedType caps_led =
      llcpp::fuchsia::input::report::LedType::CAPS_LOCK;

  if (caps_lock) {
    led_view = fidl::VectorView<llcpp::fuchsia::input::report::LedType>(&caps_led, 1);
  } else {
    led_view = fidl::VectorView<llcpp::fuchsia::input::report::LedType>(nullptr, 0);
  }

  keyboard_report_builder.set_enabled_leds(&led_view);
  llcpp::fuchsia::input::report::KeyboardOutputReport keyboard_report =
      keyboard_report_builder.view();
  report_builder.set_keyboard(&keyboard_report);

  llcpp::fuchsia::input::report::InputDevice::ResultOf::SendOutputReport result =
      keyboard_client_->SendOutputReport(report_builder.view());
}

// returns true if key was pressed and none were released
void Keyboard::ProcessInput(const ::llcpp::fuchsia::input::report::InputReport& report) {
  if (!report.has_keyboard()) {
    return;
  }
  // Check if the keyboard FIDL table contains the pressed_keys vector. This vector should
  // always exist. If it doesn't there's an error. If no keys are pressed this vector
  // exists but is size 0.
  if (!report.keyboard().has_pressed_keys()) {
    return;
  }

  fidl::VectorView last_pressed_keys(last_pressed_keys_.data(), last_pressed_keys_size_);

  // Process the released keys.
  for (llcpp::fuchsia::ui::input2::Key prev_key : last_pressed_keys) {
    if (!is_key_in_set(prev_key, report.keyboard().pressed_keys())) {
      modifiers_ &= ~modifiers_from_fuchsia_key(prev_key);

      uint32_t hid_prev_key =
          *key_util::fuchsia_key_to_hid_key(static_cast<::fuchsia::ui::input2::Key>(prev_key));
      if (repeat_enabled_ && is_repeating_ && (repeating_key_ == hid_prev_key)) {
        is_repeating_ = false;
        timer_.cancel();
      }
    }
  }

  // Process the pressed keys.
  for (llcpp::fuchsia::ui::input2::Key key : report.keyboard().pressed_keys()) {
    if (!is_key_in_set(key, last_pressed_keys)) {
      modifiers_ |= modifiers_from_fuchsia_key(key);
      if (key == llcpp::fuchsia::ui::input2::Key::CAPS_LOCK) {
        modifiers_ ^= MOD_CAPSLOCK;
        SetCapsLockLed(modifiers_ & MOD_CAPSLOCK);
      }
      uint32_t hid_key =
          *key_util::fuchsia_key_to_hid_key(static_cast<::fuchsia::ui::input2::Key>(key));
      if (repeat_enabled_ && !keycode_is_modifier(hid_key)) {
        is_repeating_ = true;
        repeat_interval_ = kLowRepeatKeyFreq;
        timer_.cancel();
        timer_.set(zx::deadline_after(repeat_interval_), kSlackDuration);
        repeating_key_ = hid_key;
      }
      handler_(hid_key, modifiers_);
    }
  }

  // Store the previous state.
  size_t i = 0;
  for (llcpp::fuchsia::ui::input2::Key key : report.keyboard().pressed_keys()) {
    last_pressed_keys_[i++] = key;
  }
  last_pressed_keys_size_ = i;
}

Keyboard::~Keyboard() {
  if (input_notifier_.func != nullptr) {
    port_cancel(&port, &input_notifier_);
  }
  if (timer_notifier_.func != nullptr) {
    port_cancel(&port, &timer_notifier_);
  }
}

zx_status_t Keyboard::InputCallback(zx_signals_t signals, uint32_t evt) {
  if (!(signals & DEV_STATE_READABLE)) {
    return ZX_ERR_STOP;
  }

  llcpp::fuchsia::input::report::InputDevice::ResultOf::GetReports result =
      keyboard_client_->GetReports();
  if (result.status() != ZX_OK) {
    return result.status();
  }

  for (auto& report : result->reports) {
    ProcessInput(report);
  }
  return ZX_OK;
}

zx_status_t Keyboard::Setup(
    llcpp::fuchsia::input::report::InputDevice::SyncClient keyboard_client) {
  keyboard_client_ = std::move(keyboard_client);

  // XXX - check for LEDS here.

  zx_status_t status;
  if ((status = zx::timer::create(ZX_TIMER_SLACK_LATE, ZX_CLOCK_MONOTONIC, &timer_)) != ZX_OK) {
    return status;
  }

  llcpp::fuchsia::input::report::InputDevice::ResultOf::GetReportsEvent result =
      keyboard_client_->GetReportsEvent();
  if (result.status() != ZX_OK) {
    return status;
  }
  keyboard_event_ = std::move(result->event);

  input_notifier_.handle = keyboard_event_.get();
  input_notifier_.waitfor = DEV_STATE_READABLE;
  input_notifier_.func = [](port_handler* input_notifier, zx_signals_t signals, uint32_t evt) {
    Keyboard* kbd = containerof(input_notifier, Keyboard, input_notifier_);
    zx_status_t status = kbd->InputCallback(signals, evt);
    if (status == ZX_ERR_STOP) {
      delete kbd;
    }
    return status;
  };

  if ((status = port_wait(&port, &input_notifier_)) != ZX_OK) {
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

  zx::channel chan;
  zx_status_t status = fdio_get_service_handle(fd, chan.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }
  auto keyboard_client = llcpp::fuchsia::input::report::InputDevice::SyncClient(std::move(chan));

  llcpp::fuchsia::input::report::InputDevice::ResultOf::GetDescriptor result =
      keyboard_client.GetDescriptor();
  if (result.status() != ZX_OK) {
    return result.status();
  }
  // Skip devices that aren't keyboards.
  if (!result->descriptor.has_keyboard()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  printf("vc: new input device /dev/class/input-report/%s\n", name);

  // This is not a memory leak, because keyboards free themselves when their underlying
  // devices close.
  Keyboard* keyboard = new Keyboard(handler_, repeat_keys_);
  status = keyboard->Setup(std::move(keyboard_client));
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
  fbl::unique_fd fd(open("/dev/class/input-report", O_DIRECTORY | O_RDONLY));
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

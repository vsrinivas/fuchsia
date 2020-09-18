// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "keyboard.h"

#include <fcntl.h>
#include <fuchsia/hardware/input/c/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/watcher.h>
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

constexpr zx::duration kHighRepeatKeyFreq = zx::msec(50);
constexpr zx::duration kLowRepeatKeyFreq = zx::msec(250);

// TODO(dgilhooley): this global watcher is necessary because the ports we are using
// take a raw function pointer. I think once we move this library to libasync we can
// remove the global watcher and use lambdas.
KeyboardWatcher main_watcher;

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
                   const fidl::VectorView<llcpp::fuchsia::ui::input2::Key>& set) {
  for (auto s : set) {
    if (key == s) {
      return true;
    }
  }
  return false;
}

}  // namespace

zx_status_t setup_keyboard_watcher(async_dispatcher_t* dispatcher, keypress_handler_t handler,
                                   bool repeat_keys) {
  return main_watcher.Setup(dispatcher, handler, repeat_keys);
}

void Keyboard::TimerCallback(async_dispatcher_t* dispatcher, async::TaskBase* task,
                             zx_status_t status) {
  handler_(repeating_key_, modifiers_);

  // increase repeat rate if we're not yet at the fastest rate
  if ((repeat_interval_ = repeat_interval_ * 3 / 4) < kHighRepeatKeyFreq) {
    repeat_interval_ = kHighRepeatKeyFreq;
  }
  task->PostDelayed(dispatcher, repeat_interval_);
}

void Keyboard::SetCapsLockLed(bool caps_lock) {
  if (!keyboard_client_) {
    return;
  }

  // Generate the OutputReport.
  auto report_builder = llcpp::fuchsia::input::report::OutputReport::UnownedBuilder();
  auto keyboard_report_builder =
      llcpp::fuchsia::input::report::KeyboardOutputReport::UnownedBuilder();
  fidl::VectorView<llcpp::fuchsia::input::report::LedType> led_view;
  llcpp::fuchsia::input::report::LedType caps_led =
      llcpp::fuchsia::input::report::LedType::CAPS_LOCK;

  if (caps_lock) {
    led_view =
        fidl::VectorView<llcpp::fuchsia::input::report::LedType>(fidl::unowned_ptr(&caps_led), 1);
  } else {
    led_view = fidl::VectorView<llcpp::fuchsia::input::report::LedType>(nullptr, 0);
  }

  keyboard_report_builder.set_enabled_leds(fidl::unowned_ptr(&led_view));
  llcpp::fuchsia::input::report::KeyboardOutputReport keyboard_report =
      keyboard_report_builder.build();
  report_builder.set_keyboard(fidl::unowned_ptr(&keyboard_report));

  keyboard_client_->SendOutputReport(report_builder.build());
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

  fidl::VectorView last_pressed_keys(fidl::unowned_ptr(last_pressed_keys_.data()),
                                     last_pressed_keys_size_);

  // Process the released keys.
  for (llcpp::fuchsia::ui::input2::Key prev_key : last_pressed_keys) {
    if (!is_key_in_set(prev_key, report.keyboard().pressed_keys())) {
      modifiers_ &= ~modifiers_from_fuchsia_key(prev_key);

      uint32_t hid_prev_key =
          *key_util::fuchsia_key_to_hid_key(static_cast<::fuchsia::ui::input2::Key>(prev_key));
      if (repeat_enabled_ && is_repeating_ && (repeating_key_ == hid_prev_key)) {
        is_repeating_ = false;
        timer_task_.Cancel();
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
        repeating_key_ = hid_key;
        timer_task_.Cancel();
        timer_task_.PostDelayed(dispatcher_, repeat_interval_);
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

void Keyboard::InputCallback(
    llcpp::fuchsia::input::report::InputReportsReader_ReadInputReports_Result result) {
  if (result.is_err()) {
    printf("vc: InputCallback returns error: %d!\n", result.err());
    return;
  }
  for (auto& report : result.response().reports) {
    ProcessInput(report);
  }

  reader_client_->ReadInputReports(
      [this](llcpp::fuchsia::input::report::InputReportsReader_ReadInputReports_Result result) {
        InputCallback(std::move(result));
      });
}

zx_status_t Keyboard::StartReading() {
  zx::channel server, client;
  zx_status_t status = zx::channel::create(0, &server, &client);
  if (status != ZX_OK) {
    return status;
  }
  auto result = keyboard_client_->GetInputReportsReader(std::move(server));
  if (result.status() != ZX_OK) {
    return result.status();
  }

  status = reader_client_.Bind(std::move(client), dispatcher_,
                               [this](fidl::UnbindInfo info) {
                                 printf("vc: Keyboard Reader unbound.\n");
                                 InputReaderUnbound(info);
                               });
  if (status != ZX_OK) {
    return status;
  }

  // Queue up the first read.
  reader_client_->ReadInputReports(
      [this](llcpp::fuchsia::input::report::InputReportsReader_ReadInputReports_Result result) {
        InputCallback(std::move(result));
      });
  return ZX_OK;
};

void Keyboard::InputReaderUnbound(fidl::UnbindInfo info) {
  zx_status_t status = StartReading();
  if (status != ZX_OK) {
    delete this;
  }
}

zx_status_t Keyboard::Setup(
    llcpp::fuchsia::input::report::InputDevice::SyncClient keyboard_client) {
  keyboard_client_ = std::move(keyboard_client);
  // XXX - check for LEDS here.

  zx_status_t status = timer_task_.PostDelayed(dispatcher_, kLowRepeatKeyFreq);
  if (status != ZX_OK) {
    return status;
  }

  // StartReading has to be last because once this succeeds then Keyboard is responsible for
  // freeing itself.
  status = StartReading();
  if (status != ZX_OK) {
    return status;
  }
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
  Keyboard* keyboard = new Keyboard(dispatcher_, handler_, repeat_keys_);
  status = keyboard->Setup(std::move(keyboard_client));
  if (status != ZX_OK) {
    delete keyboard;
    return status;
  }
  return ZX_OK;
}

void KeyboardWatcher::DirCallback(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                  zx_status_t status, const zx_packet_signal_t* signal) {
  if (!(signal->observed & ZX_CHANNEL_READABLE)) {
    printf("vc: device directory died\n");
    return;
  }

  // Buffer contains events { Opcode, Len, Name[Len] }
  // See zircon/device/vfs.h for more detail
  // extra byte is for temporary NUL
  std::array<uint8_t, fuchsia_io_MAX_BUF + 1> buffer;
  uint32_t len;
  if (zx_channel_read(wait->object(), 0, buffer.data(), nullptr, buffer.size() - 1, 0, &len,
                      nullptr) < 0) {
    printf("vc: failed to read from device directory\n");
    return;
  }

  uint32_t index = 0;
  while (index + 2 <= len) {
    uint8_t event = buffer[index];
    uint8_t namelen = buffer[index + 1];
    index += 2;
    if ((namelen + index) > len) {
      printf("vc: malformed device directory message\n");
      return;
    }
    // add temporary nul
    uint8_t tmp = buffer[index + namelen];
    buffer[index + namelen] = 0;
    OpenFile(event, reinterpret_cast<char*>(&buffer[index]));
    buffer[index + namelen] = tmp;
    index += namelen;
  }

  wait->Begin(dispatcher);
}

zx_status_t KeyboardWatcher::Setup(async_dispatcher_t* dispatcher, keypress_handler_t handler,
                                   bool repeat_keys) {
  ZX_DEBUG_ASSERT(dispatcher_ == nullptr);
  dispatcher_ = dispatcher;

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

  dir_caller_ = fdio_cpp::FdioCaller(std::move(fd));
  zx_status_t io_status = fuchsia_io_DirectoryWatch(
      dir_caller_.borrow_channel(), fuchsia_io_WATCH_MASK_ALL, 0, server.release(), &status);
  if (io_status != ZX_OK || status != ZX_OK) {
    return io_status;
  }

  dir_wait_.set_object(client.release());
  dir_wait_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
  dir_wait_.Begin(dispatcher_);

  return ZX_OK;
}

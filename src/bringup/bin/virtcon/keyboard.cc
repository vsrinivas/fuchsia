// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "keyboard.h"

#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fuchsia/hardware/input/c/fidl.h>
#include <lib/ddk/device.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/watcher.h>
#include <lib/fidl/llcpp/connect_service.h>
#include <lib/service/llcpp/service.h>
#include <lib/zx/channel.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/syscalls.h>

#include <utility>

#include <hid-parser/usages.h>
#include <hid/hid.h>
#include <hid/usages.h>

#include "src/ui/lib/key_util/key_util.h"

namespace fio = fuchsia_io;

namespace {

constexpr zx::duration kHighRepeatKeyFreq = zx::msec(50);
constexpr zx::duration kLowRepeatKeyFreq = zx::msec(250);

// TODO(dgilhooley): this global watcher is necessary because the ports we are using
// take a raw function pointer. I think once we move this library to libasync we can
// remove the global watcher and use lambdas.
KeyboardWatcher main_watcher;

int modifiers_from_fuchsia_key(fuchsia_input::wire::Key key) {
  switch (key) {
    case fuchsia_input::wire::Key::kLeftShift:
      return MOD_LSHIFT;
    case fuchsia_input::wire::Key::kRightShift:
      return MOD_RSHIFT;
    case fuchsia_input::wire::Key::kLeftAlt:
      return MOD_LALT;
    case fuchsia_input::wire::Key::kRightAlt:
      return MOD_RALT;
    case fuchsia_input::wire::Key::kLeftCtrl:
      return MOD_LCTRL;
    case fuchsia_input::wire::Key::kRightCtrl:
      return MOD_RCTRL;
    default:
      return 0;
  }
}

uint8_t hid_usage_to_keycode(uint32_t hid_usage) {
  uint16_t hid_usage_id = hid::usage::UsageToUsageId(hid_usage);
  ZX_DEBUG_ASSERT(hid_usage_id <= UINT8_MAX);
  return static_cast<uint8_t>(hid_usage_id);
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

bool is_key_in_set(fuchsia_input::wire::Key key,
                   const fidl::VectorView<fuchsia_input::wire::Key>& set) {
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
  handler_(repeating_keycode_, modifiers_);

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
  fidl::Arena allocator;

  fidl::VectorView<fuchsia_input_report::wire::LedType> led_view;
  if (caps_lock) {
    led_view = fidl::VectorView<fuchsia_input_report::wire::LedType>(allocator, 1);
    led_view[0] = fuchsia_input_report::wire::LedType::kCapsLock;
  }

  fuchsia_input_report::wire::KeyboardOutputReport keyboard_report(allocator);
  keyboard_report.set_enabled_leds(allocator, std::move(led_view));

  fuchsia_input_report::wire::OutputReport report(allocator);
  report.set_keyboard(allocator, std::move(keyboard_report));

  keyboard_client_->SendOutputReport(std::move(report));
}

// returns true if key was pressed and none were released
void Keyboard::ProcessInput(const fuchsia_input_report::wire::InputReport& report) {
  if (!report.has_keyboard()) {
    return;
  }
  // Check if the keyboard FIDL table contains the pressed_keys vector. This vector should
  // always exist. If it doesn't there's an error. If no keys are pressed this vector
  // exists but is size 0.
  if (!report.keyboard().has_pressed_keys3()) {
    return;
  }

  fidl::VectorView last_pressed_keys(fidl::VectorView<fuchsia_input::wire::Key>::FromExternal(
      last_pressed_keys_.data(), last_pressed_keys_size_));

  // Process the released keys.
  for (fuchsia_input::wire::Key prev_key : last_pressed_keys) {
    if (!is_key_in_set(prev_key, report.keyboard().pressed_keys3())) {
      modifiers_ &= ~modifiers_from_fuchsia_key(prev_key);

      uint32_t hid_prev_key =
          key_util::fuchsia_key3_to_hid_key(static_cast<::fuchsia::input::Key>(prev_key));
      uint8_t keycode = hid_usage_to_keycode(hid_prev_key);
      if (repeat_enabled_ && is_repeating_ && (repeating_keycode_ == keycode)) {
        is_repeating_ = false;
        timer_task_.Cancel();
      }
    }
  }

  // Process the pressed keys.
  for (fuchsia_input::wire::Key key : report.keyboard().pressed_keys3()) {
    if (!is_key_in_set(key, last_pressed_keys)) {
      modifiers_ |= modifiers_from_fuchsia_key(key);
      if (key == fuchsia_input::wire::Key::kCapsLock) {
        modifiers_ ^= MOD_CAPSLOCK;
        SetCapsLockLed(modifiers_ & MOD_CAPSLOCK);
      }
      uint32_t hid_key = key_util::fuchsia_key3_to_hid_key(static_cast<::fuchsia::input::Key>(key));
      uint8_t keycode = hid_usage_to_keycode(hid_key);
      if (repeat_enabled_ && !keycode_is_modifier(keycode)) {
        is_repeating_ = true;
        repeat_interval_ = kLowRepeatKeyFreq;
        repeating_keycode_ = keycode;
        timer_task_.Cancel();
        timer_task_.PostDelayed(dispatcher_, repeat_interval_);
      }
      handler_(keycode, modifiers_);
    }
  }

  // Store the previous state.
  size_t i = 0;
  for (fuchsia_input::wire::Key key : report.keyboard().pressed_keys3()) {
    last_pressed_keys_[i++] = key;
  }
  last_pressed_keys_size_ = i;
}

void Keyboard::InputCallback(
    fuchsia_input_report::wire::InputReportsReaderReadInputReportsResult result) {
  if (result.is_err()) {
    printf("vc: InputCallback returns error: %d!\n", result.err());
    return;
  }
  for (auto& report : result.response().reports) {
    ProcessInput(report);
  }

  reader_client_->ReadInputReports(
      [this](fidl::WireResponse<fuchsia_input_report::InputReportsReader::ReadInputReports>*
                 response) { InputCallback(response->result); });
}

zx_status_t Keyboard::StartReading() {
  auto endpoints = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }
  auto [client, server] = *std::move(endpoints);
  auto result = keyboard_client_->GetInputReportsReader(std::move(server));
  if (result.status() != ZX_OK) {
    return result.status();
  }

  reader_client_ = {};
  reader_client_.Bind(std::move(client), dispatcher_, this);

  // Queue up the first read.
  reader_client_->ReadInputReports(
      [this](fidl::WireResponse<fuchsia_input_report::InputReportsReader::ReadInputReports>*
                 response) { InputCallback(response->result); });
  return ZX_OK;
};

void Keyboard::on_fidl_error(fidl::UnbindInfo info) {
  printf("vc: Keyboard Reader FIDL error: %s.\n", info.FormatDescription().c_str());
  zx_status_t status = StartReading();
  if (status != ZX_OK) {
    delete this;
  }
}

zx_status_t Keyboard::Setup(
    fidl::WireSyncClient<fuchsia_input_report::InputDevice> keyboard_client) {
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
  if ((evt != fio::wire::kWatchEventExisting) && (evt != fio::wire::kWatchEventAdded)) {
    return ZX_OK;
  }

  auto client_end = service::ConnectAt<fuchsia_input_report::InputDevice>(Directory(), name);
  if (client_end.is_error()) {
    return ZX_OK;
  }

  auto keyboard_client = fidl::BindSyncClient(std::move(*client_end));

  fidl::WireResult<fuchsia_input_report::InputDevice::GetDescriptor> result =
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
  zx_status_t status = keyboard->Setup(std::move(keyboard_client));
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
  std::array<uint8_t, fio::wire::kMaxBuf + 1> buffer;
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

  auto result = fidl::WireCall(dir_caller_.directory())
                    ->Watch(fio::wire::kWatchMaskAll, 0, std::move(server));
  if (result.status() != ZX_OK) {
    return result.status();
  }

  dir_wait_.set_object(client.release());
  dir_wait_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
  dir_wait_.Begin(dispatcher_);

  return ZX_OK;
}

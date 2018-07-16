// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/hid_event_source.h"

#include <fcntl.h>
#include <threads.h>
#include <unistd.h>

#include <fbl/auto_lock.h>
#include <fbl/unique_fd.h>
#include <hid/hid.h>
#include <lib/fdio/watcher.h>
#include <zircon/compiler.h>
#include <zircon/device/input.h>
#include <zircon/types.h>

#include "lib/fxl/logging.h"

namespace machina {

static constexpr char kInputDirPath[] = "/dev/class/input";

zx_status_t HidInputDevice::Start() {
  thrd_t thread;
  auto hid_event_thread = [](void* arg) {
    return reinterpret_cast<HidInputDevice*>(arg)->HidEventLoop();
  };
  int ret = thrd_create_with_name(&thread, hid_event_thread, this, "hid-event");
  if (ret != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  ret = thrd_detach(thread);
  if (ret != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t HidInputDevice::HandleHidKeys(const hid_keys_t& curr_keys) {
  bool send_barrier = false;

  // Send key-down events.
  uint8_t keycode;
  hid_keys_t pressed;
  hid_kbd_pressed_keys(&prev_keys_, &curr_keys, &pressed);
  hid_for_every_key(&pressed, keycode) {
    SendKeyEvent(keycode, true);
    send_barrier = true;
  }

  // Send key-up events.
  hid_keys_t released;
  hid_kbd_released_keys(&prev_keys_, &curr_keys, &released);
  hid_for_every_key(&released, keycode) {
    SendKeyEvent(keycode, false);
    send_barrier = true;
  }

  if (send_barrier) {
    SendBarrier(input_dispatcher_->Keyboard());
  }

  prev_keys_ = curr_keys;
  return ZX_OK;
}

zx_status_t HidInputDevice::HidEventLoop() {
  uint8_t report[8];
  while (true) {
    ssize_t r = read(fd_.get(), report, sizeof(report));
    if (r != sizeof(report)) {
      FXL_LOG(ERROR) << "Failed to read from input device";
      return ZX_ERR_IO;
    }

    hid_keys_t curr_keys;
    hid_kbd_parse_report(report, &curr_keys);

    zx_status_t status = HandleHidKeys(curr_keys);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to handle HID keys";
      return status;
    }
  }
  return ZX_OK;
}

void HidInputDevice::SendKeyEvent(uint32_t hid_usage, bool pressed) {
  InputEvent event;
  event.type = InputEventType::KEYBOARD;
  event.key = {
      .hid_usage = hid_usage,
      .state = pressed ? KeyState::PRESSED : KeyState::RELEASED,
  };
  input_dispatcher_->Keyboard()->PostEvent(event);
}

void HidInputDevice::SendBarrier(InputEventQueue* event_queue) {
  InputEvent event;
  event.type = InputEventType::BARRIER;
  event_queue->PostEvent(event);
}

zx_status_t HidEventSource::Start() {
  thrd_t thread;
  int ret = thrd_create_with_name(&thread, &HidEventSource::WatchInputDirectory,
                                  this, "hid-watch");
  if (ret != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  ret = thrd_detach(thread);
  if (ret != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t HidEventSource::WatchInputDirectory(void* arg) {
  fbl::unique_fd dir_fd(open(kInputDirPath, O_DIRECTORY | O_RDONLY));
  if (!dir_fd) {
    return ZX_ERR_IO;
  }
  auto callback = [](int fd, int event, const char* fn, void* cookie) {
    return static_cast<HidEventSource*>(cookie)->AddInputDevice(fd, event, fn);
  };
  return fdio_watch_directory(dir_fd.get(), callback, ZX_TIME_INFINITE, arg);
}

zx_status_t HidEventSource::AddInputDevice(int dirfd, int event,
                                           const char* fn) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }

  fbl::unique_fd fd;
  int raw_fd = openat(dirfd, fn, O_RDONLY);
  if (raw_fd < 0) {
    FXL_LOG(ERROR) << "Failed to open device " << kInputDirPath << "/" << fn;
    return ZX_OK;
  }
  fd.reset(raw_fd);

  int proto = INPUT_PROTO_NONE;
  if (ioctl_input_get_protocol(fd.get(), &proto) < 0) {
    FXL_LOG(ERROR) << "Failed to get input device protocol";
    return ZX_ERR_INVALID_ARGS;
  }

  // If the device isn't a keyboard, just continue.
  if (proto != INPUT_PROTO_KBD) {
    return ZX_OK;
  }

  auto keyboard =
      fbl::make_unique<HidInputDevice>(input_dispatcher_, std::move(fd));
  zx_status_t status = keyboard->Start();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to start device " << kInputDirPath << "/" << fn;
    return status;
  }
  FXL_LOG(INFO) << "Polling device " << kInputDirPath << "/" << fn
                << " for key events";

  fbl::AutoLock lock(&mutex_);
  devices_.push_front(std::move(keyboard));
  return ZX_OK;
}

}  // namespace machina

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/launcher/input/input_device.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>

#include <hid/acer12.h>
#include <hid/usages.h>
#include <magenta/device/console.h>
#include <magenta/device/device.h>
#include <magenta/device/input.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <mojo/system/time.h>
#include <mxio/io.h>
#include <mxio/watcher.h>

#define DEV_INPUT "/dev/class/input"

#define TRYFN(fn)      \
  do {                 \
    int rc = fn;       \
    if (rc < 0) {      \
      if (device)      \
        delete device; \
      return nullptr;  \
    }                  \
  } while (0)

namespace launcher {

namespace {

constexpr uint32_t kMouseLeftButtonMask = 0x01;
constexpr uint32_t kMouseRightButtonMask = 0x02;
constexpr uint32_t kMouseMiddleButtonMask = 0x04;

int get_hid_protocol(int fd, const char* name, int* proto) {
  ssize_t rc = ioctl_input_get_protocol(fd, proto);
  if (rc < 0) {
    FTL_LOG(ERROR) << "hid: could not get protocol from " << name
                   << " (status=" << rc << ")";
  }
  return rc;
}

int get_report_desc_len(int fd, const char* name, size_t* report_desc_len) {
  ssize_t rc = ioctl_input_get_report_desc_size(fd, report_desc_len);
  if (rc < 0) {
    FTL_LOG(ERROR) << "hid: could not get report descriptor length from "
                   << name << "  (status=" << rc << ")";
  }
  return rc;
}

int get_report_desc(int fd,
                    const char* name,
                    uint8_t* buf,
                    size_t report_desc_len) {
  ssize_t rc = ioctl_input_get_report_desc(fd, buf, report_desc_len);
  if (rc < 0) {
    FTL_LOG(ERROR) << "hid: could not get report descriptor from " << name
                   << " (status=" << rc << ")";
  }
  return rc;
}

int get_num_reports(int fd, const char* name, size_t* num_reports) {
  ssize_t rc = ioctl_input_get_num_reports(fd, num_reports);
  if (rc < 0) {
    FTL_LOG(ERROR) << "hid: could not get number of reports from " << name
                   << " (status=" << rc << ")";
  }
  return rc;
}

int get_report_ids(int fd,
                   const char* name,
                   input_report_id_t* ids,
                   size_t num_reports) {
  size_t out_len = num_reports * sizeof(input_report_id_t);
  ssize_t rc = ioctl_input_get_report_ids(fd, ids, out_len);
  if (rc < 0) {
    FTL_LOG(ERROR) << "hid: could not get report ids from " << name
                   << " (status=" << rc << ")";
    return rc;
  }

  for (size_t i = 0; i < num_reports; i++) {
    input_get_report_size_t arg;
    arg.id = ids[i];
    arg.type = INPUT_REPORT_INPUT;  // TODO: get all types
    input_report_size_t size;
    ssize_t size_rc = ioctl_input_get_report_size(fd, &arg, &size);
    if (size_rc < 0) {
      FTL_LOG(ERROR) << "hid: could not get report id size from " << name
                     << " (status=" << rc << ")";
      continue;
    }
  }
  return rc;
}

int get_max_report_len(int fd,
                       const char* name,
                       input_report_size_t* max_report_len) {
  ssize_t rc = ioctl_input_get_max_reportsize(fd, max_report_len);
  if (rc < 0) {
    FTL_LOG(ERROR) << "hid: could not get max report size from " << name
                   << " (status=" << rc << ")";
  }
  return rc;
}

// The input event mojom is currently defined to expect some number
// of milliseconds.
int64_t InputEventTimestampNow() {
  return MojoGetTimeTicksNow() / 1000;
}
}

InputDevice::InputDevice() {}

InputDevice::~InputDevice() {
  if (fd_) {
    close(fd_);
  }
  if (report_desc_) {
    free(report_desc_);
  }
  if (ids_) {
    free(ids_);
  }
  if (report_) {
    free(report_);
  }
}

KeyboardInputDevice::KeyboardInputDevice() : keymap_(qwerty_map) {
  char* keys = getenv("gfxconsole.keymap");
  if (keys && !strcmp(keys, "dvorak")) {
    keymap_ = dvorak_map;
  }
}

InputDevice* InputDevice::BuildInputDevice(int fd, const char* name) {
  InputDevice* device = nullptr;

  int protocol;
  size_t report_desc_len;
  uint8_t* report_desc;

  TRYFN(get_hid_protocol(fd, name, &protocol));
  TRYFN(get_report_desc_len(fd, name, &report_desc_len));

  report_desc = static_cast<uint8_t*>(malloc(report_desc_len));
  if (!report_desc) {
    FTL_LOG(ERROR) << "Could not allocate report description of size "
                   << report_desc_len << " byte(s)";
    return nullptr;
  }
  TRYFN(get_report_desc(fd, name, report_desc, report_desc_len));

  if (protocol == INPUT_PROTO_KBD) {
    device = new KeyboardInputDevice();
  } else if (protocol == INPUT_PROTO_MOUSE) {
    device = new MouseInputDevice();
  } else if (protocol == INPUT_PROTO_NONE) {
    if (report_desc_len == ACER12_RPT_DESC_LEN &&
        !memcmp(report_desc, acer12_touch_report_desc, ACER12_RPT_DESC_LEN)) {
      device = new Acer12InputDevice();
    } else {
      FTL_LOG(ERROR) << "Generic HID device not supported " << name;
      free(report_desc);
      return nullptr;
    }
  } else {
    FTL_LOG(ERROR) << "Unsupported HID protocol " << protocol;
    free(report_desc);
    return nullptr;
  }

  if (!device) {
    FTL_LOG(ERROR) << "Could not allocate InputDevice for " << name;
    free(report_desc);
    return nullptr;
  }

  device->protocol_ = protocol;
  device->fd_ = fd;
  device->report_desc_len_ = report_desc_len;
  device->report_desc_ = report_desc;

  snprintf(device->name_, sizeof(device->name_), "hid-input-%s", name);

  // Get event handle for file descriptor
  mx_handle_t handle;
  ssize_t rc = ioctl_device_get_event_handle(device->fd_, &handle);
  if (rc < 0) {
    FTL_LOG(ERROR) << "Could not convert file descriptor to handle";
    delete device;
    return nullptr;
  }

  device->event_handle_ =
      mojo::ScopedHandleBase<mojo::Handle>(mojo::Handle(handle));

  TRYFN(get_num_reports(fd, name, &device->num_reports_));

  size_t out_len = device->num_reports_ * sizeof(input_report_id_t);
  device->ids_ = static_cast<input_report_id_t*>(malloc(out_len));
  if (!device->ids_) {
    FTL_LOG(ERROR) << "Could not allocate report id array of size " << out_len;
    delete device;
    return nullptr;
  }
  TRYFN(get_report_ids(fd, name, device->ids_, device->num_reports_));
  TRYFN(get_max_report_len(fd, name, &device->max_report_len_));

  device->report_ = static_cast<uint8_t*>(calloc(1, device->max_report_len_));
  if (!device->report_) {
    FTL_LOG(ERROR) << "Could not allocate HID report space of size "
                   << device->max_report_len_;
    delete device;
    return nullptr;
  }

  return device;
}

bool InputDevice::Read(const OnEventCallback& callback) {
  int rc = read(fd_, report_, max_report_len_);
  if (rc < 0) {
    // TODO(jpoichet) check whether the device was actually closed or not
    return false;
  }
  Parse(callback);
  return true;
}

void KeyboardInputDevice::Parse(const OnEventCallback& callback) {
  int64_t now = InputEventTimestampNow();
  uint8_t keycode;
  hid_kbd_parse_report(report_, &key_state_[current_index_]);
  // Get key pressed between previous and current state
  hid_kbd_pressed_keys(&key_state_[previous_index_],
                       &key_state_[current_index_], &key_delta_);
  // For all key pressed, generate a key event
  hid_for_every_key(&key_delta_, keycode) {
    mozart::EventPtr ev = mozart::Event::New();
    ev->action = mozart::EventType::KEY_PRESSED;
    ev->flags = mozart::EventFlags::NONE;
    ev->time_stamp = now;

    ev->key_data = mozart::KeyData::New();
    ev->key_data->key_code = 0;
    ev->key_data->is_char = false;
    ev->key_data->character = 0;
    ev->key_data->hid_usage = keycode;
    ev->key_data->text = 0;
    ev->key_data->unmodified_text = 0;

    uint8_t ch = hid_map_key(keycode, modifiers_ & MOD_SHIFT, keymap_);
    if (ch) {
      uint16_t character16 = static_cast<unsigned char>(ch);
      ev->key_data->is_char = true;
      ev->key_data->character = character16;
      ev->key_data->text = character16;
      ev->key_data->unmodified_text = character16;
    }

    switch (keycode) {
      case HID_USAGE_KEY_LEFT_SHIFT:
        modifiers_ |= MOD_LSHIFT;
        break;
      case HID_USAGE_KEY_RIGHT_SHIFT:
        modifiers_ |= MOD_RSHIFT;
        break;
      case HID_USAGE_KEY_LEFT_CTRL:
        modifiers_ |= MOD_LCTRL;
        break;
      case HID_USAGE_KEY_RIGHT_CTRL:
        modifiers_ |= MOD_RCTRL;
        break;
      case HID_USAGE_KEY_LEFT_ALT:
        modifiers_ |= MOD_LALT;
        break;
      case HID_USAGE_KEY_RIGHT_ALT:
        modifiers_ |= MOD_RALT;
        break;
      default:
        break;
    }

    callback(std::move(ev));
  }

  // Get key released between previous and current state
  hid_kbd_released_keys(&key_state_[previous_index_],
                        &key_state_[current_index_], &key_delta_);
  // For all key released, generate a key event
  hid_for_every_key(&key_delta_, keycode) {
    mozart::EventPtr ev = mozart::Event::New();
    ev->action = mozart::EventType::KEY_RELEASED;
    ev->flags = mozart::EventFlags::NONE;
    ev->time_stamp = now;

    ev->key_data = mozart::KeyData::New();
    ev->key_data->key_code = 0;
    ev->key_data->is_char = false;
    ev->key_data->character = 0;
    ev->key_data->hid_usage = keycode;
    ev->key_data->text = 0;
    ev->key_data->unmodified_text = 0;

    uint8_t ch = hid_map_key(keycode, modifiers_ & MOD_SHIFT, keymap_);
    if (ch) {
      uint16_t character16 = static_cast<unsigned char>(ch);
      ev->key_data->is_char = true;
      ev->key_data->character = character16;
      ev->key_data->text = character16;
      ev->key_data->unmodified_text = character16;
    }

    switch (keycode) {
      case HID_USAGE_KEY_LEFT_SHIFT:
        modifiers_ &= (~MOD_LSHIFT);
        break;
      case HID_USAGE_KEY_RIGHT_SHIFT:
        modifiers_ &= (~MOD_RSHIFT);
        break;
      case HID_USAGE_KEY_LEFT_CTRL:
        modifiers_ &= (~MOD_LCTRL);
        break;
      case HID_USAGE_KEY_RIGHT_CTRL:
        modifiers_ &= (~MOD_RCTRL);
        break;
      case HID_USAGE_KEY_LEFT_ALT:
        modifiers_ &= (~MOD_LALT);
        break;
      case HID_USAGE_KEY_RIGHT_ALT:
        modifiers_ &= (~MOD_RALT);
        break;
    }
    callback(std::move(ev));
  }

  // swap key states
  current_index_ = 1 - current_index_;
  previous_index_ = 1 - previous_index_;
}

void MouseInputDevice::SendEvent(const OnEventCallback& callback,
                                 float rel_x,
                                 float rel_y,
                                 int64_t timestamp,
                                 mozart::EventType type,
                                 mozart::EventFlags flags) {
  mozart::EventPtr ev = mozart::Event::New();
  ev->time_stamp = timestamp;
  ev->action = type;
  ev->flags = flags;

  ev->pointer_data = mozart::PointerData::New();
  ev->pointer_data->pointer_id = 0;
  ev->pointer_data->kind = mozart::PointerKind::MOUSE;
  ev->pointer_data->x = rel_x;
  ev->pointer_data->y = rel_y;
  ev->pointer_data->screen_x = rel_x;
  ev->pointer_data->screen_y = rel_y;
  ev->pointer_data->pressure = 0;
  ev->pointer_data->radius_major = 0;
  ev->pointer_data->radius_minor = 0;
  ev->pointer_data->orientation = 0.;
  ev->pointer_data->horizontal_wheel = 0;
  ev->pointer_data->vertical_wheel = 0;
  callback(std::move(ev));
}

void MouseInputDevice::Parse(const OnEventCallback& callback) {
  boot_mouse_report_t* report = reinterpret_cast<boot_mouse_report_t*>(report_);
  uint64_t now = InputEventTimestampNow();
  uint8_t pressed = (report->buttons ^ buttons_) & report->buttons;
  uint8_t released = (report->buttons ^ buttons_) & buttons_;

  if (!pressed && !released) {
    SendEvent(callback, report->rel_x, report->rel_y, now,
              mozart::EventType::POINTER_MOVE, mozart::EventFlags::NONE);
  } else {
    if (pressed) {
      if (pressed & kMouseLeftButtonMask) {
        SendEvent(callback, report->rel_x, report->rel_y, now,
                  mozart::EventType::POINTER_DOWN,
                  mozart::EventFlags::LEFT_MOUSE_BUTTON);
      }
      if (pressed & kMouseRightButtonMask) {
        SendEvent(callback, report->rel_x, report->rel_y, now,
                  mozart::EventType::POINTER_DOWN,
                  mozart::EventFlags::RIGHT_MOUSE_BUTTON);
      }
      if (pressed & kMouseMiddleButtonMask) {
        SendEvent(callback, report->rel_x, report->rel_y, now,
                  mozart::EventType::POINTER_DOWN,
                  mozart::EventFlags::MIDDLE_MOUSE_BUTTON);
      }
    }
    if (released) {
      if (released & kMouseLeftButtonMask) {
        SendEvent(callback, report->rel_x, report->rel_y, now,
                  mozart::EventType::POINTER_UP,
                  mozart::EventFlags::LEFT_MOUSE_BUTTON);
      }
      if (released & kMouseRightButtonMask) {
        SendEvent(callback, report->rel_x, report->rel_y, now,
                  mozart::EventType::POINTER_UP,
                  mozart::EventFlags::RIGHT_MOUSE_BUTTON);
      }
      if (released & kMouseMiddleButtonMask) {
        SendEvent(callback, report->rel_x, report->rel_y, now,
                  mozart::EventType::POINTER_UP,
                  mozart::EventFlags::MIDDLE_MOUSE_BUTTON);
      }
    }
  }
  buttons_ = report->buttons;
}

void Acer12InputDevice::ParseStylus(const OnEventCallback& callback) {
  acer12_stylus_t* report = reinterpret_cast<acer12_stylus_t*>(report_);

  const bool previous_stylus_down = stylus_down_;
  stylus_down_ = acer12_stylus_status_inrange(report->status) &&
                 acer12_stylus_status_tswitch(report->status);

  mozart::EventType action = mozart::EventType::POINTER_DOWN;
  if (stylus_down_) {
    if (previous_stylus_down) {
      action = mozart::EventType::POINTER_MOVE;
    }
  } else {
    if (!previous_stylus_down) {
      return;
    }
    action = mozart::EventType::POINTER_UP;
  }

  mozart::EventPtr ev = mozart::Event::New();
  ev->action = action;
  ev->flags = mozart::EventFlags::NONE;
  ev->time_stamp = InputEventTimestampNow();
  if (action == mozart::EventType::POINTER_UP) {
    ev->pointer_data = stylus_.Clone();
  } else {
    float x =
        static_cast<float>(report->x) / static_cast<float>(ACER12_STYLUS_X_MAX);
    float y =
        static_cast<float>(report->y) / static_cast<float>(ACER12_STYLUS_Y_MAX);

    ev->pointer_data = mozart::PointerData::New();
    ev->pointer_data->pointer_id = 11;  // Assuming max of 10 fingers
    ev->pointer_data->kind = mozart::PointerKind::TOUCH;
    ev->pointer_data->x = x;
    ev->pointer_data->y = y;
    ev->pointer_data->screen_x = x;
    ev->pointer_data->screen_y = y;
    ev->pointer_data->pressure = report->pressure;
    ev->pointer_data->radius_major = 0.;
    ev->pointer_data->radius_minor = 0.;
    ev->pointer_data->orientation = 0.;
    ev->pointer_data->horizontal_wheel = 0.;
    ev->pointer_data->vertical_wheel = 0.;
    stylus_ = *ev->pointer_data;
  }
  callback(std::move(ev));
}

void Acer12InputDevice::ParseTouchscreen(const OnEventCallback& callback) {
  acer12_touch_t* report = reinterpret_cast<acer12_touch_t*>(report_);

  int64_t now = InputEventTimestampNow();
  std::vector<mozart::PointerData> old_pointers = pointers_;
  pointers_.clear();

  // Only 5 touches per report
  for (uint8_t c = 0; c < 5; c++) {
    if (!acer12_finger_id_tswitch(report->fingers[c].finger_id))
      continue;

    uint32_t width = 2 * report->fingers[c].width;
    uint32_t height = 2 * report->fingers[c].height;

    int pointer_id = acer12_finger_id_contact(report->fingers[c].finger_id);

    auto ev = mozart::Event::New();
    ev->action = mozart::EventType::POINTER_DOWN;
    for (auto it = old_pointers.begin(); it != old_pointers.end(); ++it) {
      if (it->pointer_id == pointer_id) {
        ev->action = mozart::EventType::POINTER_MOVE;
        old_pointers.erase(it);
        break;
      }
    }
    ev->flags = mozart::EventFlags::NONE;
    ev->time_stamp = now;

    ev->pointer_data = mozart::PointerData::New();
    ev->pointer_data->pointer_id = pointer_id;
    ev->pointer_data->kind = mozart::PointerKind::TOUCH;

    float x = static_cast<float>(report->fingers[c].x) /
              static_cast<float>(ACER12_X_MAX);
    float y = static_cast<float>(report->fingers[c].y) /
              static_cast<float>(ACER12_Y_MAX);

    ev->pointer_data->x = x;
    ev->pointer_data->y = y;
    ev->pointer_data->screen_x = x;
    ev->pointer_data->screen_y = y;
    ev->pointer_data->pressure = 0.;
    ev->pointer_data->radius_major = width > height ? width : height;
    ev->pointer_data->radius_minor = width > height ? height : width;
    ev->pointer_data->orientation = 0.;
    ev->pointer_data->horizontal_wheel = 0.;
    ev->pointer_data->vertical_wheel = 0.;
    pointers_.push_back(*ev->pointer_data);

    callback(std::move(ev));
  }

  for (const auto& pointer : old_pointers) {
    auto ev = mozart::Event::New();
    ev->action = mozart::EventType::POINTER_UP;
    ev->flags = mozart::EventFlags::NONE;
    ev->time_stamp = now;
    ev->pointer_data = pointer.Clone();

    callback(std::move(ev));
  }
}

void Acer12InputDevice::Parse(const OnEventCallback& callback) {
  if (*(uint8_t*)report_ == ACER12_RPT_ID_TOUCH) {
    ParseTouchscreen(callback);
  } else if (*(uint8_t*)report_ == ACER12_RPT_ID_STYLUS) {
    ParseStylus(callback);
  }
}

//
// InputReader
//

InputReader::InputReader() {}
InputReader::~InputReader() {
  if (input_directory_key_) {
    main_loop_->RemoveHandler(input_directory_key_);
  }
  if (input_directory_fd_) {
    close(input_directory_fd_);
  }
  if (input_directory_handle_) {
    close(input_directory_handle_);
  }
}

void InputReader::Start(const OnEventCallback& callback) {
  main_loop_ = mtl::MessageLoop::GetCurrent();
  callback_ = callback;

  // Check content of /dev/input and add handle to monitor changes
  main_loop_->task_runner()->PostTask([this] {
    input_directory_fd_ = open(DEV_INPUT, O_DIRECTORY | O_RDONLY);
    if (input_directory_fd_ < 0) {
      FTL_LOG(ERROR) << "Error opening " << DEV_INPUT;
      return;
    }

    DIR* dir;
    int fd;
    if ((fd = openat(input_directory_fd_, ".", O_RDONLY | O_DIRECTORY)) < 0) {
      FTL_LOG(ERROR) << "Error opening directory " << DEV_INPUT;
      return;
    }
    if ((dir = fdopendir(fd)) == NULL) {
      FTL_LOG(ERROR) << "Failed to open directory " << DEV_INPUT;
      return;
    }

    struct dirent* de;
    while ((de = readdir(dir)) != NULL) {
      if (de->d_name[0] == '.') {
        if (de->d_name[1] == 0) {
          continue;
        }
        if ((de->d_name[1] == '.') && (de->d_name[2] == 0)) {
          continue;
        }
      }
      InputDevice* device = OpenDevice(input_directory_fd_, de->d_name);
      if (device) {
        DeviceAdded(device);
      }
    }
    closedir(dir);

    mx_handle_t handle;
    ssize_t r = ioctl_device_watch_dir(input_directory_fd_, &handle);
    if (r < 0) {
      return;
    }
    input_directory_handle_ = static_cast<MojoHandle>(handle);

    MojoHandleSignals signals = MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED;
    input_directory_key_ = main_loop_->AddHandler(
        this, input_directory_handle_, signals, ftl::TimeDelta::Max());
  });
}

InputDevice* InputReader::OpenDevice(int dirfd, const char* fn) {
  int fd = openat(dirfd, fn, O_RDONLY);
  if (fd < 0) {
    FTL_LOG(ERROR) << "Failed to open device " << fn;
    return nullptr;
  }

  return InputDevice::BuildInputDevice(fd, fn);
}

InputDevice* InputReader::GetDevice(MojoHandle handle) {
  if (!devices_.count(handle)) {
    return nullptr;
  }
  return devices_.at(handle).first;
}

void InputReader::DeviceRemoved(InputDevice* device) {
  if (!device) {
    return;
  }
  FTL_LOG(INFO) << "Input device " << device->name_ << " removed";
  MojoHandle handle = device->event_handle_.get().value();
  main_loop_->RemoveHandler(devices_.at(handle).second);
  devices_.erase(handle);
  delete device;
}

void InputReader::DeviceAdded(InputDevice* device) {
  FTL_LOG(INFO) << "Input device " << device->name_ << " added";
  MojoHandleSignals signals = MOJO_HANDLE_SIGNAL_SIGNAL0;
  mtl::MessageLoop::HandlerKey key =
      main_loop_->AddHandler(this, device->event_handle_.get().value(), signals,
                             ftl::TimeDelta::Max());
  devices_[device->event_handle_.get().value()] = std::make_pair(device, key);
}

void InputReader::OnDirectoryHandleReady(MojoHandle handle) {
  mx_status_t status;
  uint32_t sz = MXIO_MAX_FILENAME;
  char name[MXIO_MAX_FILENAME + 1];
  if ((status = mx_msgpipe_read(input_directory_handle_, name, &sz, NULL, NULL,
                                0)) < 0) {
    FTL_LOG(ERROR) << "Failed to read from " << DEV_INPUT;
    return;
  }
  name[sz] = 0;
  InputDevice* device = OpenDevice(input_directory_fd_, name);
  if (device) {
    DeviceAdded(device);
  }
}
void InputReader::OnDeviceHandleReady(MojoHandle handle) {
  InputDevice* device = devices_[handle].first;
  bool ret = device->Read(
      [this](mozart::EventPtr event) { callback_(std::move(event)); });
  if (!ret) {
    DeviceRemoved(device);
  }
}

void InputReader::OnHandleReady(MojoHandle handle) {
  if (input_directory_handle_ == handle) {
    OnDirectoryHandleReady(handle);
  } else if (devices_.count(handle)) {
    OnDeviceHandleReady(handle);
    return;
  }
}

void InputReader::OnHandleError(MojoHandle handle, MojoResult result) {
  DeviceRemoved(GetDevice(handle));
}

}  // namespace launcher

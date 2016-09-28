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
extern "C" {  // FIXME(jpoichet) remove once CL to fix watcher.h is checked in
#include <mxio/watcher.h>
}

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

inline uint32_t scale32(uint32_t z, uint32_t screen_dim, uint32_t rpt_dim) {
  return (z * screen_dim) / rpt_dim;
}

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
  if (fd_)
    close(fd_);
  if (handle_)
    mx_handle_close(handle_);

  if (report_desc_)
    free(report_desc_);
  if (ids_)
    free(ids_);
  if (report_)
    free(report_);
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
  ssize_t rc = ioctl_device_get_event_handle(device->fd_, &device->handle_);
  if (rc < 0) {
    FTL_LOG(ERROR) << "Could not convert file descriptor to handle";
    delete device;
    return nullptr;
  }

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

InputDeviceMonitor::InputDeviceMonitor() {}
InputDeviceMonitor::~InputDeviceMonitor() {}

void InputDeviceMonitor::Start() {
  // Start a thread that monitors /dev/input for new devices
  monitor_thread_ = std::thread(
      [](InputDeviceMonitor* monitor) {
        int dirfd = open(DEV_INPUT, O_DIRECTORY | O_RDONLY);
        if (dirfd < 0) {
          FTL_LOG(ERROR) << "Error opening " << DEV_INPUT;
          return;
        }
        mxio_watch_directory(
            dirfd,
            [](int dirfd, const char* fn, void* cookie) {
              InputDeviceMonitor* monitor =
                  reinterpret_cast<InputDeviceMonitor*>(cookie);
              monitor->DeviceAdded(dirfd, fn);
              return NO_ERROR;
            },
            monitor);
        close(dirfd);

      },
      this);
}

// This method is non-blocking and should be called from the caller's thread
void InputDeviceMonitor::CheckInput(const OnEventCallback& callback,
                                    const mojo::Size& display_size) {
  std::lock_guard<std::mutex> lock(devices_lock_);

  if (!devices_.size())
    return;

  int count = 0;
  for (InputDevice* device : devices_) {
    handles_[count] = device->handle_;
    wsigs_[count] = MX_SIGNAL_SIGNAL0;
    count++;
  }

  // Note: Timeout of 0 is actually clamped at 1ms and is the closest to
  // non-blocking we can get.
  mx_status_t rc =
      mx_handle_wait_many(count, handles_, wsigs_, 0, NULL, states_);
  if (rc == ERR_TIMED_OUT) {
    return;
  } else if (rc == NO_ERROR) {
    // Something has changed.
    while (--count >= 0) {
      // FIXME(jpoichet) figure out when device is gone
      if (states_[count].satisfied & MX_SIGNAL_SIGNAL0) {
        InputDevice* device = devices_.at(count);
        device->Read(callback, display_size);
      }
    }
  } else {
    switch (rc) {
      case ERR_BAD_STATE:
        FTL_LOG(ERROR) << "Checking input devices failed: Bad State";
        break;
      case ERR_INVALID_ARGS:
        FTL_LOG(ERROR) << "Checking input devices failed: Invalid Arguments";
        break;
      case ERR_ACCESS_DENIED:
        FTL_LOG(ERROR) << "Checking input devices failed: Access Denied";
        break;
      case ERR_BAD_HANDLE:
        FTL_LOG(ERROR) << "Checking input devices failed: Bad Handle";
        break;
      case ERR_HANDLE_CLOSED:
        FTL_LOG(ERROR) << "Checking input devices failed: Handle Closed";
        break;
      case ERR_NO_MEMORY:
        FTL_LOG(ERROR) << "Checking input devices failed: No Memory";
        break;
      default:
        FTL_LOG(ERROR) << "Checking input devices failed: " << rc;
    }
  }
}

// FIXME(jpoichet) we only read one report at a time though there could be more
// pending depending on the frequency CheckInput is called.
void InputDevice::Read(const OnEventCallback& callback,
                       const mojo::Size& display_size) {
  int rc = read(fd_, report_, max_report_len_);
  if (rc < 0) {
    FTL_LOG(ERROR) << "Failed to read report from " << name_ << " (r=" << rc
                   << ")";
    return;
  }
  Parse(callback, display_size);
}

// TODO(jpoichet) Need to convert keycode to proper windows_key_code
void KeyboardInputDevice::Parse(const OnEventCallback& callback,
                                const mojo::Size& display_size) {
  int64_t now = InputEventTimestampNow();
  uint8_t keycode;
  hid_kbd_parse_report(report_, &key_state_[current_index_]);
  // Get key pressed between previous and current state
  hid_kbd_pressed_keys(&key_state_[previous_index_],
                       &key_state_[current_index_], &key_delta_);
  // For all key pressed, generate a key event
  hid_for_every_key(&key_delta_, keycode) {
    mojo::EventPtr ev = mojo::Event::New();
    ev->action = mojo::EventType::KEY_PRESSED;
    ev->flags = mojo::EventFlags::NONE;
    ev->time_stamp = now;

    ev->key_data = mojo::KeyData::New();
    ev->key_data->key_code = 0;
    ev->key_data->is_char = false;
    ev->key_data->character = 0;
    ev->key_data->windows_key_code = mojo::KeyboardCode::UNKNOWN;
    ev->key_data->native_key_code = 0;
    ev->key_data->text = 0;
    ev->key_data->unmodified_text = 0;

    uint8_t ch = hid_map_key(keycode, modifiers_ & MOD_SHIFT, keymap_);
    if (ch) {
      mojo::KeyboardCode windows_key_code = mojo::KeyboardCode::BACK;  // FIXME
      uint16_t character16 = static_cast<unsigned char>(ch);
      ev->key_data->is_char = true;
      ev->key_data->character = character16;
      ev->key_data->windows_key_code = windows_key_code;
      ev->key_data->text = character16;
      ev->key_data->unmodified_text = character16;
    }

    switch (keycode) {
      case HID_USAGE_KEY_LEFT_SHIFT:
        modifiers_ |= MOD_LSHIFT;
        ev->key_data->windows_key_code = mojo::KeyboardCode::SHIFT;
        break;
      case HID_USAGE_KEY_RIGHT_SHIFT:
        modifiers_ |= MOD_RSHIFT;
        ev->key_data->windows_key_code = mojo::KeyboardCode::SHIFT;
        break;
      case HID_USAGE_KEY_LEFT_CTRL:
        modifiers_ |= MOD_LCTRL;
        ev->key_data->windows_key_code = mojo::KeyboardCode::CONTROL;
        break;
      case HID_USAGE_KEY_RIGHT_CTRL:
        modifiers_ |= MOD_RCTRL;
        ev->key_data->windows_key_code = mojo::KeyboardCode::CONTROL;
        break;
      case HID_USAGE_KEY_LEFT_ALT:
        modifiers_ |= MOD_LALT;
        ev->key_data->windows_key_code = mojo::KeyboardCode::MENU;
        break;
      case HID_USAGE_KEY_RIGHT_ALT:
        modifiers_ |= MOD_RALT;
        ev->key_data->windows_key_code = mojo::KeyboardCode::MENU;
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
    mojo::EventPtr ev = mojo::Event::New();
    ev->action = mojo::EventType::KEY_RELEASED;
    ev->flags = mojo::EventFlags::NONE;
    ev->time_stamp = now;

    ev->key_data = mojo::KeyData::New();
    ev->key_data->key_code = 0;
    ev->key_data->is_char = false;
    ev->key_data->character = 0;
    ev->key_data->windows_key_code = mojo::KeyboardCode::UNKNOWN;
    ev->key_data->native_key_code = 0;
    ev->key_data->text = 0;
    ev->key_data->unmodified_text = 0;

    uint8_t ch = hid_map_key(keycode, modifiers_ & MOD_SHIFT, keymap_);
    if (ch) {
      mojo::KeyboardCode windows_key_code = mojo::KeyboardCode::BACK;  // FIXME
      uint16_t character16 = static_cast<unsigned char>(ch);
      ev->key_data->is_char = true;
      ev->key_data->character = character16;
      ev->key_data->windows_key_code = windows_key_code;
      ev->key_data->text = character16;
      ev->key_data->unmodified_text = character16;
    }

    switch (keycode) {
      case HID_USAGE_KEY_LEFT_SHIFT:
        modifiers_ &= (~MOD_LSHIFT);
        ev->key_data->windows_key_code = mojo::KeyboardCode::SHIFT;
        break;
      case HID_USAGE_KEY_RIGHT_SHIFT:
        modifiers_ &= (~MOD_RSHIFT);
        ev->key_data->windows_key_code = mojo::KeyboardCode::SHIFT;

        break;
      case HID_USAGE_KEY_LEFT_CTRL:
        modifiers_ &= (~MOD_LCTRL);
        ev->key_data->windows_key_code = mojo::KeyboardCode::CONTROL;

        break;
      case HID_USAGE_KEY_RIGHT_CTRL:
        modifiers_ &= (~MOD_RCTRL);
        ev->key_data->windows_key_code = mojo::KeyboardCode::CONTROL;

        break;
      case HID_USAGE_KEY_LEFT_ALT:
        modifiers_ &= (~MOD_LALT);
        ev->key_data->windows_key_code = mojo::KeyboardCode::MENU;

        break;
      case HID_USAGE_KEY_RIGHT_ALT:
        modifiers_ &= (~MOD_RALT);
        ev->key_data->windows_key_code = mojo::KeyboardCode::MENU;

        break;
    }
    callback(std::move(ev));
  }

  // swap key states
  current_index_ = 1 - current_index_;
  previous_index_ = 1 - previous_index_;
}

void MouseInputDevice::Parse(const OnEventCallback& callback,
                             const mojo::Size& display_size) {
  // TODO(jpoichet) Generate mojo::Event from boot_mouse_report_t* report =
  // reinterpret_cast<boot_mouse_report_t*>(report_);
}

// TODO(jpoichet) scale x, y to display size
void Acer12InputDevice::ParseStylus(const OnEventCallback& callback,
                                    const mojo::Size& display_size) {
  acer12_stylus_t* report = reinterpret_cast<acer12_stylus_t*>(report_);

  // Don't generic events for out of range or hover with no switches
  if (!report->status || report->status == ACER12_STYLUS_STATUS_INRANGE)
    return;

  mojo::EventPtr ev = mojo::Event::New();
  ev->action = mojo::EventType::POINTER_DOWN;
  ev->flags = mojo::EventFlags::NONE;
  ev->time_stamp = InputEventTimestampNow();

  ev->pointer_data = mojo::PointerData::New();
  ev->pointer_data->pointer_id = report->rpt_id;
  ev->pointer_data->kind = mojo::PointerKind::TOUCH;

  uint32_t x = scale32(report->x, display_size.width, ACER12_STYLUS_X_MAX);
  uint32_t y = scale32(report->y, display_size.height, ACER12_STYLUS_Y_MAX);

  // |x| and |y| are in the coordinate system of the View.
  ev->pointer_data->x = x;
  ev->pointer_data->y = y;
  // |screen_x| and |screen_y| are in screen coordinates.
  ev->pointer_data->screen_x = x;
  ev->pointer_data->screen_y = y;
  ev->pointer_data->pressure = report->pressure;
  ev->pointer_data->radius_major = report->pressure >> 4;
  ev->pointer_data->radius_minor = report->pressure >> 4;
  ev->pointer_data->orientation = 0.;
  // Used for devices that support wheels. Ranges from -1 to 1.
  ev->pointer_data->horizontal_wheel = 0.;
  ev->pointer_data->vertical_wheel = 0.;

  callback(std::move(ev));
}

// TODO(jpoichet) scale x, y to display size
void Acer12InputDevice::ParseTouchscreen(const OnEventCallback& callback,
                                         const mojo::Size& display_size) {
  acer12_touch_t* report = reinterpret_cast<acer12_touch_t*>(report_);

  int64_t now = InputEventTimestampNow();
  std::vector<mojo::PointerData> old_pointers = pointers_;
  pointers_.clear();

  // Only 5 touches per report
  for (uint8_t c = 0; c < 5; c++) {
    if (!acer12_finger_id_tswitch(report->fingers[c].finger_id))
      continue;

    uint32_t width = 2 * report->fingers[c].width;
    uint32_t height = 2 * report->fingers[c].height;

    int pointer_id = acer12_finger_id_contact(report->fingers[c].finger_id);

    auto ev = mojo::Event::New();
    ev->action = mojo::EventType::POINTER_DOWN;
    for (auto it = old_pointers.begin(); it != old_pointers.end(); ++it) {
      if (it->pointer_id == pointer_id) {
        ev->action = mojo::EventType::POINTER_MOVE;
        old_pointers.erase(it);
        break;
      }
    }
    ev->flags = mojo::EventFlags::NONE;
    ev->time_stamp = now;

    ev->pointer_data = mojo::PointerData::New();
    ev->pointer_data->pointer_id = pointer_id;
    ev->pointer_data->kind = mojo::PointerKind::TOUCH;

    uint32_t x =
        scale32(report->fingers[c].x, display_size.width, ACER12_X_MAX);
    uint32_t y =
        scale32(report->fingers[c].y, display_size.height, ACER12_Y_MAX);

    // |x| and |y| are in the coordinate system of the View.
    ev->pointer_data->x = x;
    ev->pointer_data->y = y;
    // |screen_x| and |screen_y| are in screen coordinates.
    ev->pointer_data->screen_x = x;
    ev->pointer_data->screen_y = y;
    ev->pointer_data->pressure = 0.;
    ev->pointer_data->radius_major = width > height ? width : height;
    ev->pointer_data->radius_minor = width > height ? height : width;
    ev->pointer_data->orientation = 0.;
    // Used for devices that support wheels. Ranges from -1 to 1.
    ev->pointer_data->horizontal_wheel = 0.;
    ev->pointer_data->vertical_wheel = 0.;
    pointers_.push_back(*ev->pointer_data);

    callback(std::move(ev));
  }

  for (const auto& pointer : old_pointers) {
    auto ev = mojo::Event::New();
    ev->action = mojo::EventType::POINTER_UP;
    ev->flags = mojo::EventFlags::NONE;
    ev->time_stamp = now;
    ev->pointer_data = pointer.Clone();

    callback(std::move(ev));
  }
}

void Acer12InputDevice::Parse(const OnEventCallback& callback,
                              const mojo::Size& display_size) {
  if (*(uint8_t*)report_ == ACER12_RPT_ID_TOUCH) {
    ParseTouchscreen(callback, display_size);
  } else if (*(uint8_t*)report_ == ACER12_RPT_ID_STYLUS) {
    ParseStylus(callback, display_size);
  }
}

void InputDeviceMonitor::DeviceAdded(int dirfd, const char* fn) {
  FTL_LOG(INFO) << "Input device " << fn << " added";
  int fd = openat(dirfd, fn, O_RDONLY);
  if (fd < 0) {
    FTL_LOG(ERROR) << "Failed to open device " << fn;
    return;
  }

  InputDevice* device = InputDevice::BuildInputDevice(fd, fn);
  if (!device)
    return;

  devices_lock_.lock();
  devices_.push_back(device);
  devices_lock_.unlock();
}

}  // namespace launcher

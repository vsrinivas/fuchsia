// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/input_interpreter.h"

#include <fcntl.h>
#include <hid/acer12.h>
#include <hid/hid.h>
#include <hid/paradise.h>
#include <hid/samsung.h>
#include <hid/usages.h>
#include <magenta/device/device.h>
#include <magenta/device/input.h>
#include <magenta/types.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <trace/event.h>

#include "lib/ui/input/cpp/formatting.h"
#include "lib/ui/input/fidl/usages.fidl.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/time/time_point.h"

namespace {
int64_t InputEventTimestampNow() {
  return ftl::TimePoint::Now().ToEpochDelta().ToNanoseconds();
}
}  // namespace

namespace mozart {
namespace input {

std::unique_ptr<InputInterpreter> InputInterpreter::Open(
    int dirfd,
    std::string filename,
    mozart::InputDeviceRegistry* registry) {
  int fd = openat(dirfd, filename.c_str(), O_RDONLY);
  if (fd < 0) {
    FTL_LOG(ERROR) << "Failed to open device " << filename;
    return nullptr;
  }

  std::unique_ptr<InputInterpreter> device(
      new InputInterpreter(filename, fd, registry));
  if (!device->Initialize()) {
    return nullptr;
  }

  return device;
}

InputInterpreter::InputInterpreter(std::string name,
                                   int fd,
                                   mozart::InputDeviceRegistry* registry)
    : fd_(fd), name_(std::move(name)), registry_(registry) {
  memset(acer12_touch_reports_, 0, 2 * sizeof(acer12_touch_t));
}

InputInterpreter::~InputInterpreter() {}

bool InputInterpreter::Initialize() {
  int protocol;
  if (!GetProtocol(&protocol)) {
    FTL_LOG(ERROR) << "Failed to retrieve HID protocol for " << name_;
    return false;
  }

  if (protocol == INPUT_PROTO_KBD) {
    FTL_VLOG(2) << "Device " << name_ << " has keyboard";
    has_keyboard_ = true;
    keyboard_descriptor_ = mozart::KeyboardDescriptor::New();
    keyboard_descriptor_->keys.resize(HID_USAGE_KEY_RIGHT_GUI -
                                      HID_USAGE_KEY_A + 1);
    for (size_t index = HID_USAGE_KEY_A; index <= HID_USAGE_KEY_RIGHT_GUI;
         ++index) {
      keyboard_descriptor_->keys[index - HID_USAGE_KEY_A] = index;
    }

    keyboard_report_ = mozart::InputReport::New();
    keyboard_report_->keyboard = mozart::KeyboardReport::New();
  } else if (protocol == INPUT_PROTO_MOUSE) {
    FTL_VLOG(2) << "Device " << name_ << " has mouse";
    has_mouse_ = true;
    mouse_descriptor_ = mozart::MouseDescriptor::New();
    mouse_descriptor_->rel_x = mozart::Axis::New();
    mouse_descriptor_->rel_x->range = mozart::Range::New();
    mouse_descriptor_->rel_x->range->min = INT32_MIN;
    mouse_descriptor_->rel_x->range->max = INT32_MAX;
    mouse_descriptor_->rel_x->resolution = 1;

    mouse_descriptor_->rel_y = mozart::Axis::New();
    mouse_descriptor_->rel_y->range = mozart::Range::New();
    mouse_descriptor_->rel_y->range->min = INT32_MIN;
    mouse_descriptor_->rel_y->range->max = INT32_MAX;
    mouse_descriptor_->rel_y->resolution = 1;

    mouse_descriptor_->buttons |= kMouseButtonPrimary;
    mouse_descriptor_->buttons |= kMouseButtonSecondary;
    mouse_descriptor_->buttons |= kMouseButtonTertiary;

    mouse_report_ = mozart::InputReport::New();
    mouse_report_->mouse = mozart::MouseReport::New();
  } else if (protocol == INPUT_PROTO_NONE) {
    size_t report_desc_len;
    if (!GetReportDescriptionLength(&report_desc_len)) {
      FTL_LOG(ERROR) << "Failed to retrieve HID description length for "
                     << name_;
      return false;
    }

    std::vector<uint8_t> desc(report_desc_len);
    if (!GetReportDescription(desc.data(), desc.size())) {
      FTL_LOG(ERROR) << "Failed to retrieve HID description for " << name_;
      return false;
    }

    if (is_acer12_touch_report_desc(desc.data(), desc.size())) {
      mx_status_t setup_res = setup_acer12_touch(fd_);
      if (setup_res != MX_OK) {
        FTL_LOG(ERROR) << "Failed to setup Acer12 touch (res " << setup_res
                       << ")";
        return false;
      }

      FTL_VLOG(2) << "Device " << name_ << " has stylus";
      has_stylus_ = true;
      stylus_descriptor_ = mozart::StylusDescriptor::New();
      stylus_descriptor_->x = mozart::Axis::New();
      stylus_descriptor_->x->range = mozart::Range::New();
      stylus_descriptor_->x->range->min = 0;
      stylus_descriptor_->x->range->max = ACER12_STYLUS_X_MAX;
      stylus_descriptor_->x->resolution = 1;

      stylus_descriptor_->y = mozart::Axis::New();
      stylus_descriptor_->y->range = mozart::Range::New();
      stylus_descriptor_->y->range->min = 0;
      stylus_descriptor_->y->range->max = ACER12_STYLUS_Y_MAX;
      stylus_descriptor_->y->resolution = 1;

      stylus_descriptor_->is_invertible = false;

      stylus_descriptor_->buttons |= kStylusBarrel;

      stylus_report_ = mozart::InputReport::New();
      stylus_report_->stylus = mozart::StylusReport::New();

      FTL_VLOG(2) << "Device " << name_ << " has touchscreen";
      has_touchscreen_ = true;
      touchscreen_descriptor_ = mozart::TouchscreenDescriptor::New();
      touchscreen_descriptor_->x = mozart::Axis::New();
      touchscreen_descriptor_->x->range = mozart::Range::New();
      touchscreen_descriptor_->x->range->min = 0;
      touchscreen_descriptor_->x->range->max = ACER12_X_MAX;
      touchscreen_descriptor_->x->resolution = 1;

      touchscreen_descriptor_->y = mozart::Axis::New();
      touchscreen_descriptor_->y->range = mozart::Range::New();
      touchscreen_descriptor_->y->range->min = 0;
      touchscreen_descriptor_->y->range->max = ACER12_Y_MAX;
      touchscreen_descriptor_->y->resolution = 1;

      // TODO(jpoichet) do not hardcode this
      touchscreen_descriptor_->max_finger_id = 255;

      touchscreen_report_ = mozart::InputReport::New();
      touchscreen_report_->touchscreen = mozart::TouchscreenReport::New();

      touch_device_type_ = TouchDeviceType::ACER12;
    } else if (is_samsung_touch_report_desc(desc.data(), desc.size())) {
      mx_status_t setup_res = setup_samsung_touch(fd_);
      if (setup_res != MX_OK) {
        FTL_LOG(ERROR) << "Failed to setup Samsung touch (res " << setup_res
                       << ")";
        return false;
      }

      FTL_VLOG(2) << "Device " << name_ << " has touchscreen";
      has_touchscreen_ = true;
      touchscreen_descriptor_ = mozart::TouchscreenDescriptor::New();
      touchscreen_descriptor_->x = mozart::Axis::New();
      touchscreen_descriptor_->x->range = mozart::Range::New();
      touchscreen_descriptor_->x->range->min = 0;
      touchscreen_descriptor_->x->range->max = SAMSUNG_X_MAX;
      touchscreen_descriptor_->x->resolution = 1;

      touchscreen_descriptor_->y = mozart::Axis::New();
      touchscreen_descriptor_->y->range = mozart::Range::New();
      touchscreen_descriptor_->y->range->min = 0;
      touchscreen_descriptor_->y->range->max = SAMSUNG_Y_MAX;
      touchscreen_descriptor_->y->resolution = 1;

      // TODO(jpoichet) do not hardcode this
      touchscreen_descriptor_->max_finger_id = 255;

      touchscreen_report_ = mozart::InputReport::New();
      touchscreen_report_->touchscreen = mozart::TouchscreenReport::New();

      touch_device_type_ = TouchDeviceType::SAMSUNG;
    } else if (is_paradise_touch_report_desc(desc.data(), desc.size())) {
      mx_status_t setup_res = setup_paradise_touch(fd_);
      if (setup_res != MX_OK) {
        FTL_LOG(ERROR) << "Failed to setup Paradise touch (res " << setup_res
                       << ")";
        return false;
      }

      // TODO(cpu): Add support for stylus.

      FTL_VLOG(2) << "Device " << name_ << " has touchscreen";
      has_touchscreen_ = true;
      touchscreen_descriptor_ = mozart::TouchscreenDescriptor::New();
      touchscreen_descriptor_->x = mozart::Axis::New();
      touchscreen_descriptor_->x->range = mozart::Range::New();
      touchscreen_descriptor_->x->range->min = 0;
      touchscreen_descriptor_->x->range->max = PARADISE_X_MAX;
      touchscreen_descriptor_->x->resolution = 1;

      touchscreen_descriptor_->y = mozart::Axis::New();
      touchscreen_descriptor_->y->range = mozart::Range::New();
      touchscreen_descriptor_->y->range->min = 0;
      touchscreen_descriptor_->y->range->max = PARADISE_Y_MAX;
      touchscreen_descriptor_->y->resolution = 1;

      // TODO(cpu) do not hardcode |max_finger_id|.
      touchscreen_descriptor_->max_finger_id = 255;

      touchscreen_report_ = mozart::InputReport::New();
      touchscreen_report_->touchscreen = mozart::TouchscreenReport::New();

      touch_device_type_ = TouchDeviceType::PARADISE;
    } else {
      FTL_VLOG(2) << "Device " << name_ << " has unsupported HID device";
      return false;
    }
  } else {
    FTL_VLOG(2) << "Device " << name_ << " has unsupported HID protocol";
    return false;
  }

  // Get event handle for file descriptor
  mx_handle_t handle;
  ssize_t rc = ioctl_device_get_event_handle(fd_, &handle);
  if (rc < 0) {
    FTL_LOG(ERROR) << "Could not convert file descriptor to handle";
    return false;
  }

  event_.reset(handle);

  if (!GetMaxReportLength(&max_report_len_)) {
    FTL_LOG(ERROR) << "Failed to retrieve maximum HID report length for "
                   << name_;
    return false;
  }

  report_.reserve(max_report_len_);

  NotifyRegistry();

  return true;
}

void InputInterpreter::NotifyRegistry() {
  mozart::DeviceDescriptorPtr descriptor = mozart::DeviceDescriptor::New();
  if (has_keyboard_) {
    descriptor->keyboard = keyboard_descriptor_.Clone();
  }
  if (has_mouse_) {
    descriptor->mouse = mouse_descriptor_.Clone();
  }
  if (has_stylus_) {
    descriptor->stylus = stylus_descriptor_.Clone();
  }
  if (has_touchscreen_) {
    descriptor->touchscreen = touchscreen_descriptor_.Clone();
  }

  registry_->RegisterDevice(std::move(descriptor), input_device_.NewRequest());
}

bool InputInterpreter::Read(bool discard) {
  int rc = read(fd_, report_.data(), max_report_len_);
  if (rc < 1) {
    FTL_LOG(ERROR) << "Failed to read from input: " << rc;
    // TODO(jpoichet) check whether the device was actually closed or not
    return false;
  }

  TRACE_DURATION("input", "Read");
  if (has_keyboard_) {
    ParseKeyboardReport(report_.data(), rc);
    if (!discard) {
      input_device_->DispatchReport(keyboard_report_.Clone());
    }
  }

  if (has_mouse_) {
    ParseMouseReport(report_.data(), rc);
    if (!discard) {
      input_device_->DispatchReport(mouse_report_.Clone());
    }
  }

  switch (touch_device_type_) {
    case TouchDeviceType::ACER12:
      if (report_[0] == ACER12_RPT_ID_STYLUS) {
        if (ParseAcer12StylusReport(report_.data(), rc)) {
          if (!discard) {
            input_device_->DispatchReport(stylus_report_.Clone());
          }
        }
      } else if (report_[0] == ACER12_RPT_ID_TOUCH) {
        if (ParseAcer12TouchscreenReport(report_.data(), rc)) {
          if (!discard) {
            input_device_->DispatchReport(touchscreen_report_.Clone());
          }
        }
      }
      break;

    case TouchDeviceType::SAMSUNG:
      if (report_[0] == SAMSUNG_RPT_ID_TOUCH) {
        if (ParseSamsungTouchscreenReport(report_.data(), rc)) {
          if (!discard) {
            input_device_->DispatchReport(touchscreen_report_.Clone());
          }
        }
      }
      break;

    case TouchDeviceType::PARADISE:
      if (report_[0] == PARADISE_RPT_ID_TOUCH) {
        if (ParseParadiseTouchscreenReport(report_.data(), rc)) {
          if (!discard) {
            input_device_->DispatchReport(touchscreen_report_.Clone());
          }
        }
      }
      break;

    default:
      break;
  }

  return true;
}

void InputInterpreter::ParseKeyboardReport(uint8_t* report, size_t len) {
  hid_keys_t key_state;
  uint8_t keycode;
  hid_kbd_parse_report(report, &key_state);
  keyboard_report_->event_time = InputEventTimestampNow();

  size_t index = 0;
  keyboard_report_->keyboard->pressed_keys.resize(index);
  hid_for_every_key(&key_state, keycode) {
    keyboard_report_->keyboard->pressed_keys.resize(index + 1);
    keyboard_report_->keyboard->pressed_keys[index] = keycode;
    index++;
  }
  FTL_VLOG(2) << name_ << " parsed: " << *keyboard_report_;
}

void InputInterpreter::ParseMouseReport(uint8_t* r, size_t len) {
  auto report = reinterpret_cast<boot_mouse_report_t*>(r);
  mouse_report_->event_time = InputEventTimestampNow();

  mouse_report_->mouse->rel_x = report->rel_x;
  mouse_report_->mouse->rel_y = report->rel_y;
  mouse_report_->mouse->pressed_buttons = report->buttons;
  FTL_VLOG(2) << name_ << " parsed: " << *mouse_report_;
}

bool InputInterpreter::ParseAcer12StylusReport(uint8_t* r, size_t len) {
  if (len != sizeof(acer12_stylus_t)) {
    return false;
  }

  auto report = reinterpret_cast<acer12_stylus_t*>(r);
  stylus_report_->event_time = InputEventTimestampNow();

  stylus_report_->stylus->x = report->x;
  stylus_report_->stylus->y = report->y;
  stylus_report_->stylus->pressure = report->pressure;

  stylus_report_->stylus->is_in_contact =
      acer12_stylus_status_inrange(report->status) &&
      (acer12_stylus_status_tswitch(report->status) ||
       acer12_stylus_status_eraser(report->status));

  stylus_report_->stylus->in_range =
      acer12_stylus_status_inrange(report->status);

  if (acer12_stylus_status_invert(report->status) ||
      acer12_stylus_status_eraser(report->status)) {
    stylus_report_->stylus->is_inverted = true;
  }

  if (acer12_stylus_status_barrel(report->status)) {
    stylus_report_->stylus->pressed_buttons |= kStylusBarrel;
  }
  FTL_VLOG(2) << name_ << " parsed: " << *stylus_report_;

  return true;
}

bool InputInterpreter::ParseAcer12TouchscreenReport(uint8_t* r, size_t len) {
  if (len != sizeof(acer12_touch_t)) {
    return false;
  }

  // Acer12 touch reports come in pairs when there are more than 5 fingers
  // First report has the actual number of fingers stored in contact_count,
  // second report will have a contact_count of 0.
  auto report = reinterpret_cast<acer12_touch_t*>(r);
  if (report->contact_count > 0) {
    acer12_touch_reports_[0] = *report;
  } else {
    acer12_touch_reports_[1] = *report;
  }
  touchscreen_report_->event_time = InputEventTimestampNow();

  size_t index = 0;
  touchscreen_report_->touchscreen->touches.resize(index);

  for (uint8_t i = 0; i < 2; i++) {
    // Only 5 touches per report
    for (uint8_t c = 0; c < 5; c++) {
      auto fid = acer12_touch_reports_[i].fingers[c].finger_id;

      if (!acer12_finger_id_tswitch(fid))
        continue;
      mozart::TouchPtr touch = mozart::Touch::New();
      touch->finger_id = acer12_finger_id_contact(fid);
      touch->x = acer12_touch_reports_[i].fingers[c].x;
      touch->y = acer12_touch_reports_[i].fingers[c].y;
      touch->width = acer12_touch_reports_[i].fingers[c].width;
      touch->height = acer12_touch_reports_[i].fingers[c].height;
      touchscreen_report_->touchscreen->touches.resize(index + 1);
      touchscreen_report_->touchscreen->touches[index++] = std::move(touch);
    }
  }
  FTL_VLOG(2) << name_ << " parsed: " << *touchscreen_report_;
  return true;
}

bool InputInterpreter::ParseSamsungTouchscreenReport(uint8_t* r, size_t len) {
  if (len != sizeof(samsung_touch_t)) {
    return false;
  }

  const auto& report = *(reinterpret_cast<samsung_touch_t*>(r));
  touchscreen_report_->event_time = InputEventTimestampNow();

  size_t index = 0;
  touchscreen_report_->touchscreen->touches.resize(index);

  for (size_t i = 0; i < countof(report.fingers); ++i) {
    auto fid = report.fingers[i].finger_id;

    if (!samsung_finger_id_tswitch(fid))
      continue;

    mozart::TouchPtr touch = mozart::Touch::New();
    touch->finger_id = samsung_finger_id_contact(fid);
    touch->x = report.fingers[i].x;
    touch->y = report.fingers[i].y;
    touch->width = report.fingers[i].width;
    touch->height = report.fingers[i].height;
    touchscreen_report_->touchscreen->touches.resize(index + 1);
    touchscreen_report_->touchscreen->touches[index++] = std::move(touch);
  }

  return true;
}

bool InputInterpreter::ParseParadiseTouchscreenReport(uint8_t* r, size_t len) {
  if (len != sizeof(paradise_touch_t)) {
    FTL_LOG(INFO) << "paradise wrong size " << len;
    return false;
  }

  const auto& report = *(reinterpret_cast<paradise_touch_t*>(r));
  touchscreen_report_->event_time = InputEventTimestampNow();

  size_t index = 0;
  touchscreen_report_->touchscreen->touches.resize(index);

  for (size_t i = 0; i < countof(report.fingers); ++i) {
    if (!paradise_finger_flags_tswitch(report.fingers[i].flags))
      continue;

    mozart::TouchPtr touch = mozart::Touch::New();
    touch->finger_id = report.fingers[i].finger_id;
    touch->x = report.fingers[i].x;
    touch->y = report.fingers[i].y;
    touch->width = 5;   // TODO(cpu): Don't hardcode |width| or |height|.
    touch->height = 5;
    touchscreen_report_->touchscreen->touches.resize(index + 1);
    touchscreen_report_->touchscreen->touches[index++] = std::move(touch);
  }

  FTL_VLOG(2) << name_ << " parsed: " << *touchscreen_report_;
  return true;
}


mx_status_t InputInterpreter::GetProtocol(int* out_proto) {
  ssize_t rc = ioctl_input_get_protocol(fd_, out_proto);
  if (rc < 0) {
    FTL_LOG(ERROR) << "hid: could not get protocol from " << name_
                   << " (status=" << rc << ")";
  }
  return rc;
}

mx_status_t InputInterpreter::GetReportDescriptionLength(
    size_t* out_report_desc_len) {
  ssize_t rc = ioctl_input_get_report_desc_size(fd_, out_report_desc_len);
  if (rc < 0) {
    FTL_LOG(ERROR) << "hid: could not get report descriptor length from "
                   << name_ << "  (status=" << rc << ")";
  }
  return rc;
}

mx_status_t InputInterpreter::GetReportDescription(uint8_t* out_buf,
                                                   size_t out_report_desc_len) {
  ssize_t rc = ioctl_input_get_report_desc(fd_, out_buf, out_report_desc_len);
  if (rc < 0) {
    FTL_LOG(ERROR) << "hid: could not get report descriptor from " << name_
                   << " (status=" << rc << ")";
  }
  return rc;
}

mx_status_t InputInterpreter::GetMaxReportLength(
    input_report_size_t* out_max_report_len) {
  ssize_t rc = ioctl_input_get_max_reportsize(fd_, out_max_report_len);
  if (rc < 0) {
    FTL_LOG(ERROR) << "hid: could not get max report size from " << name_
                   << " (status=" << rc << ")";
  }
  return rc;
}

}  // namespace input
}  // namespace mozart

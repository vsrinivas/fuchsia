// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/input_reader/input_device.h"

#include <fcntl.h>
#include <hid/acer12.h>
#include <hid/hid.h>
#include <hid/usages.h>
#include <magenta/device/device.h>
#include <magenta/device/input.h>
#include <magenta/types.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include "apps/mozart/src/input_reader/input_report.h"
#include "apps/tracing/lib/trace/event.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/time/time_point.h"

namespace mozart {
namespace input {

std::unique_ptr<InputDevice> InputDevice::Open(int dirfd,
                                               std::string filename,
                                               uint32_t id) {
  int fd = openat(dirfd, filename.c_str(), O_RDONLY);
  if (fd < 0) {
    FTL_LOG(ERROR) << "Failed to open device " << filename;
    return nullptr;
  }

  std::unique_ptr<InputDevice> device(new InputDevice(filename, fd, id));
  if (!device->Initialize()) {
    return nullptr;
  }

  return device;
}

InputDevice::InputDevice(std::string name, int fd, uint32_t id)
    : fd_(fd), name_(std::move(name)), id_(id) {
  memset(acer12_touch_reports_, 0, 2 * sizeof(acer12_touch_t));
}

InputDevice::~InputDevice() {}

bool InputDevice::Initialize() {
  int protocol;
  if (!GetProtocol(&protocol)) {
    FTL_LOG(ERROR) << "Failed to retrieve HID protocol for " << name_;
    return false;
  }

  if (protocol == INPUT_PROTO_KBD) {
    has_keyboard_ = true;
    keyboard_descriptor_.AddKeyRange(HID_USAGE_KEY_A, HID_USAGE_KEY_RIGHT_GUI);
  } else if (protocol == INPUT_PROTO_MOUSE) {
    has_mouse_ = true;
    mouse_descriptor_.AddButton(INPUT_USAGE_BUTTON_PRIMARY);
    mouse_descriptor_.AddButton(INPUT_USAGE_BUTTON_SECONDARY);
    mouse_descriptor_.AddButton(INPUT_USAGE_BUTTON_TERTIARY);
    mouse_descriptor_.rel_x = MakeAxis<int32_t>(INT32_MIN, INT32_MAX, 1);
    mouse_descriptor_.rel_y = MakeAxis<int32_t>(INT32_MIN, INT32_MAX, 1);
  } else if (protocol == INPUT_PROTO_NONE) {
    size_t report_desc_len;
    if (!GetReportDescriptionLength(&report_desc_len)) {
      FTL_LOG(ERROR) << "Failed to retrieve HID description length for "
                     << name_;
      return false;
    }
    if (report_desc_len != ACER12_RPT_DESC_LEN) {
      return false;
    }

    uint8_t desc[ACER12_RPT_DESC_LEN];
    if (!GetReportDescription(desc, report_desc_len)) {
      FTL_LOG(ERROR) << "Failed to retrieve HID description for " << name_;
      return false;
    }

    if (!memcmp(desc, acer12_touch_report_desc, ACER12_RPT_DESC_LEN)) {
      has_stylus_ = true;
      stylus_descriptor_.x = MakeAxis<uint32_t>(0, ACER12_STYLUS_X_MAX, 1);
      stylus_descriptor_.y = MakeAxis<uint32_t>(0, ACER12_STYLUS_Y_MAX, 1);
      stylus_descriptor_.AddButton(INPUT_USAGE_BUTTON_PRIMARY);
      stylus_descriptor_.AddButton(INPUT_USAGE_BUTTON_SECONDARY);
      stylus_descriptor_.AddButton(INPUT_USAGE_BUTTON_TERTIARY);

      has_touchscreen_ = true;
      touchscreen_descriptor_.x = MakeAxis<uint32_t>(0, ACER12_X_MAX, 1);
      touchscreen_descriptor_.y = MakeAxis<uint32_t>(0, ACER12_Y_MAX, 1);
    } else {
      return false;
    }
  } else {
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
  return true;
}

bool InputDevice::Read(const OnReportCallback& callback) {
  int rc = read(fd_, report_.data(), max_report_len_);
  if (rc < 0) {
    FTL_LOG(ERROR) << "Failed to read from input: " << rc;
    // TODO(jpoichet) check whether the device was actually closed or not
    return false;
  }

  TRACE_DURATION("input", "Read");
  std::vector<InputReport::ReportType> pending;
  if (has_keyboard_) {
    ParseKeyboardReport(report_.data(), rc);
    pending.push_back(InputReport::ReportType::kKeyboard);
  }

  if (has_mouse_) {
    ParseMouseReport(report_.data(), rc);
    pending.push_back(InputReport::ReportType::kMouse);
  }

  if (has_stylus_ && report_[0] == ACER12_RPT_ID_STYLUS) {
    ParseStylusReport(report_.data(), rc);
    pending.push_back(InputReport::ReportType::kStylus);
  }

  if (has_touchscreen_ && report_[0] == ACER12_RPT_ID_TOUCH) {
    ParseTouchscreenReport(report_.data(), rc);
    pending.push_back(InputReport::ReportType::kTouchscreen);
  }

  for (auto type : pending) {
    callback(type);
  }

  return true;
}

void InputDevice::ParseKeyboardReport(uint8_t* report, size_t len) {
  hid_keys_t key_state;
  uint8_t keycode;
  hid_kbd_parse_report(report, &key_state);
  keyboard_report_.timestamp = ftl::TimePoint::Now();
  keyboard_report_.down.clear();
  hid_for_every_key(&key_state, keycode) {
    keyboard_report_.down.push_back(keycode);
  }
}

void InputDevice::ParseMouseReport(uint8_t* r, size_t len) {
  auto report = reinterpret_cast<boot_mouse_report_t*>(r);
  mouse_report_.timestamp = ftl::TimePoint::Now();
  mouse_report_.rel_x = report->rel_x;
  mouse_report_.rel_y = report->rel_y;
  mouse_report_.buttons = report->buttons;
}

void InputDevice::ParseStylusReport(uint8_t* r, size_t len) {
  auto report = reinterpret_cast<acer12_stylus_t*>(r);
  stylus_report_.timestamp = ftl::TimePoint::Now();
  stylus_report_.x = report->x;
  stylus_report_.y = report->y;
  stylus_report_.pressure = report->pressure;

  stylus_report_.in_range = acer12_stylus_status_inrange(report->status);
  stylus_report_.is_down = acer12_stylus_status_inrange(report->status) &&
                           (acer12_stylus_status_tswitch(report->status) ||
                            acer12_stylus_status_eraser(report->status));

  // TODO(jpoichet) TIP, INVERT and ERASER aren't all buttons
  stylus_report_.down.clear();
  if (acer12_stylus_status_tswitch(report->status)) {
    stylus_report_.down.push_back(INPUT_USAGE_STYLUS_TIP);
  }
  if (acer12_stylus_status_barrel(report->status)) {
    stylus_report_.down.push_back(INPUT_USAGE_STYLUS_BARREL);
  }
  if (acer12_stylus_status_invert(report->status)) {
    stylus_report_.down.push_back(INPUT_USAGE_STYLUS_INVERT);
  }
  if (acer12_stylus_status_eraser(report->status)) {
    stylus_report_.down.push_back(INPUT_USAGE_STYLUS_ERASER);
  }
}

void InputDevice::ParseTouchscreenReport(uint8_t* r, size_t len) {
  // Acer12 touch reports come in pairs when there are more than 5 fingers
  // First report has the actual number of fingers stored in contact_count,
  // second report will have a contact_count of 0.
  auto report = reinterpret_cast<acer12_touch_t*>(r);
  if (report->contact_count > 0) {
    acer12_touch_reports_[0] = *report;
  } else {
    acer12_touch_reports_[1] = *report;
  }
  touch_report_.timestamp = ftl::TimePoint::Now();
  touch_report_.touches.clear();

  for (uint8_t i = 0; i < 2; i++) {
    // Only 5 touches per report
    for (uint8_t c = 0; c < 5; c++) {
      if (!acer12_finger_id_tswitch(
              acer12_touch_reports_[i].fingers[c].finger_id))
        continue;
      Touch touch;
      touch.finger_id = acer12_touch_reports_[i].fingers[c].finger_id;
      touch.x = acer12_touch_reports_[i].fingers[c].x;
      touch.y = acer12_touch_reports_[i].fingers[c].y;
      touch.width = acer12_touch_reports_[i].fingers[c].width;
      touch.height = acer12_touch_reports_[i].fingers[c].height;
      touch_report_.touches.push_back(touch);
    }
  }
}

mx_status_t InputDevice::GetProtocol(int* out_proto) {
  ssize_t rc = ioctl_input_get_protocol(fd_, out_proto);
  if (rc < 0) {
    FTL_LOG(ERROR) << "hid: could not get protocol from " << name_
                   << " (status=" << rc << ")";
  }
  return rc;
}

mx_status_t InputDevice::GetReportDescriptionLength(
    size_t* out_report_desc_len) {
  ssize_t rc = ioctl_input_get_report_desc_size(fd_, out_report_desc_len);
  if (rc < 0) {
    FTL_LOG(ERROR) << "hid: could not get report descriptor length from "
                   << name_ << "  (status=" << rc << ")";
  }
  return rc;
}

mx_status_t InputDevice::GetReportDescription(uint8_t* out_buf,
                                              size_t out_report_desc_len) {
  ssize_t rc = ioctl_input_get_report_desc(fd_, out_buf, out_report_desc_len);
  if (rc < 0) {
    FTL_LOG(ERROR) << "hid: could not get report descriptor from " << name_
                   << " (status=" << rc << ")";
  }
  return rc;
}

mx_status_t InputDevice::GetMaxReportLength(
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

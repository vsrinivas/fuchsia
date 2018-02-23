// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/input_interpreter.h"

#include <fcntl.h>
#include <hid/acer12.h>
#include <hid/egalax.h>
#include <hid/hid.h>
#include <hid/paradise.h>
#include <hid/samsung.h>
#include <hid/usages.h>

#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <zircon/device/device.h>
#include <zircon/device/input.h>
#include <zircon/types.h>

#include <trace/event.h>

#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_point.h"
#include "lib/ui/input/cpp/formatting.h"
#include <fuchsia/cpp/input.h>

namespace {

int64_t InputEventTimestampNow() {
  return fxl::TimePoint::Now().ToEpochDelta().ToNanoseconds();
}

input::InputReport CloneReport(const input::InputReportPtr& report) {
  input::InputReport result;
  fidl::Clone(*report, &result);
  return result;
}

}  // namespace

namespace mozart {

std::unique_ptr<InputInterpreter> InputInterpreter::Open(
    int dirfd,
    std::string filename,
    input::InputDeviceRegistry* registry) {
  int fd = openat(dirfd, filename.c_str(), O_RDONLY);
  if (fd < 0) {
    FXL_LOG(ERROR) << "Failed to open device " << filename;
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
                                   input::InputDeviceRegistry* registry)
    : registry_(registry), hid_decoder_(std::move(name), fd) {
  memset(acer12_touch_reports_, 0, 2 * sizeof(acer12_touch_t));
}

InputInterpreter::~InputInterpreter() {}

bool InputInterpreter::Initialize() {
  int protocol;
  if (!hid_decoder_.Init(&protocol))
    return false;

  if (protocol == INPUT_PROTO_KBD) {
    FXL_VLOG(2) << "Device " << name() << " has keyboard";
    has_keyboard_ = true;
    keyboard_descriptor_ = input::KeyboardDescriptor::New();
    keyboard_descriptor_->keys.resize(HID_USAGE_KEY_RIGHT_GUI -
                                      HID_USAGE_KEY_A + 1);
    for (size_t index = HID_USAGE_KEY_A; index <= HID_USAGE_KEY_RIGHT_GUI;
         ++index) {
      keyboard_descriptor_->keys->at(index - HID_USAGE_KEY_A) = index;
    }

    keyboard_report_ = input::InputReport::New();
    keyboard_report_->keyboard = input::KeyboardReport::New();
  } else if (protocol == INPUT_PROTO_MOUSE) {
    FXL_VLOG(2) << "Device " << name() << " has mouse";
    has_mouse_ = true;
    mouse_device_type_ = MouseDeviceType::BOOT;

    mouse_descriptor_ = input::MouseDescriptor::New();
    mouse_descriptor_->rel_x.range.min = INT32_MIN;
    mouse_descriptor_->rel_x.range.max = INT32_MAX;
    mouse_descriptor_->rel_x.resolution = 1;

    mouse_descriptor_->rel_y.range.min = INT32_MIN;
    mouse_descriptor_->rel_y.range.max = INT32_MAX;
    mouse_descriptor_->rel_y.resolution = 1;

    mouse_descriptor_->buttons |= input::kMouseButtonPrimary;
    mouse_descriptor_->buttons |= input::kMouseButtonSecondary;
    mouse_descriptor_->buttons |= input::kMouseButtonTertiary;

    mouse_report_ = input::InputReport::New();
    mouse_report_->mouse = input::MouseReport::New();
  } else if (protocol == INPUT_PROTO_NONE) {
    size_t report_desc_len;
    if (!hid_decoder_.GetReportDescriptionLength(&report_desc_len))
      return false;

    std::vector<uint8_t> desc(report_desc_len);
    if (!hid_decoder_.GetReportDescription(desc.data(), desc.size()))
      return false;

    if (is_acer12_touch_report_desc(desc.data(), desc.size())) {
      FXL_VLOG(2) << "Device " << name() << " has stylus";
      has_stylus_ = true;
      stylus_descriptor_ = input::StylusDescriptor::New();

      stylus_descriptor_->x.range.min = 0;
      stylus_descriptor_->x.range.max = ACER12_STYLUS_X_MAX;
      stylus_descriptor_->x.resolution = 1;

      stylus_descriptor_->y.range.min = 0;
      stylus_descriptor_->y.range.max = ACER12_STYLUS_Y_MAX;
      stylus_descriptor_->y.resolution = 1;

      stylus_descriptor_->is_invertible = false;

      stylus_descriptor_->buttons |= input::kStylusBarrel;

      stylus_report_ = input::InputReport::New();
      stylus_report_->stylus = input::StylusReport::New();

      FXL_VLOG(2) << "Device " << name() << " has touchscreen";
      has_touchscreen_ = true;
      touchscreen_descriptor_ = input::TouchscreenDescriptor::New();

      touchscreen_descriptor_->x.range.min = 0;
      touchscreen_descriptor_->x.range.max = ACER12_X_MAX;
      touchscreen_descriptor_->x.resolution = 1;

      touchscreen_descriptor_->y.range.min = 0;
      touchscreen_descriptor_->y.range.max = ACER12_Y_MAX;
      touchscreen_descriptor_->y.resolution = 1;

      // TODO(jpoichet) do not hardcode this
      touchscreen_descriptor_->max_finger_id = 255;

      touchscreen_report_ = input::InputReport::New();
      touchscreen_report_->touchscreen = input::TouchscreenReport::New();

      touch_device_type_ = TouchDeviceType::ACER12;
    } else if (is_samsung_touch_report_desc(desc.data(), desc.size())) {
      // TODO(cpu): remove this hack from this layer.
      hid_decoder_.apply_samsung_touch_hack();

      FXL_VLOG(2) << "Device " << name() << " has touchscreen";
      has_touchscreen_ = true;
      touchscreen_descriptor_ = input::TouchscreenDescriptor::New();

      touchscreen_descriptor_->x.range.min = 0;
      touchscreen_descriptor_->x.range.max = SAMSUNG_X_MAX;
      touchscreen_descriptor_->x.resolution = 1;

      touchscreen_descriptor_->y.range.min = 0;
      touchscreen_descriptor_->y.range.max = SAMSUNG_Y_MAX;
      touchscreen_descriptor_->y.resolution = 1;

      // TODO(jpoichet) do not hardcode this
      touchscreen_descriptor_->max_finger_id = 255;

      touchscreen_report_ = input::InputReport::New();
      touchscreen_report_->touchscreen = input::TouchscreenReport::New();

      touch_device_type_ = TouchDeviceType::SAMSUNG;
    } else if (is_paradise_touch_report_desc(desc.data(), desc.size())) {
      // TODO(cpu): Add support for stylus.

      FXL_VLOG(2) << "Device " << name() << " has touchscreen";
      has_touchscreen_ = true;
      touchscreen_descriptor_ = input::TouchscreenDescriptor::New();

      touchscreen_descriptor_->x.range.min = 0;
      touchscreen_descriptor_->x.range.max = PARADISE_X_MAX;
      touchscreen_descriptor_->x.resolution = 1;

      touchscreen_descriptor_->y.range.min = 0;
      touchscreen_descriptor_->y.range.max = PARADISE_Y_MAX;
      touchscreen_descriptor_->y.resolution = 1;

      // TODO(cpu) do not hardcode |max_finger_id|.
      touchscreen_descriptor_->max_finger_id = 255;

      touchscreen_report_ = input::InputReport::New();
      touchscreen_report_->touchscreen = input::TouchscreenReport::New();

      touch_device_type_ = TouchDeviceType::PARADISEv1;
    } else if (is_paradise_touch_v2_report_desc(desc.data(), desc.size())) {
      FXL_VLOG(2) << "Device " << name() << " has touchscreen";
      has_touchscreen_ = true;
      touchscreen_descriptor_ = input::TouchscreenDescriptor::New();

      touchscreen_descriptor_->x.range.min = 0;
      touchscreen_descriptor_->x.range.max = PARADISE_X_MAX;
      touchscreen_descriptor_->x.resolution = 1;

      touchscreen_descriptor_->y.range.min = 0;
      touchscreen_descriptor_->y.range.max = PARADISE_Y_MAX;
      touchscreen_descriptor_->y.resolution = 1;

      // TODO(cpu) do not hardcode |max_finger_id|.
      touchscreen_descriptor_->max_finger_id = 255;

      touchscreen_report_ = input::InputReport::New();
      touchscreen_report_->touchscreen = input::TouchscreenReport::New();

      touch_device_type_ = TouchDeviceType::PARADISEv2;
    } else if (is_paradise_touchpad_v1_report_desc(desc.data(), desc.size())) {
      FXL_VLOG(2) << "Device " << name() << " has touchpad";
      has_mouse_ = true;
      mouse_device_type_ = MouseDeviceType::PARADISEv1;

      mouse_descriptor_ = input::MouseDescriptor::New();

      mouse_descriptor_->rel_x.range.min = INT32_MIN;
      mouse_descriptor_->rel_x.range.max = INT32_MAX;
      mouse_descriptor_->rel_x.resolution = 1;

      mouse_descriptor_->rel_y.range.min = INT32_MIN;
      mouse_descriptor_->rel_y.range.max = INT32_MAX;
      mouse_descriptor_->rel_y.resolution = 1;

      mouse_descriptor_->buttons |= input::kMouseButtonPrimary;

      mouse_report_ = input::InputReport::New();
      mouse_report_->mouse = input::MouseReport::New();
    } else if (is_paradise_touchpad_v2_report_desc(desc.data(), desc.size())) {
      FXL_VLOG(2) << "Device " << name() << " has touchpad";
      has_mouse_ = true;
      mouse_device_type_ = MouseDeviceType::PARADISEv2;

      mouse_descriptor_ = input::MouseDescriptor::New();

      mouse_descriptor_->rel_x.range.min = INT32_MIN;
      mouse_descriptor_->rel_x.range.max = INT32_MAX;
      mouse_descriptor_->rel_x.resolution = 1;

      mouse_descriptor_->rel_y.range.min = INT32_MIN;
      mouse_descriptor_->rel_y.range.max = INT32_MAX;
      mouse_descriptor_->rel_y.resolution = 1;

      mouse_descriptor_->buttons |= input::kMouseButtonPrimary;

      mouse_report_ = input::InputReport::New();
      mouse_report_->mouse = input::MouseReport::New();
    } else if (is_egalax_touchscreen_report_desc(desc.data(), desc.size())) {
      FXL_VLOG(2) << "Device " << name() << " has touchscreen";
      has_touchscreen_ = true;
      touchscreen_descriptor_ = input::TouchscreenDescriptor::New();

      touchscreen_descriptor_->x.range.min = 0;
      touchscreen_descriptor_->x.range.max = EGALAX_X_MAX;
      touchscreen_descriptor_->x.resolution = 1;

      touchscreen_descriptor_->y.range.min = 0;
      touchscreen_descriptor_->y.range.max = EGALAX_Y_MAX;
      touchscreen_descriptor_->y.resolution = 1;

      touchscreen_descriptor_->max_finger_id = 1;

      touchscreen_report_ = input::InputReport::New();
      touchscreen_report_->touchscreen = input::TouchscreenReport::New();

      touch_device_type_ = TouchDeviceType::EGALAX;
    } else {
      FXL_VLOG(2) << "Device " << name() << " has unsupported HID device";
      return false;
    }
  } else {
    FXL_VLOG(2) << "Device " << name() << " has unsupported HID protocol";
    return false;
  }

  zx_handle_t handle;
  if (!hid_decoder_.GetEvent(&handle))
    return false;

  event_.reset(handle);
  NotifyRegistry();
  return true;
}

void InputInterpreter::NotifyRegistry() {
  input::DeviceDescriptor descriptor;
  if (has_keyboard_) {
    fidl::Clone(keyboard_descriptor_, &descriptor.keyboard);
  }
  if (has_mouse_) {
    fidl::Clone(mouse_descriptor_, &descriptor.mouse);
  }
  if (has_stylus_) {
    fidl::Clone(stylus_descriptor_, &descriptor.stylus);
  }
  if (has_touchscreen_) {
    fidl::Clone(touchscreen_descriptor_, &descriptor.touchscreen);
  }

  registry_->RegisterDevice(std::move(descriptor), input_device_.NewRequest());
}

bool InputInterpreter::Read(bool discard) {
  // If positive |rc| is the number of bytes read. If negative the error
  // while reading.
  int rc;
  auto report = hid_decoder_.Read(&rc);

  if (rc < 1) {
    FXL_LOG(ERROR) << "Failed to read from input: " << rc;
    // TODO(cpu) check whether the device was actually closed or not.
    return false;
  }

  TRACE_DURATION("input", "Read");
  if (has_keyboard_) {
    ParseKeyboardReport(report.data(), rc);
    if (!discard) {
      input_device_->DispatchReport(CloneReport(keyboard_report_));
    }
  }

  switch (mouse_device_type_) {
    case MouseDeviceType::BOOT:
      ParseMouseReport(report.data(), rc);
      if (!discard) {
        input_device_->DispatchReport(CloneReport(mouse_report_));
      }
      break;
    case MouseDeviceType::PARADISEv1:
      if (ParseParadiseTouchpadReport<paradise_touchpad_v1_t>(report.data(), rc)) {
        if (!discard) {
          input_device_->DispatchReport(CloneReport(mouse_report_));
        }
      }
      break;
    case MouseDeviceType::PARADISEv2:
      if (ParseParadiseTouchpadReport<paradise_touchpad_v2_t>(report.data(), rc)) {
        if (!discard) {
          input_device_->DispatchReport(CloneReport(mouse_report_));
        }
      }
      break;
    case MouseDeviceType::NONE:
      break;
  }

  switch (touch_device_type_) {
    case TouchDeviceType::ACER12:
      if (report[0] == ACER12_RPT_ID_STYLUS) {
        if (ParseAcer12StylusReport(report.data(), rc)) {
          if (!discard) {
            input_device_->DispatchReport(CloneReport(stylus_report_));
          }
        }
      } else if (report[0] == ACER12_RPT_ID_TOUCH) {
        if (ParseAcer12TouchscreenReport(report.data(), rc)) {
          if (!discard) {
            input_device_->DispatchReport(CloneReport(touchscreen_report_));
          }
        }
      }
      break;

    case TouchDeviceType::SAMSUNG:
      if (report[0] == SAMSUNG_RPT_ID_TOUCH) {
        if (ParseSamsungTouchscreenReport(report.data(), rc)) {
          if (!discard) {
            input_device_->DispatchReport(CloneReport(touchscreen_report_));
          }
        }
      }
      break;

    case TouchDeviceType::PARADISEv1:
      if (report[0] == PARADISE_RPT_ID_TOUCH) {
        if (ParseParadiseTouchscreenReport<paradise_touch_t>(report.data(), rc)) {
          if (!discard) {
            input_device_->DispatchReport(CloneReport(touchscreen_report_));
          }
        }
      }
      break;
    case TouchDeviceType::PARADISEv2:
      if (report[0] == PARADISE_RPT_ID_TOUCH) {
        if (ParseParadiseTouchscreenReport<paradise_touch_v2_t>(report.data(), rc)) {
          if (!discard) {
            input_device_->DispatchReport(CloneReport(touchscreen_report_));
          }
        }
      }
      break;
    case TouchDeviceType::EGALAX:
      if (report[0] == EGALAX_RPT_ID_TOUCH) {
        if (ParseEGalaxTouchscreenReport(report.data(), rc)) {
          if (!discard) {
            input_device_->DispatchReport(CloneReport(touchscreen_report_));
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
    keyboard_report_->keyboard->pressed_keys->at(index) = keycode;
    index++;
  }
  FXL_VLOG(2) << name() << " parsed: " << *keyboard_report_;
}

void InputInterpreter::ParseMouseReport(uint8_t* r, size_t len) {
  auto report = reinterpret_cast<boot_mouse_report_t*>(r);
  mouse_report_->event_time = InputEventTimestampNow();

  mouse_report_->mouse->rel_x = report->rel_x;
  mouse_report_->mouse->rel_y = report->rel_y;
  mouse_report_->mouse->pressed_buttons = report->buttons;
  FXL_VLOG(2) << name() << " parsed: " << *mouse_report_;
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
    stylus_report_->stylus->pressed_buttons |= input::kStylusBarrel;
  }
  FXL_VLOG(2) << name() << " parsed: " << *stylus_report_;

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
      input::Touch touch;
      touch.finger_id = acer12_finger_id_contact(fid);
      touch.x = acer12_touch_reports_[i].fingers[c].x;
      touch.y = acer12_touch_reports_[i].fingers[c].y;
      touch.width = acer12_touch_reports_[i].fingers[c].width;
      touch.height = acer12_touch_reports_[i].fingers[c].height;
      touchscreen_report_->touchscreen->touches.resize(index + 1);
      touchscreen_report_->touchscreen->touches->at(index++) = std::move(touch);
    }
  }
  FXL_VLOG(2) << name() << " parsed: " << *touchscreen_report_;
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

    input::Touch touch;
    touch.finger_id = samsung_finger_id_contact(fid);
    touch.x = report.fingers[i].x;
    touch.y = report.fingers[i].y;
    touch.width = report.fingers[i].width;
    touch.height = report.fingers[i].height;
    touchscreen_report_->touchscreen->touches.resize(index + 1);
    touchscreen_report_->touchscreen->touches->at(index++) = std::move(touch);
  }

  return true;
}

template <typename ReportT>
bool InputInterpreter::ParseParadiseTouchscreenReport(uint8_t* r, size_t len) {
  if (len != sizeof(ReportT)) {
    FXL_LOG(INFO) << "paradise wrong size " << len;
    return false;
  }

  const auto& report = *(reinterpret_cast<ReportT*>(r));
  touchscreen_report_->event_time = InputEventTimestampNow();

  size_t index = 0;
  touchscreen_report_->touchscreen->touches.resize(index);

  for (size_t i = 0; i < countof(report.fingers); ++i) {
    if (!paradise_finger_flags_tswitch(report.fingers[i].flags))
      continue;

    input::Touch touch;
    touch.finger_id = report.fingers[i].finger_id;
    touch.x = report.fingers[i].x;
    touch.y = report.fingers[i].y;
    touch.width = 5;  // TODO(cpu): Don't hardcode |width| or |height|.
    touch.height = 5;
    touchscreen_report_->touchscreen->touches.resize(index + 1);
    touchscreen_report_->touchscreen->touches->at(index++) = std::move(touch);
  }

  FXL_VLOG(2) << name() << " parsed: " << *touchscreen_report_;
  return true;
}

bool InputInterpreter::ParseEGalaxTouchscreenReport(uint8_t* r, size_t len) {
  if (len != sizeof(egalax_touch_t)) {
    FXL_LOG(INFO) << "egalax wrong size " << len << " expected "
                  << sizeof(egalax_touch_t);
    return false;
  }

  const auto& report = *(reinterpret_cast<egalax_touch_t*>(r));
  touchscreen_report_->event_time = InputEventTimestampNow();
  if (egalax_pressed_flags(report.button_pad)) {
    input::Touch touch;
    touch.finger_id = 0;
    touch.x = report.x;
    touch.y = report.y;
    touch.width = 5;
    touch.height = 5;
    touchscreen_report_->touchscreen->touches.resize(1);
    touchscreen_report_->touchscreen->touches->at(0) = std::move(touch);
  } else {
    // if the button isn't pressed, send an empty report, this will terminate
    // the finger session
    touchscreen_report_->touchscreen->touches.resize(0);
  }

  FXL_VLOG(2) << name() << " parsed: " << *touchscreen_report_;
  return true;
}

template <typename ReportT>
bool InputInterpreter::ParseParadiseTouchpadReport(uint8_t* r, size_t len) {
  if (len != sizeof(ReportT)) {
    FXL_LOG(INFO) << "paradise wrong size " << len;
    return false;
  }

  mouse_report_->event_time = InputEventTimestampNow();

  const auto& report = *(reinterpret_cast<ReportT*>(r));
  if (!report.fingers[0].tip_switch) {
    mouse_report_->mouse->rel_x = 0;
    mouse_report_->mouse->rel_y = 0;
    mouse_report_->mouse->pressed_buttons = 0;

    mouse_abs_x_ = -1;
    return true;
  }

  // Each axis has a resolution of .00078125cm. 5/32 is a relatively arbitrary
  // coefficient that gives decent sensitivity and a nice resolution of .005cm.
  mouse_report_->mouse->rel_x =
      mouse_abs_x_ != -1 ? 5 * (report.fingers[0].x - mouse_abs_x_) / 32 : 0;
  mouse_report_->mouse->rel_y =
      mouse_abs_x_ != -1 ? 5 * (report.fingers[0].y - mouse_abs_y_) / 32 : 0;
  mouse_report_->mouse->pressed_buttons =
      report.button ? input::kMouseButtonPrimary : 0;

  // Don't update the abs position if there was no relative change, so that
  // we don't drop fractional relative deltas.
  if (mouse_report_->mouse->rel_y || mouse_abs_x_ == -1) {
    mouse_abs_y_ = report.fingers[0].y;
  }
  if (mouse_report_->mouse->rel_x || mouse_abs_x_ == -1) {
    mouse_abs_x_ = report.fingers[0].x;
  }

  return true;
}

}  // namespace mozart

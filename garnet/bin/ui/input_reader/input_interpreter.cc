// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/input_interpreter.h"
#include "garnet/bin/ui/input_reader/device.h"

#include <fuchsia/hardware/input/c/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <hid-parser/parser.h>
#include <hid-parser/usages.h>
#include <hid/acer12.h>
#include <hid/ambient-light.h>
#include <hid/boot.h>
#include <hid/egalax.h>
#include <hid/eyoyo.h>
#include <hid/ft3x27.h>
#include <hid/hid.h>
#include <hid/paradise.h>
#include <hid/samsung.h>
#include <hid/usages.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fxl/arraysize.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/time/time_point.h>
#include <lib/ui/input/cpp/formatting.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <trace/event.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "garnet/bin/ui/input_reader/fdio_hid_decoder.h"
#include "garnet/bin/ui/input_reader/protocols.h"

namespace {

// Variable to quickly re-enable the hardcoded touchpad reports.
// TODO(ZX-3219): Remove this once touchpads are stable
bool USE_TOUCHPAD_HARDCODED_REPORTS = false;

int64_t InputEventTimestampNow() {
  return fxl::TimePoint::Now().ToEpochDelta().ToNanoseconds();
}

fuchsia::ui::input::InputReport CloneReport(
    const fuchsia::ui::input::InputReport& report) {
  fuchsia::ui::input::InputReport result;
  fidl::Clone(report, &result);
  return result;
}

// TODO(SCN-473): Extract sensor IDs from HID.
const size_t kParadiseAccLid = 0;
const size_t kParadiseAccBase = 1;
const size_t kAmbientLight = 2;
}  // namespace

namespace mozart {

InputInterpreter::InputInterpreter(
    std::unique_ptr<HidDecoder> hid_decoder,
    fuchsia::ui::input::InputDeviceRegistry* registry)
    : registry_(registry), hid_decoder_(std::move(hid_decoder)) {
  FXL_DCHECK(hid_decoder_);
}

InputInterpreter::~InputInterpreter() {}

bool InputInterpreter::Initialize() {
  if (!hid_decoder_->Init())
    return false;

  if (!ParseProtocol())
    return false;

  auto protocol = protocol_;

  if (protocol == Protocol::Keyboard) {
    FXL_VLOG(2) << "Device " << name() << " has keyboard";
    has_keyboard_ = true;
    keyboard_descriptor_ = fuchsia::ui::input::KeyboardDescriptor::New();
    keyboard_descriptor_->keys.resize(HID_USAGE_KEY_RIGHT_GUI -
                                      HID_USAGE_KEY_A + 1);
    for (size_t index = HID_USAGE_KEY_A; index <= HID_USAGE_KEY_RIGHT_GUI;
         ++index) {
      keyboard_descriptor_->keys.at(index - HID_USAGE_KEY_A) = index;
    }

    keyboard_report_ = fuchsia::ui::input::InputReport::New();
    keyboard_report_->keyboard = fuchsia::ui::input::KeyboardReport::New();
  } else if (protocol == Protocol::BootMouse || protocol == Protocol::Gamepad) {
    FXL_VLOG(2) << "Device " << name() << " has mouse";
    has_mouse_ = true;
    mouse_device_type_ = (protocol == Protocol::BootMouse)
                             ? MouseDeviceType::BOOT
                             : MouseDeviceType::GAMEPAD;

    mouse_descriptor_ = fuchsia::ui::input::MouseDescriptor::New();
    mouse_descriptor_->rel_x.range.min = INT32_MIN;
    mouse_descriptor_->rel_x.range.max = INT32_MAX;
    mouse_descriptor_->rel_x.resolution = 1;

    mouse_descriptor_->rel_y.range.min = INT32_MIN;
    mouse_descriptor_->rel_y.range.max = INT32_MAX;
    mouse_descriptor_->rel_y.resolution = 1;

    mouse_descriptor_->buttons |= fuchsia::ui::input::kMouseButtonPrimary;
    mouse_descriptor_->buttons |= fuchsia::ui::input::kMouseButtonSecondary;
    mouse_descriptor_->buttons |= fuchsia::ui::input::kMouseButtonTertiary;

    mouse_report_ = fuchsia::ui::input::InputReport::New();
    mouse_report_->mouse = fuchsia::ui::input::MouseReport::New();
  } else if (protocol == Protocol::Acer12Touch) {
    FXL_VLOG(2) << "Device " << name() << " has stylus";
    has_stylus_ = true;
    stylus_descriptor_ = fuchsia::ui::input::StylusDescriptor::New();

    stylus_descriptor_->x.range.min = 0;
    stylus_descriptor_->x.range.max = ACER12_STYLUS_X_MAX;
    stylus_descriptor_->x.resolution = 1;

    stylus_descriptor_->y.range.min = 0;
    stylus_descriptor_->y.range.max = ACER12_STYLUS_Y_MAX;
    stylus_descriptor_->y.resolution = 1;

    stylus_descriptor_->is_invertible = false;

    stylus_descriptor_->buttons |= fuchsia::ui::input::kStylusBarrel;

    stylus_report_ = fuchsia::ui::input::InputReport::New();
    stylus_report_->stylus = fuchsia::ui::input::StylusReport::New();

    FXL_VLOG(2) << "Device " << name() << " has touchscreen";
    has_touchscreen_ = true;
    touchscreen_descriptor_ = fuchsia::ui::input::TouchscreenDescriptor::New();

    touchscreen_descriptor_->x.range.min = 0;
    touchscreen_descriptor_->x.range.max = ACER12_X_MAX;
    touchscreen_descriptor_->x.resolution = 1;

    touchscreen_descriptor_->y.range.min = 0;
    touchscreen_descriptor_->y.range.max = ACER12_Y_MAX;
    touchscreen_descriptor_->y.resolution = 1;

    // TODO(jpoichet) do not hardcode this
    touchscreen_descriptor_->max_finger_id = 255;

    touchscreen_report_ = fuchsia::ui::input::InputReport::New();
    touchscreen_report_->touchscreen =
        fuchsia::ui::input::TouchscreenReport::New();

    touch_device_type_ = TouchDeviceType::ACER12;
  } else if (protocol == Protocol::SamsungTouch) {
    FXL_VLOG(2) << "Device " << name() << " has touchscreen";
    has_touchscreen_ = true;
    touchscreen_descriptor_ = fuchsia::ui::input::TouchscreenDescriptor::New();

    touchscreen_descriptor_->x.range.min = 0;
    touchscreen_descriptor_->x.range.max = SAMSUNG_X_MAX;
    touchscreen_descriptor_->x.resolution = 1;

    touchscreen_descriptor_->y.range.min = 0;
    touchscreen_descriptor_->y.range.max = SAMSUNG_Y_MAX;
    touchscreen_descriptor_->y.resolution = 1;

    // TODO(jpoichet) do not hardcode this
    touchscreen_descriptor_->max_finger_id = 255;

    touchscreen_report_ = fuchsia::ui::input::InputReport::New();
    touchscreen_report_->touchscreen =
        fuchsia::ui::input::TouchscreenReport::New();

    touch_device_type_ = TouchDeviceType::SAMSUNG;
  } else if (protocol == Protocol::ParadiseV1Touch) {
    // TODO(cpu): Add support for stylus.
    FXL_VLOG(2) << "Device " << name() << " has touchscreen";
    has_touchscreen_ = true;
    touchscreen_descriptor_ = fuchsia::ui::input::TouchscreenDescriptor::New();

    touchscreen_descriptor_->x.range.min = 0;
    touchscreen_descriptor_->x.range.max = PARADISE_X_MAX;
    touchscreen_descriptor_->x.resolution = 1;

    touchscreen_descriptor_->y.range.min = 0;
    touchscreen_descriptor_->y.range.max = PARADISE_Y_MAX;
    touchscreen_descriptor_->y.resolution = 1;

    // TODO(cpu) do not hardcode |max_finger_id|.
    touchscreen_descriptor_->max_finger_id = 255;

    touchscreen_report_ = fuchsia::ui::input::InputReport::New();
    touchscreen_report_->touchscreen =
        fuchsia::ui::input::TouchscreenReport::New();

    touch_device_type_ = TouchDeviceType::PARADISEv1;
  } else if (protocol == Protocol::ParadiseV2Touch) {
    FXL_VLOG(2) << "Device " << name() << " has stylus";
    has_stylus_ = true;
    stylus_descriptor_ = fuchsia::ui::input::StylusDescriptor::New();

    stylus_descriptor_->x.range.min = 0;
    stylus_descriptor_->x.range.max = PARADISE_STYLUS_X_MAX;
    stylus_descriptor_->x.resolution = 1;

    stylus_descriptor_->y.range.min = 0;
    stylus_descriptor_->y.range.max = PARADISE_STYLUS_Y_MAX;
    stylus_descriptor_->y.resolution = 1;

    stylus_descriptor_->is_invertible = false;

    stylus_descriptor_->buttons |= fuchsia::ui::input::kStylusBarrel;

    stylus_report_ = fuchsia::ui::input::InputReport::New();
    stylus_report_->stylus = fuchsia::ui::input::StylusReport::New();

    FXL_VLOG(2) << "Device " << name() << " has touchscreen";
    has_touchscreen_ = true;
    touchscreen_descriptor_ = fuchsia::ui::input::TouchscreenDescriptor::New();

    touchscreen_descriptor_->x.range.min = 0;
    touchscreen_descriptor_->x.range.max = PARADISE_X_MAX;
    touchscreen_descriptor_->x.resolution = 1;

    touchscreen_descriptor_->y.range.min = 0;
    touchscreen_descriptor_->y.range.max = PARADISE_Y_MAX;
    touchscreen_descriptor_->y.resolution = 1;

    // TODO(cpu) do not hardcode |max_finger_id|.
    touchscreen_descriptor_->max_finger_id = 255;

    touchscreen_report_ = fuchsia::ui::input::InputReport::New();
    touchscreen_report_->touchscreen =
        fuchsia::ui::input::TouchscreenReport::New();

    touch_device_type_ = TouchDeviceType::PARADISEv2;
  } else if (protocol == Protocol::ParadiseV3Touch) {
    FXL_VLOG(2) << "Device " << name() << " has stylus";
    has_stylus_ = true;
    stylus_descriptor_ = fuchsia::ui::input::StylusDescriptor::New();

    stylus_descriptor_->x.range.min = 0;
    stylus_descriptor_->x.range.max = PARADISE_STYLUS_X_MAX;
    stylus_descriptor_->x.resolution = 1;

    stylus_descriptor_->y.range.min = 0;
    stylus_descriptor_->y.range.max = PARADISE_STYLUS_Y_MAX;
    stylus_descriptor_->y.resolution = 1;

    stylus_descriptor_->is_invertible = false;

    stylus_descriptor_->buttons |= fuchsia::ui::input::kStylusBarrel;

    stylus_report_ = fuchsia::ui::input::InputReport::New();
    stylus_report_->stylus = fuchsia::ui::input::StylusReport::New();

    FXL_VLOG(2) << "Device " << name() << " has touchscreen";
    has_touchscreen_ = true;
    touchscreen_descriptor_ = fuchsia::ui::input::TouchscreenDescriptor::New();

    touchscreen_descriptor_->x.range.min = 0;
    touchscreen_descriptor_->x.range.max = PARADISE_X_MAX;
    touchscreen_descriptor_->x.resolution = 1;

    touchscreen_descriptor_->y.range.min = 0;
    touchscreen_descriptor_->y.range.max = PARADISE_Y_MAX;
    touchscreen_descriptor_->y.resolution = 1;

    // TODO(cpu) do not hardcode |max_finger_id|.
    touchscreen_descriptor_->max_finger_id = 255;

    touchscreen_report_ = fuchsia::ui::input::InputReport::New();
    touchscreen_report_->touchscreen =
        fuchsia::ui::input::TouchscreenReport::New();

    touch_device_type_ = TouchDeviceType::PARADISEv3;
  } else if (protocol == Protocol::ParadiseV1TouchPad) {
    FXL_VLOG(2) << "Device " << name() << " has touchpad";
    has_mouse_ = true;
    mouse_device_type_ = MouseDeviceType::PARADISEv1;

    mouse_descriptor_ = fuchsia::ui::input::MouseDescriptor::New();

    mouse_descriptor_->rel_x.range.min = INT32_MIN;
    mouse_descriptor_->rel_x.range.max = INT32_MAX;
    mouse_descriptor_->rel_x.resolution = 1;

    mouse_descriptor_->rel_y.range.min = INT32_MIN;
    mouse_descriptor_->rel_y.range.max = INT32_MAX;
    mouse_descriptor_->rel_y.resolution = 1;

    mouse_descriptor_->buttons |= fuchsia::ui::input::kMouseButtonPrimary;

    mouse_report_ = fuchsia::ui::input::InputReport::New();
    mouse_report_->mouse = fuchsia::ui::input::MouseReport::New();
  } else if (protocol == Protocol::ParadiseV2TouchPad) {
    FXL_VLOG(2) << "Device " << name() << " has touchpad";
    has_mouse_ = true;
    mouse_device_type_ = MouseDeviceType::PARADISEv2;

    mouse_descriptor_ = fuchsia::ui::input::MouseDescriptor::New();

    mouse_descriptor_->rel_x.range.min = INT32_MIN;
    mouse_descriptor_->rel_x.range.max = INT32_MAX;
    mouse_descriptor_->rel_x.resolution = 1;

    mouse_descriptor_->rel_y.range.min = INT32_MIN;
    mouse_descriptor_->rel_y.range.max = INT32_MAX;
    mouse_descriptor_->rel_y.resolution = 1;

    mouse_descriptor_->buttons |= fuchsia::ui::input::kMouseButtonPrimary;

    mouse_report_ = fuchsia::ui::input::InputReport::New();
    mouse_report_->mouse = fuchsia::ui::input::MouseReport::New();
  } else if (protocol == Protocol::EgalaxTouch) {
    FXL_VLOG(2) << "Device " << name() << " has touchscreen";
    has_touchscreen_ = true;
    touchscreen_descriptor_ = fuchsia::ui::input::TouchscreenDescriptor::New();

    touchscreen_descriptor_->x.range.min = 0;
    touchscreen_descriptor_->x.range.max = EGALAX_X_MAX;
    touchscreen_descriptor_->x.resolution = 1;

    touchscreen_descriptor_->y.range.min = 0;
    touchscreen_descriptor_->y.range.max = EGALAX_Y_MAX;
    touchscreen_descriptor_->y.resolution = 1;

    touchscreen_descriptor_->max_finger_id = 1;

    touchscreen_report_ = fuchsia::ui::input::InputReport::New();
    touchscreen_report_->touchscreen =
        fuchsia::ui::input::TouchscreenReport::New();

    touch_device_type_ = TouchDeviceType::EGALAX;
  } else if (protocol == Protocol::ParadiseSensor) {
    FXL_VLOG(2) << "Device " << name() << " has motion sensors";
    sensor_device_type_ = SensorDeviceType::PARADISE;
    has_sensors_ = true;

    fuchsia::ui::input::SensorDescriptorPtr acc_base =
        fuchsia::ui::input::SensorDescriptor::New();
    acc_base->type = fuchsia::ui::input::SensorType::ACCELEROMETER;
    acc_base->loc = fuchsia::ui::input::SensorLocation::BASE;
    sensor_descriptors_[kParadiseAccBase] = std::move(acc_base);

    fuchsia::ui::input::SensorDescriptorPtr acc_lid =
        fuchsia::ui::input::SensorDescriptor::New();
    acc_lid->type = fuchsia::ui::input::SensorType::ACCELEROMETER;
    acc_lid->loc = fuchsia::ui::input::SensorLocation::LID;
    sensor_descriptors_[kParadiseAccLid] = std::move(acc_lid);

    sensor_report_ = fuchsia::ui::input::InputReport::New();
    sensor_report_->sensor = fuchsia::ui::input::SensorReport::New();
  } else if (protocol == Protocol::EyoyoTouch) {
    FXL_VLOG(2) << "Device " << name() << " has touchscreen";
    has_touchscreen_ = true;
    touchscreen_descriptor_ = fuchsia::ui::input::TouchscreenDescriptor::New();

    touchscreen_descriptor_->x.range.min = 0;
    touchscreen_descriptor_->x.range.max = EYOYO_X_MAX;
    touchscreen_descriptor_->x.resolution = 1;

    touchscreen_descriptor_->y.range.min = 0;
    touchscreen_descriptor_->y.range.max = EYOYO_Y_MAX;
    touchscreen_descriptor_->y.resolution = 1;

    // TODO(jpoichet) do not hardcode this
    touchscreen_descriptor_->max_finger_id = 255;

    touchscreen_report_ = fuchsia::ui::input::InputReport::New();
    touchscreen_report_->touchscreen =
        fuchsia::ui::input::TouchscreenReport::New();

    touch_device_type_ = TouchDeviceType::EYOYO;
  } else if (protocol == Protocol::LightSensor) {
    FXL_VLOG(2) << "Device " << name() << " has an ambient light sensor";
    sensor_device_type_ = SensorDeviceType::AMBIENT_LIGHT;
    has_sensors_ = true;

    fuchsia::ui::input::SensorDescriptorPtr desc =
        fuchsia::ui::input::SensorDescriptor::New();
    desc->type = fuchsia::ui::input::SensorType::LIGHTMETER;
    desc->loc = fuchsia::ui::input::SensorLocation::UNKNOWN;
    sensor_descriptors_[kAmbientLight] = std::move(desc);

    sensor_report_ = fuchsia::ui::input::InputReport::New();
    sensor_report_->sensor = fuchsia::ui::input::SensorReport::New();
  } else if (protocol == Protocol::EyoyoTouch) {
    FXL_VLOG(2) << "Device " << name() << " has touchscreen";
    has_touchscreen_ = true;
    touchscreen_descriptor_ = fuchsia::ui::input::TouchscreenDescriptor::New();

    touchscreen_descriptor_->x.range.min = 0;
    touchscreen_descriptor_->x.range.max = EYOYO_X_MAX;
    touchscreen_descriptor_->x.resolution = 1;

    touchscreen_descriptor_->y.range.min = 0;
    touchscreen_descriptor_->y.range.max = EYOYO_Y_MAX;
    touchscreen_descriptor_->y.resolution = 1;

    // TODO(jpoichet) do not hardcode this
    touchscreen_descriptor_->max_finger_id = 255;

    touchscreen_report_ = fuchsia::ui::input::InputReport::New();
    touchscreen_report_->touchscreen =
        fuchsia::ui::input::TouchscreenReport::New();

    touch_device_type_ = TouchDeviceType::EYOYO;
  } else if (protocol == Protocol::Ft3x27Touch) {
    FXL_VLOG(2) << "Device " << name() << " has a touchscreen";
    has_touchscreen_ = true;
    touchscreen_descriptor_ = fuchsia::ui::input::TouchscreenDescriptor::New();
    touchscreen_descriptor_->x.range.min = 0;
    touchscreen_descriptor_->x.range.max = FT3X27_X_MAX;
    touchscreen_descriptor_->x.resolution = 1;
    touchscreen_descriptor_->y.range.min = 0;
    touchscreen_descriptor_->y.range.max = FT3X27_Y_MAX;
    touchscreen_descriptor_->y.resolution = 1;

    // TODO(SCN-867) Use HID parsing for all touch devices
    // will remove the need for this hardcoding
    touchscreen_descriptor_->max_finger_id = 255;

    touchscreen_report_ = fuchsia::ui::input::InputReport::New();
    touchscreen_report_->touchscreen =
        fuchsia::ui::input::TouchscreenReport::New();

    touch_device_type_ = TouchDeviceType::FT3X27;
  }

  event_ = hid_decoder_->GetEvent();
  if (!event_)
    return false;

  NotifyRegistry();
  return true;
}

void InputInterpreter::NotifyRegistry() {
  if (has_sensors_) {
    FXL_DCHECK(kMaxSensorCount == sensor_descriptors_.size());
    FXL_DCHECK(kMaxSensorCount == sensor_devices_.size());
    for (size_t i = 0; i < kMaxSensorCount; ++i) {
      if (sensor_descriptors_[i]) {
        fuchsia::ui::input::DeviceDescriptor descriptor;
        zx_status_t status =
            fidl::Clone(sensor_descriptors_[i], &descriptor.sensor);
        FXL_DCHECK(status == ZX_OK)
            << "Sensor descriptor: clone failed (status=" << status << ")";
        registry_->RegisterDevice(std::move(descriptor),
                                  sensor_devices_[i].NewRequest());
      }
    }
    // Sensor devices can't be anything else, so don't bother with other types.
    return;
  }

  // Register the hardcoded device's descriptors.
  {
    fuchsia::ui::input::DeviceDescriptor descriptor;
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
    registry_->RegisterDevice(std::move(descriptor),
                              input_device_.NewRequest());
  }

  // Register the generic device's descriptors.
  for (size_t i = 0; i < devices_.size(); i++) {
    fuchsia::ui::input::DeviceDescriptor descriptor;
    InputDevice& device = devices_[i];
    if (device.descriptor.has_keyboard) {
      fidl::Clone(device.descriptor.keyboard_descriptor, &descriptor.keyboard);
    }
    if (device.descriptor.has_mouse) {
      fidl::Clone(device.descriptor.mouse_descriptor, &descriptor.mouse);
    }
    if (device.descriptor.has_stylus) {
      fidl::Clone(device.descriptor.stylus_descriptor, &descriptor.stylus);
    }
    if (device.descriptor.has_touchscreen) {
      fidl::Clone(device.descriptor.touchscreen_descriptor,
                  &descriptor.touchscreen);
    }
    if (device.descriptor.has_sensor) {
      fidl::Clone(device.descriptor.sensor_descriptor, &descriptor.sensor);
    }
    registry_->RegisterDevice(std::move(descriptor),
                              device.input_device.NewRequest());
  }
}

bool InputInterpreter::Read(bool discard) {
  TRACE_DURATION("input", "hid_read");

  // If positive |rc| is the number of bytes read. If negative the error
  // while reading.
  int rc = 1;
  auto report = hid_decoder_->Read(&rc);

  if (rc < 1) {
    FXL_LOG(ERROR) << "Failed to read from input: " << rc << " for " << name();
    // TODO(cpu) check whether the device was actually closed or not.
    return false;
  }

  if (has_keyboard_) {
    hardcoded_.ParseKeyboardReport(report.data(), rc, keyboard_report_.get());
    if (!discard) {
      TRACE_FLOW_BEGIN("input", "hid_read_to_listener",
                       keyboard_report_->trace_id);
      input_device_->DispatchReport(CloneReport(*keyboard_report_));
    }
  }

  switch (mouse_device_type_) {
    case MouseDeviceType::BOOT:
      hardcoded_.ParseMouseReport(report.data(), rc, mouse_report_.get());
      if (!discard) {
        TRACE_FLOW_BEGIN("input", "hid_read_to_listener",
                         mouse_report_->trace_id);
        input_device_->DispatchReport(CloneReport(*mouse_report_));
      }
      break;
    case MouseDeviceType::PARADISEv1:
      if (hardcoded_.ParseParadiseTouchpadReportV1(report.data(), rc,
                                                   mouse_report_.get())) {
        if (!discard) {
          TRACE_FLOW_BEGIN("input", "hid_read_to_listener",
                           mouse_report_->trace_id);
          input_device_->DispatchReport(CloneReport(*mouse_report_));
        }
      }
      break;
    case MouseDeviceType::PARADISEv2:
      if (hardcoded_.ParseParadiseTouchpadReportV2(report.data(), rc,
                                                   mouse_report_.get())) {
        if (!discard) {
          TRACE_FLOW_BEGIN("input", "hid_read_to_listener",
                           mouse_report_->trace_id);
          input_device_->DispatchReport(CloneReport(*mouse_report_));
        }
      }
      break;
    case MouseDeviceType::GAMEPAD:
      // TODO(cpu): remove this once we have a good way to test gamepad.
      if (hardcoded_.ParseGamepadMouseReport(report.data(), rc,
                                             mouse_report_.get())) {
        if (!discard) {
          TRACE_FLOW_BEGIN("input", "hid_read_to_listener",
                           mouse_report_->trace_id);
          input_device_->DispatchReport(CloneReport(*mouse_report_));
        }
      }
      break;
    case MouseDeviceType::NONE:
      break;
    default:
      break;
  }

  switch (touch_device_type_) {
    case TouchDeviceType::ACER12:
      if (report[0] == ACER12_RPT_ID_STYLUS) {
        if (hardcoded_.ParseAcer12StylusReport(report.data(), rc,
                                               stylus_report_.get())) {
          if (!discard) {
            TRACE_FLOW_BEGIN("input", "hid_read_to_listener",
                             stylus_report_->trace_id);
            input_device_->DispatchReport(CloneReport(*stylus_report_));
          }
        }
      } else if (report[0] == ACER12_RPT_ID_TOUCH) {
        if (hardcoded_.ParseAcer12TouchscreenReport(
                report.data(), rc, touchscreen_report_.get())) {
          if (!discard) {
            TRACE_FLOW_BEGIN("input", "hid_read_to_listener",
                             touchscreen_report_->trace_id);
            input_device_->DispatchReport(CloneReport(*touchscreen_report_));
          }
        }
      }
      break;
    case TouchDeviceType::SAMSUNG:
      if (report[0] == SAMSUNG_RPT_ID_TOUCH) {
        if (hardcoded_.ParseSamsungTouchscreenReport(
                report.data(), rc, touchscreen_report_.get())) {
          if (!discard) {
            TRACE_FLOW_BEGIN("input", "hid_read_to_listener",
                             touchscreen_report_->trace_id);
            input_device_->DispatchReport(CloneReport(*touchscreen_report_));
          }
        }
      }
      break;

    case TouchDeviceType::PARADISEv1:
      if (report[0] == PARADISE_RPT_ID_TOUCH) {
        if (hardcoded_.ParseParadiseTouchscreenReportV1(
                report.data(), rc, touchscreen_report_.get())) {
          if (!discard) {
            TRACE_FLOW_BEGIN("input", "hid_read_to_listener",
                             touchscreen_report_->trace_id);
            input_device_->DispatchReport(CloneReport(*touchscreen_report_));
          }
        }
      }
      break;
    case TouchDeviceType::PARADISEv2:
      if (report[0] == PARADISE_RPT_ID_TOUCH) {
        if (hardcoded_.ParseParadiseTouchscreenReportV2(
                report.data(), rc, touchscreen_report_.get())) {
          if (!discard) {
            TRACE_FLOW_BEGIN("input", "hid_read_to_listener",
                             touchscreen_report_->trace_id);
            input_device_->DispatchReport(CloneReport(*touchscreen_report_));
          }
        }
      } else if (report[0] == PARADISE_RPT_ID_STYLUS) {
        if (hardcoded_.ParseParadiseStylusReport(report.data(), rc,
                                                 stylus_report_.get())) {
          if (!discard) {
            TRACE_FLOW_BEGIN("input", "hid_read_to_listener",
                             stylus_report_->trace_id);
            input_device_->DispatchReport(CloneReport(*stylus_report_));
          }
        }
      }
      break;
    case TouchDeviceType::PARADISEv3:
      if (report[0] == PARADISE_RPT_ID_TOUCH) {
        // Paradise V3 uses the same touchscreen report as v1.
        if (hardcoded_.ParseParadiseTouchscreenReportV1(
                report.data(), rc, touchscreen_report_.get())) {
          if (!discard) {
            TRACE_FLOW_BEGIN("input", "hid_read_to_listener",
                             touchscreen_report_->trace_id);
            input_device_->DispatchReport(CloneReport(*touchscreen_report_));
          }
        }
      } else if (report[0] == PARADISE_RPT_ID_STYLUS) {
        if (hardcoded_.ParseParadiseStylusReport(report.data(), rc,
                                                 stylus_report_.get())) {
          if (!discard) {
            TRACE_FLOW_BEGIN("input", "hid_read_to_listener",
                             stylus_report_->trace_id);
            input_device_->DispatchReport(CloneReport(*stylus_report_));
          }
        }
      }
      break;
    case TouchDeviceType::EGALAX:
      if (report[0] == EGALAX_RPT_ID_TOUCH) {
        if (hardcoded_.ParseEGalaxTouchscreenReport(
                report.data(), rc, touchscreen_report_.get())) {
          if (!discard) {
            TRACE_FLOW_BEGIN("input", "hid_read_to_listener",
                             touchscreen_report_->trace_id);
            input_device_->DispatchReport(CloneReport(*touchscreen_report_));
          }
        }
      }
      break;

    case TouchDeviceType::EYOYO:
      if (report[0] == EYOYO_RPT_ID_TOUCH) {
        if (hardcoded_.ParseEyoyoTouchscreenReport(report.data(), rc,
                                                   touchscreen_report_.get())) {
          if (!discard) {
            TRACE_FLOW_BEGIN("input", "hid_read_to_listener",
                             touchscreen_report_->trace_id);
            input_device_->DispatchReport(CloneReport(*touchscreen_report_));
          }
        }
      }
      break;
    case TouchDeviceType::FT3X27:
      if (report[0] == FT3X27_RPT_ID_TOUCH) {
        if (hardcoded_.ParseFt3x27TouchscreenReport(
                report.data(), rc, touchscreen_report_.get())) {
          if (!discard) {
            TRACE_FLOW_BEGIN("input", "hid_read_to_listener",
                             touchscreen_report_->trace_id);
            input_device_->DispatchReport(CloneReport(*touchscreen_report_));
          }
        }
      }
      break;

    default:
      break;
  }

  switch (sensor_device_type_) {
    case SensorDeviceType::PARADISE:
      if (hardcoded_.ParseParadiseSensorReport(report.data(), rc, &sensor_idx_,
                                               sensor_report_.get())) {
        if (!discard) {
          FXL_DCHECK(sensor_idx_ < kMaxSensorCount);
          FXL_DCHECK(sensor_devices_[sensor_idx_]);
          TRACE_FLOW_BEGIN("input", "hid_read_to_listener",
                           sensor_report_->trace_id);
          sensor_devices_[sensor_idx_]->DispatchReport(
              CloneReport(*sensor_report_));
        }
      }
      break;
    case SensorDeviceType::AMBIENT_LIGHT:
      if (hardcoded_.ParseAmbientLightSensorReport(
              report.data(), rc, &sensor_idx_, sensor_report_.get())) {
        if (!discard) {
          FXL_DCHECK(sensor_idx_ < kMaxSensorCount);
          FXL_DCHECK(sensor_devices_[sensor_idx_]);
          TRACE_FLOW_BEGIN("input", "hid_read_to_listener",
                           sensor_report_->trace_id);
          sensor_devices_[sensor_idx_]->DispatchReport(
              CloneReport(*sensor_report_));
        }
      }
      break;
    default:
      break;
  }
  for (size_t i = 0; i < devices_.size(); i++) {
    InputDevice& device = devices_[i];
    if (device.device->ParseReport(report.data(), rc, device.report.get())) {
      if (!discard) {
        device.report->event_time = InputEventTimestampNow();
        device.report->trace_id = TRACE_NONCE();
        TRACE_FLOW_BEGIN("input", "hid_read_to_listener",
                         device.report->trace_id);
        device.input_device->DispatchReport(CloneReport(*device.report));
      }
    }
  }

  return true;
}

Protocol ExtractProtocol(hid::Usage input) {
  using ::hid::usage::Consumer;
  using ::hid::usage::Digitizer;
  using ::hid::usage::GenericDesktop;
  using ::hid::usage::Page;
  using ::hid::usage::Sensor;
  struct {
    hid::Usage usage;
    Protocol protocol;
  } usage_to_protocol[] = {
      {{static_cast<uint16_t>(Page::kConsumer),
        static_cast<uint32_t>(Consumer::kConsumerControl)},
       Protocol::Buttons},
      {{static_cast<uint16_t>(Page::kDigitizer),
        static_cast<uint32_t>(Digitizer::kTouchScreen)},
       Protocol::Touch},
      {{static_cast<uint16_t>(Page::kDigitizer),
        static_cast<uint32_t>(Digitizer::kTouchPad)},
       Protocol::Touchpad},
      {{static_cast<uint16_t>(Page::kGenericDesktop),
        static_cast<uint32_t>(GenericDesktop::kMouse)},
       Protocol::Mouse},
      // Add more sensors here
  };

  if (input.page == Page::kSensor) {
    return Protocol::Sensor;
  }

  for (auto& j : usage_to_protocol) {
    if (input.page == j.usage.page && input.usage == j.usage.usage) {
      return j.protocol;
    }
  }
  return Protocol::Other;
}

bool InputInterpreter::ParseProtocol() {
  HidDecoder::BootMode boot_mode = hid_decoder_->ReadBootMode();
  // For most keyboards and mouses Zircon requests the boot protocol
  // which has a fixed layout. This covers the following two cases:
  if (boot_mode == HidDecoder::BootMode::KEYBOARD) {
    protocol_ = Protocol::Keyboard;
    return true;
  }
  if (boot_mode == HidDecoder::BootMode::MOUSE) {
    protocol_ = Protocol::BootMouse;
    return true;
  }

  // For the rest of devices (fuchsia_hardware_input_BootProtocol_NONE) we need
  // to parse the report descriptor. The legacy method involves memcmp() of
  // known descriptors which cover the next 8 devices:

  int desc_size;
  auto desc = hid_decoder_->ReadReportDescriptor(&desc_size);
  if (desc_size == 0) {
    return false;
  }

  if (is_acer12_touch_report_desc(desc.data(), desc.size())) {
    protocol_ = Protocol::Acer12Touch;
    return true;
  }
  if (is_samsung_touch_report_desc(desc.data(), desc.size())) {
    hid_decoder_->SetupDevice(HidDecoder::Device::SAMSUNG);
    protocol_ = Protocol::SamsungTouch;
    return true;
  }
  if (is_paradise_touch_report_desc(desc.data(), desc.size())) {
    protocol_ = Protocol::ParadiseV1Touch;
    return true;
  }
  if (is_paradise_touch_v2_report_desc(desc.data(), desc.size())) {
    protocol_ = Protocol::ParadiseV2Touch;
    return true;
  }
  if (is_paradise_touch_v3_report_desc(desc.data(), desc.size())) {
    protocol_ = Protocol::ParadiseV3Touch;
    return true;
  }
  if (USE_TOUCHPAD_HARDCODED_REPORTS) {
    if (is_paradise_touchpad_v1_report_desc(desc.data(), desc.size())) {
      protocol_ = Protocol::ParadiseV1TouchPad;
      return true;
    }
    if (is_paradise_touchpad_v2_report_desc(desc.data(), desc.size())) {
      protocol_ = Protocol::ParadiseV2TouchPad;
      return true;
    }
  }
  if (is_egalax_touchscreen_report_desc(desc.data(), desc.size())) {
    protocol_ = Protocol::EgalaxTouch;
    return true;
  }
  if (is_paradise_sensor_report_desc(desc.data(), desc.size())) {
    protocol_ = Protocol::ParadiseSensor;
    return true;
  }
  if (is_eyoyo_touch_report_desc(desc.data(), desc.size())) {
    hid_decoder_->SetupDevice(HidDecoder::Device::EYOYO);
    protocol_ = Protocol::EyoyoTouch;
    return true;
  }
  // TODO(SCN-867) Use HID parsing for all touch devices
  // will remove the need for this
  if (is_ft3x27_touch_report_desc(desc.data(), desc.size())) {
    hid_decoder_->SetupDevice(HidDecoder::Device::FT3X27);
    protocol_ = Protocol::Ft3x27Touch;
    return true;
  }

  // For the rest of devices we use the new way; with the hid-parser
  // library.

  hid::DeviceDescriptor* dev_desc = nullptr;
  auto parse_res =
      hid::ParseReportDescriptor(desc.data(), desc.size(), &dev_desc);
  if (parse_res != hid::ParseResult::kParseOk) {
    FXL_LOG(ERROR) << "hid-parser: error " << int(parse_res)
                   << " parsing report descriptor for " << name();
    return false;
  }

  auto count = dev_desc->rep_count;
  if (count == 0) {
    FXL_LOG(ERROR) << "no report descriptors for " << name();
    return false;
  }

  // Find the first input report.
  const hid::ReportDescriptor* input_desc = nullptr;
  for (size_t rep = 0; rep < count; rep++) {
    const hid::ReportDescriptor* desc = &dev_desc->report[rep];
    if (desc->input_count != 0) {
      input_desc = desc;
      break;
    }
  }

  if (input_desc == nullptr) {
    FXL_LOG(ERROR) << "no input report fields for " << name();
    return false;
  }

  // Traverse up the nested collections to the Application collection.
  auto collection = input_desc->input_fields[0].col;
  while (collection != nullptr) {
    if (collection->type == hid::CollectionType::kApplication) {
      break;
    }
    collection = collection->parent;
  }

  if (collection == nullptr) {
    FXL_LOG(ERROR) << "invalid HID collection for " << name();
    return false;
  }

  FXL_LOG(INFO) << "hid-parser succesful for " << name() << " with usage page "
                << collection->usage.page << " and usage "
                << collection->usage.usage;

  InputDevice input_device = {};
  input_device.report = fuchsia::ui::input::InputReport::New();

  // Most modern gamepads report themselves as Joysticks. Madness.
  if (collection->usage.page == hid::usage::Page::kGenericDesktop &&
      collection->usage.usage == hid::usage::GenericDesktop::kJoystick &&
      hardcoded_.ParseGamepadDescriptor(input_desc->input_fields,
                                        input_desc->input_count)) {
    protocol_ = Protocol::Gamepad;
    return true;
  } else {
    protocol_ = ExtractProtocol(collection->usage);
    switch (protocol_) {
      case Protocol::LightSensor:
        hardcoded_.ParseAmbientLightDescriptor(input_desc->input_fields,
                                               input_desc->input_count);
        return true;
      case Protocol::Buttons: {
        FXL_VLOG(2) << "Device " << name() << " has HID buttons";

        input_device.device = std::make_unique<Buttons>();
        input_device.report->buttons = fuchsia::ui::input::ButtonsReport::New();
        break;
      }
      case Protocol::Sensor: {
        FXL_VLOG(2) << "Device " << name() << " has HID sensor";

        input_device.device = std::make_unique<Sensor>();
        input_device.report->sensor = fuchsia::ui::input::SensorReport::New();
        break;
      }
      case Protocol::Touchpad: {
        FXL_VLOG(2) << "Device " << name() << " has HID touchpad";

        input_device.device = std::make_unique<Touchpad>();
        input_device.report->mouse = fuchsia::ui::input::MouseReport::New();
        break;
      }
      case Protocol::Touch: {
        FXL_VLOG(2) << "Device " << name() << " has HID touch";

        input_device.device = std::make_unique<TouchScreen>();
        input_device.report->touchscreen =
            fuchsia::ui::input::TouchscreenReport::New();
        break;
      }
      case Protocol::Mouse: {
        FXL_VLOG(2) << "Device " << name() << " has HID mouse";

        input_device.device = std::make_unique<Mouse>();
        input_device.report->mouse = fuchsia::ui::input::MouseReport::New();
        break;
      }
      // Add more protocols here
      default:
        return false;
    }
  }

  if (!input_device.device->ParseReportDescriptor(*input_desc,
                                                  &input_device.descriptor)) {
    FXL_LOG(ERROR) << "invalid report descriptor for " << name();
    return false;
  }
  devices_.push_back(std::move(input_device));

  return true;
}

}  // namespace mozart

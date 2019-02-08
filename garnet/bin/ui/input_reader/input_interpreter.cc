// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/input_interpreter.h"

#include <fuchsia/hardware/input/c/fidl.h>
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

#include <sys/types.h>
#include <sys/uio.h>
#include <zircon/device/device.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <trace/event.h>

#include <fuchsia/ui/input/cpp/fidl.h>
#include "garnet/bin/ui/input_reader/fdio_hid_decoder.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/arraysize.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_point.h"
#include "lib/ui/input/cpp/formatting.h"

namespace {

// Variable to quickly re-enable the hardcoded touchpad reports.
// TODO(ZX-3219): Remove this once touchpads are stable
bool USE_TOUCHPAD_HARDCODED_REPORTS = false;

// TODO(SCN-843): We need to generalize these extraction functions

// Casting from unsigned to signed can change the bit pattern so
// we need to resort to this method.
int8_t signed_bit_cast(uint8_t src) {
  int8_t dest;
  memcpy(&dest, &src, sizeof(uint8_t));
  return dest;
}

// Extracts up to 8 bits unsigned number from a byte array |v|.
// Both |begin| and |count| are in bits units. This function does not
// check for the array being long enough.
static uint8_t extract_uint8(const uint8_t* v, uint32_t begin, uint32_t count) {
  uint8_t val = v[begin / 8u] >> (begin % 8u);
  return (count < 8) ? (val & ~(1u << count)) : val;
}

// Extracts a 16 bits unsigned number from a byte array |v|.
// |begin| is in bits units. This function does not check for the array
// being long enough.
static uint16_t extract_uint16(const uint8_t* v, uint32_t begin) {
  return static_cast<uint16_t>(extract_uint8(v, begin, 8)) |
         static_cast<uint16_t>(extract_uint8(v, begin + 8, 8)) << 8;
}

// Extracts up to 8 bits sign extended to int32_t from a byte array |v|.
// Both |begin| and |count| are in bits units. This function does not
// check for the array being long enough.
static int32_t extract_int8_ext(const uint8_t* v, uint32_t begin,
                                uint32_t count) {
  uint8_t val = extract_uint8(v, begin, count);
  return signed_bit_cast(val);
}

int64_t InputEventTimestampNow() {
  return fxl::TimePoint::Now().ToEpochDelta().ToNanoseconds();
}

fuchsia::ui::input::InputReport CloneReport(
    const fuchsia::ui::input::InputReportPtr& report) {
  fuchsia::ui::input::InputReport result;
  fidl::Clone(*report, &result);
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
  memset(acer12_touch_reports_, 0, 2 * sizeof(acer12_touch_t));
  FXL_DCHECK(hid_decoder_);
}

InputInterpreter::~InputInterpreter() {}

bool InputInterpreter::Initialize() {
  if (!hid_decoder_->Init())
    return false;

  if (!ParseProtocol())
    return false;

  auto protocol = protocol_;

  if (protocol == InputInterpreter::Protocol::Keyboard) {
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
  } else if (protocol == InputInterpreter::Protocol::Buttons) {
    FXL_VLOG(2) << "Device " << name() << " has buttons";
    has_buttons_ = true;
    buttons_descriptor_ = fuchsia::ui::input::ButtonsDescriptor::New();
    buttons_descriptor_->buttons |= fuchsia::ui::input::kVolumeUp;
    buttons_descriptor_->buttons |= fuchsia::ui::input::kVolumeDown;
    buttons_descriptor_->buttons |= fuchsia::ui::input::kMicMute;
    buttons_report_ = fuchsia::ui::input::InputReport::New();
    buttons_report_->buttons = fuchsia::ui::input::ButtonsReport::New();
  } else if (protocol == InputInterpreter::Protocol::Mouse) {
    FXL_VLOG(2) << "Device " << name() << " has mouse";
    has_mouse_ = true;
    mouse_device_type_ = MouseDeviceType::HID;

    mouse_descriptor_ = fuchsia::ui::input::MouseDescriptor::New();
    // At the moment all mice send relative units, so these min and max values
    // do not affect anything. Set them to maximum range.
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
  } else if (protocol == InputInterpreter::Protocol::BootMouse ||
             protocol == InputInterpreter::Protocol::Gamepad) {
    FXL_VLOG(2) << "Device " << name() << " has mouse";
    has_mouse_ = true;
    mouse_device_type_ = (protocol == InputInterpreter::Protocol::BootMouse)
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
  } else if (protocol == InputInterpreter::Protocol::Touch) {
    FXL_VLOG(2) << "Device " << name() << " has hid touch";

    has_touchscreen_ = true;
    touchscreen_descriptor_ = fuchsia::ui::input::TouchscreenDescriptor::New();
    Touchscreen::Descriptor touch_desc;
    SetDescriptor(&touch_desc);
    touchscreen_descriptor_->x.range.min = touch_desc.x_min;
    touchscreen_descriptor_->x.range.max = touch_desc.x_max;
    touchscreen_descriptor_->x.resolution = touch_desc.x_resolution;

    touchscreen_descriptor_->y.range.min = touch_desc.y_min;
    touchscreen_descriptor_->y.range.max = touch_desc.y_max;
    touchscreen_descriptor_->y.resolution = touch_desc.x_resolution;

    touchscreen_descriptor_->max_finger_id = touch_desc.max_finger_id;

    touchscreen_report_ = fuchsia::ui::input::InputReport::New();
    touchscreen_report_->touchscreen =
        fuchsia::ui::input::TouchscreenReport::New();

    touch_device_type_ = TouchDeviceType::HID;
  } else if (protocol == InputInterpreter::Protocol::Touchpad) {
    FXL_VLOG(2) << "Device " << name() << " has hid touchpad";

    has_mouse_ = true;
    mouse_descriptor_ = fuchsia::ui::input::MouseDescriptor::New();
    mouse_device_type_ = MouseDeviceType::TOUCH;

    mouse_descriptor_->rel_x.range.min = INT32_MIN;
    mouse_descriptor_->rel_x.range.max = INT32_MAX;
    mouse_descriptor_->rel_x.resolution = 1;

    mouse_descriptor_->rel_y.range.min = INT32_MIN;
    mouse_descriptor_->rel_y.range.max = INT32_MAX;
    mouse_descriptor_->rel_y.resolution = 1;

    mouse_descriptor_->buttons |= fuchsia::ui::input::kMouseButtonPrimary;

    mouse_report_ = fuchsia::ui::input::InputReport::New();
    mouse_report_->mouse = fuchsia::ui::input::MouseReport::New();
  } else if (protocol == InputInterpreter::Protocol::Acer12Touch) {
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
  } else if (protocol == InputInterpreter::Protocol::SamsungTouch) {
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
  } else if (protocol == InputInterpreter::Protocol::ParadiseV1Touch) {
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
  } else if (protocol == InputInterpreter::Protocol::ParadiseV2Touch) {
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
  } else if (protocol == InputInterpreter::Protocol::ParadiseV3Touch) {
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
  } else if (protocol == InputInterpreter::Protocol::ParadiseV1TouchPad) {
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
  } else if (protocol == InputInterpreter::Protocol::ParadiseV2TouchPad) {
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
  } else if (protocol == InputInterpreter::Protocol::EgalaxTouch) {
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
  } else if (protocol == InputInterpreter::Protocol::ParadiseSensor) {
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
  } else if (protocol == InputInterpreter::Protocol::EyoyoTouch) {
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
  } else if (protocol == InputInterpreter::Protocol::LightSensor) {
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
  } else if (protocol == InputInterpreter::Protocol::EyoyoTouch) {
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
  } else if (protocol == InputInterpreter::Protocol::Ft3x27Touch) {
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
  } else {
    FXL_VLOG(2) << "Device " << name() << " has unsupported HID device";
    return false;
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
  registry_->RegisterDevice(std::move(descriptor), input_device_.NewRequest());
}

bool InputInterpreter::Read(bool discard) {
  // If positive |rc| is the number of bytes read. If negative the error
  // while reading.
  int rc = 1;
  auto report = hid_decoder_->Read(&rc);

  if (rc < 1) {
    FXL_LOG(ERROR) << "Failed to read from input: " << rc << " for " << name();
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

  if (has_buttons_) {
    if (!ParseButtonsReport(report.data(), rc))
      return false;

    if (!discard) {
      input_device_->DispatchReport(CloneReport(buttons_report_));
    }
  }

  switch (mouse_device_type_) {
    case MouseDeviceType::BOOT:
      ParseMouseReport(report.data(), rc);
      if (!discard) {
        input_device_->DispatchReport(CloneReport(mouse_report_));
      }
      break;
    case MouseDeviceType::TOUCH:
      Touchscreen::Report touch_report;
      if (!ParseReport(report.data(), rc, &touch_report)) {
        FXL_LOG(ERROR) << " failed reading from touchpad";
        return false;
      }

      if (ParseTouchpadReport(&touch_report)) {
        if (!discard) {
          input_device_->DispatchReport(CloneReport(mouse_report_));
        }
      }
      break;
    case MouseDeviceType::HID:
      Mouse::Report mouse_report;
      if (!ParseReport(report.data(), rc, &mouse_report)) {
        FXL_LOG(ERROR) << " failed reading from mouse";
        return false;
      }

      if (ParseHidMouseReport(&mouse_report)) {
        if (!discard) {
          input_device_->DispatchReport(CloneReport(mouse_report_));
        }
      }
      break;
    case MouseDeviceType::PARADISEv1:
      if (ParseParadiseTouchpadReport<paradise_touchpad_v1_t>(report.data(),
                                                              rc)) {
        if (!discard) {
          input_device_->DispatchReport(CloneReport(mouse_report_));
        }
      }
      break;
    case MouseDeviceType::PARADISEv2:
      if (ParseParadiseTouchpadReport<paradise_touchpad_v2_t>(report.data(),
                                                              rc)) {
        if (!discard) {
          input_device_->DispatchReport(CloneReport(mouse_report_));
        }
      }
      break;
    case MouseDeviceType::GAMEPAD:
      // TODO(cpu): remove this once we have a good way to test gamepad.
      HidGamepadSimple gamepad;
      if (!ParseReport(report.data(), rc, &gamepad)) {
        FXL_LOG(ERROR) << " failed reading from gamepad ";
        return false;
      }
      ParseGamepadMouseReport(&gamepad);
      if (!discard) {
        input_device_->DispatchReport(CloneReport(mouse_report_));
      }
      break;
    case MouseDeviceType::NONE:
      break;
  }

  switch (touch_device_type_) {
    case TouchDeviceType::HID:
      Touchscreen::Report touch_report;
      if (!ParseReport(report.data(), rc, &touch_report)) {
        FXL_LOG(ERROR) << " failed reading from touchscreen ";
        return false;
      }

      if (ParseTouchscreenReport(&touch_report)) {
        if (!discard) {
          input_device_->DispatchReport(CloneReport(touchscreen_report_));
        }
      }
      break;
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
        if (ParseParadiseTouchscreenReport<paradise_touch_t>(report.data(),
                                                             rc)) {
          if (!discard) {
            input_device_->DispatchReport(CloneReport(touchscreen_report_));
          }
        }
      }
      break;
    case TouchDeviceType::PARADISEv2:
      if (report[0] == PARADISE_RPT_ID_TOUCH) {
        if (ParseParadiseTouchscreenReport<paradise_touch_v2_t>(report.data(),
                                                                rc)) {
          if (!discard) {
            input_device_->DispatchReport(CloneReport(touchscreen_report_));
          }
        }
      } else if (report[0] == PARADISE_RPT_ID_STYLUS) {
        if (ParseParadiseStylusReport(report.data(), rc)) {
          if (!discard) {
            input_device_->DispatchReport(CloneReport(stylus_report_));
          }
        }
      }
      break;
    case TouchDeviceType::PARADISEv3:
      if (report[0] == PARADISE_RPT_ID_TOUCH) {
        if (ParseParadiseTouchscreenReport<paradise_touch_t>(report.data(),
                                                             rc)) {
          if (!discard) {
            input_device_->DispatchReport(CloneReport(touchscreen_report_));
          }
        }
      } else if (report[0] == PARADISE_RPT_ID_STYLUS) {
        if (ParseParadiseStylusReport(report.data(), rc)) {
          if (!discard) {
            input_device_->DispatchReport(CloneReport(stylus_report_));
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

    case TouchDeviceType::EYOYO:
      if (report[0] == EYOYO_RPT_ID_TOUCH) {
        if (ParseEyoyoTouchscreenReport(report.data(), rc)) {
          if (!discard) {
            input_device_->DispatchReport(CloneReport(touchscreen_report_));
          }
        }
      }
      break;
    case TouchDeviceType::FT3X27:
      if (report[0] == FT3X27_RPT_ID_TOUCH) {
        if (ParseFt3x27TouchscreenReport(report.data(), rc)) {
          if (!discard) {
            input_device_->DispatchReport(CloneReport(touchscreen_report_));
          }
        }
      }
      break;

    default:
      break;
  }

  switch (sensor_device_type_) {
    case SensorDeviceType::PARADISE:
      if (ParseParadiseSensorReport(report.data(), rc)) {
        if (!discard) {
          FXL_DCHECK(sensor_idx_ < kMaxSensorCount);
          FXL_DCHECK(sensor_devices_[sensor_idx_]);
          sensor_devices_[sensor_idx_]->DispatchReport(
              CloneReport(sensor_report_));
        }
      }
      break;
    case SensorDeviceType::AMBIENT_LIGHT:
      if (ParseAmbientLightSensorReport(report.data(), rc)) {
        if (!discard) {
          FXL_DCHECK(sensor_idx_ < kMaxSensorCount);
          FXL_DCHECK(sensor_devices_[sensor_idx_]);
          sensor_devices_[sensor_idx_]->DispatchReport(
              CloneReport(sensor_report_));
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

  auto& pressed_keys = keyboard_report_->keyboard->pressed_keys;
  pressed_keys.resize(0);
  hid_for_every_key(&key_state, keycode) { pressed_keys.push_back(keycode); }
  FXL_VLOG(2) << name() << " parsed: " << *keyboard_report_;
}

void InputInterpreter::ParseMouseReport(uint8_t* r, size_t len) {
  auto report = reinterpret_cast<hid_boot_mouse_report_t*>(r);
  mouse_report_->event_time = InputEventTimestampNow();

  mouse_report_->mouse->rel_x = report->rel_x;
  mouse_report_->mouse->rel_y = report->rel_y;
  mouse_report_->mouse->pressed_buttons = report->buttons;
  FXL_VLOG(2) << name() << " parsed: " << *mouse_report_;
}

void InputInterpreter::ParseGamepadMouseReport(
    // TODO(cpu): remove this once we have a better way to test gamepads.
    const HidGamepadSimple* gamepad) {
  mouse_report_->event_time = InputEventTimestampNow();

  mouse_report_->mouse->rel_x = gamepad->left_x;
  mouse_report_->mouse->rel_y = gamepad->left_y;
  mouse_report_->mouse->pressed_buttons = gamepad->hat_switch;
}

bool InputInterpreter::ParseHidMouseReport(const Mouse::Report* report) {
  mouse_report_->event_time = InputEventTimestampNow();

  mouse_report_->mouse->rel_x = report->rel_x;
  mouse_report_->mouse->rel_y = report->rel_y;

  mouse_report_->mouse->pressed_buttons = 0;
  mouse_report_->mouse->pressed_buttons |=
      report->left_click ? fuchsia::ui::input::kMouseButtonPrimary : 0;
  return true;
}

// This logic converts the multi-finger report from the touchpad into
// a mouse report. It does this by only tracking the first finger that
// is placed down, and converting the absolution finger position into
// relative X and Y movements. All other fingers besides the tracking
// finger are ignored.
bool InputInterpreter::ParseTouchpadReport(Touchscreen::Report* report) {
  mouse_report_->event_time = InputEventTimestampNow();
  mouse_report_->mouse->rel_x = 0;
  mouse_report_->mouse->rel_y = 0;
  mouse_report_->mouse->pressed_buttons = 0;

  // If all fingers are lifted reset our tracking finger.
  if (report->contact_count == 0) {
    has_touch_ = false;
    tracking_finger_was_lifted_ = true;
    return true;
  }

  // If we don't have a tracking finger then set one.
  if (!has_touch_) {
    has_touch_ = true;
    tracking_finger_was_lifted_ = false;
    tracking_finger_id_ = report->contacts[0].id;

    mouse_abs_x_ = report->contacts[0].x;
    mouse_abs_y_ = report->contacts[0].y;
    return true;
  }

  // Find the finger we are tracking.
  Touchscreen::ContactReport* contact = nullptr;
  for (size_t i = 0; i < report->contact_count; i++) {
    if (report->contacts[i].id == tracking_finger_id_) {
      contact = &report->contacts[i];
      break;
    }
  }

  // If our tracking finger isn't pressed return early.
  if (contact == nullptr) {
    tracking_finger_was_lifted_ = true;
    return true;
  }

  // If our tracking finger was lifted then reset the abs values otherwise
  // the pointer will jump rapidly.
  if (tracking_finger_was_lifted_) {
    tracking_finger_was_lifted_ = false;
    mouse_abs_x_ = contact->x;
    mouse_abs_y_ = contact->y;
  }

  // The touch driver returns in units of 10^-5m, but the resolution expected
  // by |mouse_report_| is 10^-3.
  mouse_report_->mouse->rel_x = (contact->x - mouse_abs_x_) / 100;
  mouse_report_->mouse->rel_y = (contact->y - mouse_abs_y_) / 100;

  mouse_report_->mouse->pressed_buttons =
      report->button ? fuchsia::ui::input::kMouseButtonPrimary : 0;

  mouse_abs_x_ = report->contacts[0].x;
  mouse_abs_y_ = report->contacts[0].y;

  return true;
}

bool InputInterpreter::ParseTouchscreenReport(Touchscreen::Report* report) {
  touchscreen_report_->event_time = InputEventTimestampNow();
  touchscreen_report_->touchscreen->touches.resize(report->contact_count);

  for (size_t i = 0; i < report->contact_count; ++i) {
    fuchsia::ui::input::Touch touch;
    touch.finger_id = report->contacts[i].id;
    touch.x = report->contacts[i].x;
    touch.y = report->contacts[i].y;
    // TODO(SCN-1188): Add support for contact ellipse.
    touch.width = 5;
    touch.height = 5;
    touchscreen_report_->touchscreen->touches.at(i) = std::move(touch);
  }

  return true;
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
    stylus_report_->stylus->pressed_buttons |=
        fuchsia::ui::input::kStylusBarrel;
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
      fuchsia::ui::input::Touch touch;
      touch.finger_id = acer12_finger_id_contact(fid);
      touch.x = acer12_touch_reports_[i].fingers[c].x;
      touch.y = acer12_touch_reports_[i].fingers[c].y;
      touch.width = acer12_touch_reports_[i].fingers[c].width;
      touch.height = acer12_touch_reports_[i].fingers[c].height;
      touchscreen_report_->touchscreen->touches.resize(index + 1);
      touchscreen_report_->touchscreen->touches.at(index++) = std::move(touch);
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

  for (size_t i = 0; i < arraysize(report.fingers); ++i) {
    auto fid = report.fingers[i].finger_id;

    if (!samsung_finger_id_tswitch(fid))
      continue;

    fuchsia::ui::input::Touch touch;
    touch.finger_id = samsung_finger_id_contact(fid);
    touch.x = report.fingers[i].x;
    touch.y = report.fingers[i].y;
    touch.width = report.fingers[i].width;
    touch.height = report.fingers[i].height;
    touchscreen_report_->touchscreen->touches.resize(index + 1);
    touchscreen_report_->touchscreen->touches.at(index++) = std::move(touch);
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

  for (size_t i = 0; i < arraysize(report.fingers); ++i) {
    if (!paradise_finger_flags_tswitch(report.fingers[i].flags))
      continue;

    fuchsia::ui::input::Touch touch;
    touch.finger_id = report.fingers[i].finger_id;
    touch.x = report.fingers[i].x;
    touch.y = report.fingers[i].y;
    touch.width = 5;  // TODO(cpu): Don't hardcode |width| or |height|.
    touch.height = 5;
    touchscreen_report_->touchscreen->touches.resize(index + 1);
    touchscreen_report_->touchscreen->touches.at(index++) = std::move(touch);
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
    fuchsia::ui::input::Touch touch;
    touch.finger_id = 0;
    touch.x = report.x;
    touch.y = report.y;
    touch.width = 5;
    touch.height = 5;
    touchscreen_report_->touchscreen->touches.resize(1);
    touchscreen_report_->touchscreen->touches.at(0) = std::move(touch);
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
      report.button ? fuchsia::ui::input::kMouseButtonPrimary : 0;

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

// Writes out result to sensor_report_ and sensor_idx_.
bool InputInterpreter::ParseParadiseSensorReport(uint8_t* r, size_t len) {
  if (len != sizeof(paradise_sensor_vector_data_t) &&
      len != sizeof(paradise_sensor_scalar_data_t)) {
    FXL_LOG(INFO) << "paradise sensor data: wrong size " << len << ", expected "
                  << sizeof(paradise_sensor_vector_data_t) << " or "
                  << sizeof(paradise_sensor_scalar_data_t);
    return false;
  }

  sensor_report_->event_time = InputEventTimestampNow();
  sensor_idx_ = r[0];  // We know sensor structs start with sensor ID.
  switch (sensor_idx_) {
    case kParadiseAccLid:
    case kParadiseAccBase: {
      const auto& report =
          *(reinterpret_cast<paradise_sensor_vector_data_t*>(r));
      fidl::Array<int16_t, 3> data;
      data[0] = report.vector[0];
      data[1] = report.vector[1];
      data[2] = report.vector[2];
      sensor_report_->sensor->set_vector(std::move(data));
    } break;
    case 2:
    case 3:
    case 4:
      // TODO(SCN-626): Expose other sensors.
      return false;
    default:
      FXL_LOG(ERROR) << "paradise sensor unrecognized: " << sensor_idx_;
      return false;
  }

  FXL_VLOG(3) << name()
              << " parsed (sensor=" << static_cast<uint16_t>(sensor_idx_)
              << "): " << *sensor_report_;
  return true;
}

bool InputInterpreter::ParseParadiseStylusReport(uint8_t* r, size_t len) {
  if (len != sizeof(paradise_stylus_t)) {
    FXL_LOG(INFO) << "paradise wrong stylus report size " << len;
    return false;
  }

  auto report = reinterpret_cast<paradise_stylus_t*>(r);
  stylus_report_->event_time = InputEventTimestampNow();

  stylus_report_->stylus->x = report->x;
  stylus_report_->stylus->y = report->y;
  stylus_report_->stylus->pressure = report->pressure;

  stylus_report_->stylus->is_in_contact =
      paradise_stylus_status_inrange(report->status) &&
      (paradise_stylus_status_tswitch(report->status) ||
       paradise_stylus_status_eraser(report->status));

  stylus_report_->stylus->in_range =
      paradise_stylus_status_inrange(report->status);

  if (paradise_stylus_status_invert(report->status) ||
      paradise_stylus_status_eraser(report->status)) {
    stylus_report_->stylus->is_inverted = true;
  }

  if (paradise_stylus_status_barrel(report->status)) {
    stylus_report_->stylus->pressed_buttons |=
        fuchsia::ui::input::kStylusBarrel;
  }
  FXL_VLOG(2) << name() << " parsed: " << *stylus_report_;

  return true;
}

// Writes out result to sensor_report_ and sensor_idx_.
bool InputInterpreter::ParseAmbientLightSensorReport(const uint8_t* report,
                                                     size_t len) {
  HidAmbientLightSimple data;
  if (!ParseReport(report, len, &data)) {
    FXL_LOG(ERROR) << " failed reading from ambient light sensor";
    return false;
  }
  sensor_report_->sensor->set_scalar(data.illuminance);
  sensor_report_->event_time = InputEventTimestampNow();
  sensor_idx_ = kAmbientLight;

  FXL_VLOG(2) << name()
              << " parsed (sensor=" << static_cast<uint16_t>(sensor_idx_)
              << "): " << *sensor_report_;
  return true;
}

bool InputInterpreter::ParseButtonsReport(const uint8_t* report, size_t len) {
  HidButtons data;
  if (!ParseReport(report, len, &data)) {
    FXL_LOG(ERROR) << " failed reading from buttons";
    return false;
  }
  buttons_report_->buttons->set_volume(data.volume);
  buttons_report_->buttons->set_mic_mute(data.mic_mute);
  buttons_report_->event_time = InputEventTimestampNow();

  FXL_VLOG(2) << name() << " parsed buttons: " << *buttons_report_
              << " volume: " << static_cast<int32_t>(data.volume)
              << " mic mute: " << (data.mic_mute ? "yes" : "no");
  return true;
}

bool InputInterpreter::ParseEyoyoTouchscreenReport(uint8_t* r, size_t len) {
  if (len != sizeof(eyoyo_touch_t)) {
    return false;
  }

  const auto& report = *(reinterpret_cast<eyoyo_touch_t*>(r));
  touchscreen_report_->event_time = InputEventTimestampNow();

  size_t index = 0;
  touchscreen_report_->touchscreen->touches.resize(index);

  for (size_t i = 0; i < arraysize(report.fingers); ++i) {
    auto fid = report.fingers[i].finger_id;

    if (!eyoyo_finger_id_tswitch(fid))
      continue;

    fuchsia::ui::input::Touch touch;
    touch.finger_id = eyoyo_finger_id_contact(fid);
    touch.x = report.fingers[i].x;
    touch.y = report.fingers[i].y;
    // Panel does not support touch width/height.
    touch.width = 5;
    touch.height = 5;
    touchscreen_report_->touchscreen->touches.resize(index + 1);
    touchscreen_report_->touchscreen->touches.at(index++) = std::move(touch);
  }

  return true;
}

bool InputInterpreter::ParseFt3x27TouchscreenReport(uint8_t* r, size_t len) {
  if (len != sizeof(ft3x27_touch_t)) {
    return false;
  }

  const auto& report = *(reinterpret_cast<ft3x27_touch_t*>(r));
  touchscreen_report_->event_time = InputEventTimestampNow();

  size_t index = 0;
  touchscreen_report_->touchscreen->touches.resize(index);

  for (size_t i = 0; i < arraysize(report.fingers); ++i) {
    auto fid = report.fingers[i].finger_id;

    if (!ft3x27_finger_id_tswitch(fid))
      continue;

    fuchsia::ui::input::Touch touch;
    touch.finger_id = ft3x27_finger_id_contact(fid);
    touch.x = report.fingers[i].x;
    touch.y = report.fingers[i].y;
    touch.width = 5;
    touch.height = 5;
    touchscreen_report_->touchscreen->touches.resize(index + 1);
    touchscreen_report_->touchscreen->touches.at(index++) = std::move(touch);
    FXL_VLOG(2) << name()
                << " parsed (sensor=" << static_cast<uint16_t>(touch.finger_id)
                << ") x=" << touch.x << ", y=" << touch.y;
  }

  return true;
}

InputInterpreter::Protocol ExtractProtocol(hid::Usage input) {
  using ::hid::usage::Consumer;
  using ::hid::usage::Digitizer;
  using ::hid::usage::GenericDesktop;
  using ::hid::usage::Page;
  using ::hid::usage::Sensor;
  struct {
    hid::Usage usage;
    InputInterpreter::Protocol protocol;
  } usage_to_protocol[] = {
      {{static_cast<uint16_t>(Page::kSensor),
        static_cast<uint32_t>(Sensor::kAmbientLight)},
       InputInterpreter::Protocol::LightSensor},
      {{static_cast<uint16_t>(Page::kConsumer),
        static_cast<uint32_t>(Consumer::kConsumerControl)},
       InputInterpreter::Protocol::Buttons},
      {{static_cast<uint16_t>(Page::kDigitizer),
        static_cast<uint32_t>(Digitizer::kTouchScreen)},
       InputInterpreter::Protocol::Touch},
      {{static_cast<uint16_t>(Page::kDigitizer),
        static_cast<uint32_t>(Digitizer::kTouchPad)},
       InputInterpreter::Protocol::Touchpad},
      {{static_cast<uint16_t>(Page::kGenericDesktop),
        static_cast<uint32_t>(GenericDesktop::kMouse)},
       InputInterpreter::Protocol::Mouse},
      // Add more sensors here
  };
  for (auto& j : usage_to_protocol) {
    if (input.page == j.usage.page && input.usage == j.usage.usage) {
      return j.protocol;
    }
  }
  return InputInterpreter::Protocol::Other;
}

bool InputInterpreter::ParseProtocol() {
  HidDecoder::BootMode boot_mode = hid_decoder_->ReadBootMode();
  // For most keyboards and mouses Zircon requests the boot protocol
  // which has a fixed layout. This covers the following two cases:
  if (boot_mode == HidDecoder::BootMode::KEYBOARD) {
    protocol_ = InputInterpreter::Protocol::Keyboard;
    return true;
  }
  if (boot_mode == HidDecoder::BootMode::MOUSE) {
    protocol_ = InputInterpreter::Protocol::BootMouse;
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
    protocol_ = InputInterpreter::Protocol::Acer12Touch;
    return true;
  }
  if (is_samsung_touch_report_desc(desc.data(), desc.size())) {
    hid_decoder_->SetupDevice(HidDecoder::Device::SAMSUNG);
    protocol_ = InputInterpreter::Protocol::SamsungTouch;
    return true;
  }
  if (is_paradise_touch_report_desc(desc.data(), desc.size())) {
    protocol_ = InputInterpreter::Protocol::ParadiseV1Touch;
    return true;
  }
  if (is_paradise_touch_v2_report_desc(desc.data(), desc.size())) {
    protocol_ = InputInterpreter::Protocol::ParadiseV2Touch;
    return true;
  }
  if (is_paradise_touch_v3_report_desc(desc.data(), desc.size())) {
    protocol_ = InputInterpreter::Protocol::ParadiseV3Touch;
    return true;
  }
  if (USE_TOUCHPAD_HARDCODED_REPORTS) {
    if (is_paradise_touchpad_v1_report_desc(desc.data(), desc.size())) {
      protocol_ = InputInterpreter::Protocol::ParadiseV1TouchPad;
      return true;
    }
    if (is_paradise_touchpad_v2_report_desc(desc.data(), desc.size())) {
      protocol_ = InputInterpreter::Protocol::ParadiseV2TouchPad;
      return true;
    }
  }
  if (is_egalax_touchscreen_report_desc(desc.data(), desc.size())) {
    protocol_ = InputInterpreter::Protocol::EgalaxTouch;
    return true;
  }
  if (is_paradise_sensor_report_desc(desc.data(), desc.size())) {
    protocol_ = InputInterpreter::Protocol::ParadiseSensor;
    return true;
  }
  if (is_eyoyo_touch_report_desc(desc.data(), desc.size())) {
    hid_decoder_->SetupDevice(HidDecoder::Device::EYOYO);
    protocol_ = InputInterpreter::Protocol::EyoyoTouch;
    return true;
  }
  // TODO(SCN-867) Use HID parsing for all touch devices
  // will remove the need for this
  if (is_ft3x27_touch_report_desc(desc.data(), desc.size())) {
    hid_decoder_->SetupDevice(HidDecoder::Device::FT3X27);
    protocol_ = InputInterpreter::Protocol::Ft3x27Touch;
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
    FXL_LOG(ERROR) << "invalid hid collection for " << name();
    return false;
  }

  FXL_LOG(INFO) << "hid-parser succesful for " << name() << " with usage page "
                << collection->usage.page << " and usage "
                << collection->usage.usage;

  // Most modern gamepads report themselves as Joysticks. Madness.
  if (collection->usage.page == hid::usage::Page::kGenericDesktop &&
      collection->usage.usage == hid::usage::GenericDesktop::kJoystick &&
      ParseGamepadDescriptor(input_desc->input_fields,
                             input_desc->input_count)) {
    protocol_ = InputInterpreter::Protocol::Gamepad;
  } else {
    protocol_ = ExtractProtocol(collection->usage);
    switch (protocol_) {
      case InputInterpreter::Protocol::LightSensor:
        ParseAmbientLightDescriptor(input_desc->input_fields,
                                    input_desc->input_count);
        break;
      case InputInterpreter::Protocol::Buttons:
        ParseButtonsDescriptor(input_desc->input_fields,
                               input_desc->input_count);
        break;
      case InputInterpreter::Protocol::Touchpad:
        // Fallthrough
      case InputInterpreter::Protocol::Touch: {
        bool success = ts_.ParseTouchscreenDescriptor(input_desc);
        if (!success) {
          FXL_LOG(ERROR) << "invalid touchscreen descriptor for " << name();
          return false;
        }
        break;
      }
      case InputInterpreter::Protocol::Mouse: {
        bool success = mouse_.ParseDescriptor(input_desc);
        if (!success) {
          FXL_LOG(ERROR) << "invalid mouse descriptor for " << name();
          return false;
        }
        break;
      }
      // Add more protocols here
      default:
        return false;
    }
  }

  return true;
}

bool InputInterpreter::ParseGamepadDescriptor(const hid::ReportField* fields,
                                              size_t count) {
  // Need to recover the five fields as seen in HidGamepadSimple and put
  // them into the decoder_ in the same order.
  if (count < 5u)
    return false;

  decoder_.resize(6u);
  uint8_t offset = 0;

  if (fields[0].report_id != 0) {
    // If exists, the first entry (8-bits) is always the report id and
    // all items start after the first byte.
    decoder_[0] = DataLocator{0u, 8u, fields[0].report_id};
    offset = 8u;
  }

  // Needs to be kept in sync with HidGamepadSimple {}.
  const uint16_t table[] = {
      static_cast<uint16_t>(hid::usage::GenericDesktop::kX),         // left X.
      static_cast<uint16_t>(hid::usage::GenericDesktop::kY),         // left Y.
      static_cast<uint16_t>(hid::usage::GenericDesktop::kZ),         // right X.
      static_cast<uint16_t>(hid::usage::GenericDesktop::kRz),        // right Y.
      static_cast<uint16_t>(hid::usage::GenericDesktop::kHatSwitch)  // buttons
  };

  uint32_t bit_count = 0;

  // Traverse each input report field and see if there is a match in the table.
  // If so place the location in |decoder_| array.
  for (size_t ix = 0; ix != count; ix++) {
    if (fields[ix].type != hid::kInput)
      continue;

    for (size_t iy = 0; iy != arraysize(table); iy++) {
      if (fields[ix].attr.usage.usage == table[iy]) {
        // Found a required usage.
        decoder_[iy + 1] =
            DataLocator{bit_count + offset, fields[ix].attr.bit_sz, 0};
        break;
      }
    }

    bit_count += fields[ix].attr.bit_sz;
  }

  // Here |decoder_| should look like this:
  // [rept_id][left X][left Y]....[hat_sw]
  // With each box, the location in a report for each item, for example:
  // [0, 0, 0][24, 0, 0][8, 0, 0][0, 0, 0]...[64, 4, 0]
  return true;
}

bool InputInterpreter::ParseAmbientLightDescriptor(
    const hid::ReportField* fields, size_t count) {
  if (count == 0u)
    return false;

  decoder_.resize(2u);
  uint8_t offset = 0;

  if (fields[0].report_id != 0) {
    // If exists, the first entry (8-bits) is always the report id and
    // all items start after the first byte.
    decoder_[0] = DataLocator{0u, 8u, fields[0].report_id};
    offset = 8u;
  }

  uint32_t bit_count = 0;

  // Traverse each input report field and see if there is a match in the table.
  // If so place the location in |decoder_| array.
  for (size_t ix = 0; ix != count; ix++) {
    if (fields[ix].type != hid::kInput)
      continue;

    if (fields[ix].attr.usage.usage == hid::usage::Sensor::kLightIlluminance) {
      decoder_[1] = DataLocator{bit_count + offset, fields[ix].attr.bit_sz, 0};
      // Found a required usage.
      // Here |decoder_| should look like this:
      // [rept_id][abs_light]
      return true;
    }

    bit_count += fields[ix].attr.bit_sz;
  }
  return false;
}

bool InputInterpreter::ParseButtonsDescriptor(const hid::ReportField* fields,
                                              size_t count) {
  if (count == 0u)
    return false;

  decoder_.resize(3u);
  uint8_t offset = 0;

  if (fields[0].report_id != 0) {
    // If exists, the first entry (8-bits) is always the report id and
    // all items start after the first byte.
    decoder_[0] = DataLocator{0u, 8u, fields[0].report_id};
    offset = 8u;
  }

  // Needs to be kept in sync with HidButtons {}.
  const uint16_t table[] = {
      static_cast<uint16_t>(hid::usage::Consumer::kVolume),
      static_cast<uint16_t>(hid::usage::Telephony::kPhoneMute),
  };

  uint32_t bit_count = 0;

  // Traverse each input report field and see if there is a match in the table.
  // If so place the location in |decoder_| array.
  for (size_t ix = 0; ix != count; ix++) {
    if (fields[ix].type != hid::kInput)
      continue;

    for (size_t iy = 0; iy != arraysize(table); iy++) {
      if (fields[ix].attr.usage.usage == table[iy]) {
        // Found a required usage.
        decoder_[iy + 1] =
            DataLocator{bit_count + offset, fields[ix].attr.bit_sz, 0};
        break;
      }
    }

    bit_count += fields[ix].attr.bit_sz;
  }

  // Here |decoder_| should look like this:
  // [rept_id][volume][mic_mute]
  return true;
}

bool InputInterpreter::ParseReport(const uint8_t* report, size_t len,
                                   HidGamepadSimple* gamepad) {
  if (protocol_ != InputInterpreter::Protocol::Gamepad)
    return false;

  auto cur = &decoder_[0];
  if ((cur->match != 0) && (cur->count == 8u)) {
    // The first byte is the report id.
    if (report[0] != cur->match) {
      // This is a normal condition. The device can generate reports
      // for controls we don't yet handle.
      *gamepad = {};
      return true;
    }
    ++cur;
  }

  gamepad->left_x = extract_int8_ext(report, cur->begin, cur->count) / 2;
  ++cur;
  gamepad->left_y = extract_int8_ext(report, cur->begin, cur->count) / 2;
  ++cur;
  gamepad->right_x = extract_int8_ext(report, cur->begin, cur->count) / 2;
  ++cur;
  gamepad->right_y = extract_int8_ext(report, cur->begin, cur->count) / 2;
  ++cur;
  gamepad->hat_switch = extract_int8_ext(report, cur->begin, cur->count);
  return true;
}

bool InputInterpreter::ParseReport(const uint8_t* report, size_t len,
                                   HidAmbientLightSimple* data) {
  if (protocol_ != InputInterpreter::Protocol::LightSensor)
    return false;

  auto cur = &decoder_[0];
  if ((cur->match != 0) && (cur->count == 8u)) {
    // The first byte is the report id.
    if (report[0] != cur->match) {
      // This is a normal condition. The device can generate reports
      // for controls we don't yet handle.
      *data = {};
      return true;
    }
    ++cur;
  }
  if (cur->count != 16u) {
    FXL_LOG(ERROR) << "Unexpected count in report from ambient light:"
                   << cur->count;
    return false;
  }
  data->illuminance = extract_uint16(report, cur->begin);
  return true;
}

bool InputInterpreter::ParseReport(const uint8_t* report, size_t len,
                                   HidButtons* data) {
  if (protocol_ != InputInterpreter::Protocol::Buttons)
    return false;

  auto cur = &decoder_[0];
  if ((cur->match != 0) && (cur->count == 8u)) {
    // The first byte is the report id.
    if (report[0] != cur->match) {
      // This is a normal condition. The device can generate reports
      // for controls we don't yet handle.
      *data = {};
      return true;
    }
    ++cur;
  }

  // 2 bits, see zircon/system/ulib/hid's buttons.c and include/hid/buttons.h
  if (cur->count != 2u) {
    FXL_LOG(ERROR) << "Unexpected count in report from buttons:" << cur->count;
    return false;
  }
  // TODO(SCN-843): We need to generalize these extraction functions, e.g. add
  // extract_int8
  data->volume = extract_uint8(report, cur->begin, 2u);
  if (data->volume == 3) {  // 2 bits unsigned 3 is signed -1
    data->volume = -1;
  }
  ++cur;

  // 1 bit, see zircon/system/ulib/hid's buttons.c and include/hid/buttons.h
  if (cur->count != 1u) {
    FXL_LOG(ERROR) << "Unexpected count in report from buttons:" << cur->count;
    return false;
  }
  data->mic_mute = extract_uint8(report, cur->begin, 1u);
  return true;
}

bool InputInterpreter::ParseReport(const uint8_t* report, size_t len,
                                   Touchscreen::Report* touchscreen) {
  if (report[0] != ts_.report_id()) {
    FXL_VLOG(0) << name() << " Touchscreen report "
                << static_cast<uint32_t>(report[0])
                << " does not match report id "
                << static_cast<uint32_t>(ts_.report_id());
    return false;
  }

  return ts_.ParseReport(report, len, touchscreen);
}

bool InputInterpreter::ParseReport(const uint8_t* report, size_t len,
                                   Mouse::Report* mouse) {
  if (report[0] != mouse_.report_id()) {
    FXL_VLOG(0) << name() << " Mouse report "
                << static_cast<uint32_t>(report[0])
                << " does not match report id "
                << static_cast<uint32_t>(mouse_.report_id());
    return false;
  }

  return mouse_.ParseReport(report, len, mouse);
}

bool InputInterpreter::SetDescriptor(Touchscreen::Descriptor* touch_desc) {
  return ts_.SetDescriptor(touch_desc);
}

}  // namespace mozart

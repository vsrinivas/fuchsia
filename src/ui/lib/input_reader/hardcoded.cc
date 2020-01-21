// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/input_reader/hardcoded.h"

#include <fuchsia/hardware/input/c/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/fostr/fidl/fuchsia/ui/input/formatting.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <zircon/errors.h>
#include <zircon/types.h>

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
#include <trace/event.h>

#include "src/lib/fxl/arraysize.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/time/time_point.h"

namespace {

int64_t InputEventTimestampNow() { return fxl::TimePoint::Now().ToEpochDelta().ToNanoseconds(); }

fuchsia::ui::input::InputReport CloneReport(const fuchsia::ui::input::InputReport& report) {
  fuchsia::ui::input::InputReport result;
  fidl::Clone(report, &result);
  return result;
}

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
static int32_t extract_int8_ext(const uint8_t* v, uint32_t begin, uint32_t count) {
  uint8_t val = extract_uint8(v, begin, count);
  return signed_bit_cast(val);
}

// TODO(SCN-473): Extract sensor IDs from HID.
const size_t kParadiseAccLid = 0;
const size_t kParadiseAccBase = 1;
const size_t kAmbientLight = 2;

}  // namespace

namespace ui_input {

bool Hardcoded::ParseGamepadDescriptor(const hid::ReportField* fields, size_t count) {
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
        decoder_[iy + 1] = DataLocator{bit_count + offset, fields[ix].attr.bit_sz, 0};
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

bool Hardcoded::ParseAmbientLightDescriptor(const hid::ReportField* fields, size_t count) {
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

void Hardcoded::ParseMouseReport(const uint8_t* r, size_t len,
                                 fuchsia::ui::input::InputReport* mouse_report) {
  auto report = reinterpret_cast<const hid_boot_mouse_report_t*>(r);
  mouse_report->event_time = InputEventTimestampNow();
  mouse_report->trace_id = TRACE_NONCE();

  mouse_report->mouse->rel_x = report->rel_x;
  mouse_report->mouse->rel_y = report->rel_y;
  mouse_report->mouse->pressed_buttons = report->buttons;
  FXL_VLOG(2) << name() << " parsed: " << *mouse_report;
}

bool Hardcoded::ParseReport(const uint8_t* report, size_t len, HidGamepadSimple* gamepad) {
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

bool Hardcoded::ParseGamepadMouseReport(const uint8_t* report, size_t len,
                                        fuchsia::ui::input::InputReport* mouse_report) {
  HidGamepadSimple gamepad = {};
  if (!ParseReport(report, len, &gamepad))
    return false;
  mouse_report->event_time = InputEventTimestampNow();
  mouse_report->trace_id = TRACE_NONCE();

  mouse_report->mouse->rel_x = gamepad.left_x;
  mouse_report->mouse->rel_y = gamepad.left_y;
  mouse_report->mouse->pressed_buttons = gamepad.hat_switch;
  return true;
}
bool Hardcoded::ParseParadiseSensorReport(const uint8_t* r, size_t len, uint8_t* sensor_idx,
                                          fuchsia::ui::input::InputReport* sensor_report) {
  if (len != sizeof(paradise_sensor_vector_data_t) &&
      len != sizeof(paradise_sensor_scalar_data_t)) {
    FXL_LOG(INFO) << "paradise sensor data: wrong size " << len << ", expected "
                  << sizeof(paradise_sensor_vector_data_t) << " or "
                  << sizeof(paradise_sensor_scalar_data_t);
    return false;
  }

  sensor_report->event_time = InputEventTimestampNow();
  sensor_report->trace_id = TRACE_NONCE();
  *sensor_idx = r[0];  // We know sensor structs start with sensor ID.
  switch (*sensor_idx) {
    case kParadiseAccLid:
    case kParadiseAccBase: {
      const auto& report = *(reinterpret_cast<const paradise_sensor_vector_data_t*>(r));
      std::array<int16_t, 3> data;
      data[0] = report.vector[0];
      data[1] = report.vector[1];
      data[2] = report.vector[2];
      sensor_report->sensor->set_vector(std::move(data));
    } break;
    case 2:
    case 3:
    case 4:
      // TODO(SCN-626): Expose other sensors.
      return false;
    default:
      FXL_LOG(ERROR) << "paradise sensor unrecognized: " << *sensor_idx;
      return false;
  }

  FXL_VLOG(3) << name() << " parsed (sensor=" << static_cast<uint16_t>(*sensor_idx)
              << "): " << *sensor_report;
  return true;
}

bool Hardcoded::ParseReport(const uint8_t* report, size_t len, HidAmbientLightSimple* data) {
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
    FXL_LOG(ERROR) << "Unexpected count in report from ambient light:" << cur->count;
    return false;
  }
  data->illuminance = extract_uint16(report, cur->begin);
  return true;
}

bool Hardcoded::ParseAmbientLightSensorReport(const uint8_t* report, size_t len,
                                              uint8_t* sensor_idx,
                                              fuchsia::ui::input::InputReport* sensor_report) {
  HidAmbientLightSimple data;
  if (!ParseReport(report, len, &data)) {
    FXL_LOG(ERROR) << " failed reading from ambient light sensor";
    return false;
  }
  sensor_report->sensor->set_scalar(data.illuminance);
  sensor_report->event_time = InputEventTimestampNow();
  sensor_report->trace_id = TRACE_NONCE();
  *sensor_idx = kAmbientLight;

  FXL_VLOG(2) << name() << " parsed (sensor=" << static_cast<uint16_t>(*sensor_idx)
              << "): " << *sensor_report;
  return true;
}

Protocol Hardcoded::MatchProtocol(const std::vector<uint8_t> desc, HidDecoder* hid_decoder) {
  if (is_paradise_sensor_report_desc(desc.data(), desc.size())) {
    return Protocol::ParadiseSensor;
  }
  return Protocol::Other;
}

void Hardcoded::Initialize(Protocol protocol) {
  protocol_ = protocol;

  if (protocol == Protocol::BootMouse || protocol == Protocol::Gamepad) {
    FXL_VLOG(2) << "Device " << name() << " has mouse";
    has_mouse_ = true;
    mouse_device_type_ =
        (protocol == Protocol::BootMouse) ? MouseDeviceType::BOOT : MouseDeviceType::GAMEPAD;

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
  } else if (protocol == Protocol::ParadiseSensor) {
    FXL_VLOG(2) << "Device " << name() << " has motion sensors";
    sensor_device_type_ = SensorDeviceType::PARADISE;
    has_sensors_ = true;

    fuchsia::ui::input::SensorDescriptorPtr acc_base = fuchsia::ui::input::SensorDescriptor::New();
    acc_base->type = fuchsia::ui::input::SensorType::ACCELEROMETER;
    acc_base->loc = fuchsia::ui::input::SensorLocation::BASE;
    sensor_descriptors_[kParadiseAccBase] = std::move(acc_base);

    fuchsia::ui::input::SensorDescriptorPtr acc_lid = fuchsia::ui::input::SensorDescriptor::New();
    acc_lid->type = fuchsia::ui::input::SensorType::ACCELEROMETER;
    acc_lid->loc = fuchsia::ui::input::SensorLocation::LID;
    sensor_descriptors_[kParadiseAccLid] = std::move(acc_lid);

    sensor_report_ = fuchsia::ui::input::InputReport::New();
    sensor_report_->sensor = fuchsia::ui::input::SensorReport::New();
  } else if (protocol == Protocol::LightSensor) {
    FXL_VLOG(2) << "Device " << name() << " has an ambient light sensor";
    sensor_device_type_ = SensorDeviceType::AMBIENT_LIGHT;
    has_sensors_ = true;

    fuchsia::ui::input::SensorDescriptorPtr desc = fuchsia::ui::input::SensorDescriptor::New();
    desc->type = fuchsia::ui::input::SensorType::LIGHTMETER;
    desc->loc = fuchsia::ui::input::SensorLocation::UNKNOWN;
    sensor_descriptors_[kAmbientLight] = std::move(desc);

    sensor_report_ = fuchsia::ui::input::InputReport::New();
    sensor_report_->sensor = fuchsia::ui::input::SensorReport::New();
  }
}

void Hardcoded::NotifyRegistry(fuchsia::ui::input::InputDeviceRegistry* registry) {
  if (has_sensors_) {
    FXL_DCHECK(kMaxSensorCount == sensor_descriptors_.size());
    FXL_DCHECK(kMaxSensorCount == sensor_devices_.size());
    for (size_t i = 0; i < kMaxSensorCount; ++i) {
      if (sensor_descriptors_[i]) {
        fuchsia::ui::input::DeviceDescriptor descriptor;
        zx_status_t status = fidl::Clone(sensor_descriptors_[i], &descriptor.sensor);
        FXL_DCHECK(status == ZX_OK) << "Sensor descriptor: clone failed (status=" << status << ")";
        registry->RegisterDevice(std::move(descriptor), sensor_devices_[i].NewRequest());
      }
    }
    // Sensor devices can't be anything else, so don't bother with other types.
    return;
  }

  // Register the hardcoded device's descriptors.
  {
    fuchsia::ui::input::DeviceDescriptor descriptor;
    if (has_mouse_) {
      fidl::Clone(mouse_descriptor_, &descriptor.mouse);
    }
    if (has_stylus_) {
      fidl::Clone(stylus_descriptor_, &descriptor.stylus);
    }
    if (has_touchscreen_) {
      fidl::Clone(touchscreen_descriptor_, &descriptor.touchscreen);
    }
    registry->RegisterDevice(std::move(descriptor), input_device_.NewRequest());
  }
}

void Hardcoded::Read(const uint8_t* report, int report_len, bool discard) {
  switch (mouse_device_type_) {
    case MouseDeviceType::GAMEPAD:
      // TODO(cpu): remove this once we have a good way to test gamepad.
      if (ParseGamepadMouseReport(report, report_len, mouse_report_.get())) {
        if (!discard) {
          TRACE_FLOW_BEGIN("input", "hid_read_to_listener", mouse_report_->trace_id);
          input_device_->DispatchReport(CloneReport(*mouse_report_));
        }
      }
      break;
    case MouseDeviceType::NONE:
      break;
    default:
      break;
  }

  switch (sensor_device_type_) {
    case SensorDeviceType::PARADISE:
      if (ParseParadiseSensorReport(report, report_len, &sensor_idx_, sensor_report_.get())) {
        if (!discard) {
          FXL_DCHECK(sensor_idx_ < kMaxSensorCount);
          FXL_DCHECK(sensor_devices_[sensor_idx_]);
          TRACE_FLOW_BEGIN("input", "hid_read_to_listener", sensor_report_->trace_id);
          sensor_devices_[sensor_idx_]->DispatchReport(CloneReport(*sensor_report_));
        }
      }
      break;
    case SensorDeviceType::AMBIENT_LIGHT:
      if (ParseAmbientLightSensorReport(report, report_len, &sensor_idx_, sensor_report_.get())) {
        if (!discard) {
          FXL_DCHECK(sensor_idx_ < kMaxSensorCount);
          FXL_DCHECK(sensor_devices_[sensor_idx_]);
          TRACE_FLOW_BEGIN("input", "hid_read_to_listener", sensor_report_->trace_id);
          sensor_devices_[sensor_idx_]->DispatchReport(CloneReport(*sensor_report_));
        }
      }
      break;
    default:
      break;
  }
}

}  // namespace ui_input

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helpers.h"

#include "lib/fxl/logging.h"
#include "logging.h"

namespace bluetoothcli {

std::string AppearanceToString(bluetooth_control::Appearance appearance) {
  switch (appearance) {
    case bluetooth_control::Appearance::UNKNOWN:
      return "(unknown)";
    case bluetooth_control::Appearance::PHONE:
      return "Phone";
    case bluetooth_control::Appearance::COMPUTER:
      return "Computer";
    case bluetooth_control::Appearance::WATCH:
      return "Watch";
    case bluetooth_control::Appearance::WATCH_SPORTS:
      return "Sports Watch";
    case bluetooth_control::Appearance::CLOCK:
      return "Clock";
    case bluetooth_control::Appearance::DISPLAY:
      return "Display";
    case bluetooth_control::Appearance::REMOTE_CONTROL:
      return "Remote Control";
    case bluetooth_control::Appearance::EYE_GLASSES:
      return "Eye Glasses";
    case bluetooth_control::Appearance::TAG:
      return "Tag";
    case bluetooth_control::Appearance::KEYRING:
      return "Keyring";
    case bluetooth_control::Appearance::MEDIA_PLAYER:
      return "Media Player";
    case bluetooth_control::Appearance::BARCODE_SCANNER:
      return "Barcode Scanner";
    case bluetooth_control::Appearance::THERMOMETER:
      return "Thermometer";
    case bluetooth_control::Appearance::THERMOMETER_EAR:
      return "Ear Thermometer";
    case bluetooth_control::Appearance::HEART_RATE_SENSOR:
      return "Heart Rate Sensor";
    case bluetooth_control::Appearance::HEART_RATE_SENSOR_BELT:
      return "Herat Rate Sensor: Belt";
    case bluetooth_control::Appearance::BLOOD_PRESSURE:
      return "Blood Pressure Monitor";
    case bluetooth_control::Appearance::BLOOD_PRESSURE_ARM:
      return "Blood Pressure Monitor: Arm";
    case bluetooth_control::Appearance::BLOOD_PRESSURE_WRIST:
      return "Blood Pressure Monitor: Wrist";
    case bluetooth_control::Appearance::HID:
      return "Human Interface Device (HID)";
    case bluetooth_control::Appearance::HID_KEYBOARD:
      return "Keyboard (HID)";
    case bluetooth_control::Appearance::HID_MOUSE:
      return "Mouse (HID)";
    case bluetooth_control::Appearance::HID_JOYSTICK:
      return "Joystick (HID)";
    case bluetooth_control::Appearance::HID_GAMEPAD:
      return "Gamepad (HID)";
    case bluetooth_control::Appearance::HID_DIGITIZER_TABLET:
      return "Digitizer Tablet (HID)";
    case bluetooth_control::Appearance::HID_CARD_READER:
      return "Card Reader (HID)";
    case bluetooth_control::Appearance::HID_DIGITAL_PEN:
      return "Digital Pen (HID)";
    case bluetooth_control::Appearance::HID_BARCODE_SCANNER:
      return "Barcode Scanner (HID)";
    case bluetooth_control::Appearance::GLUCOSE_METER:
      return "Glucose Meter";
    case bluetooth_control::Appearance::RUNNING_WALKING_SENSOR:
      return "Running/Walking Sensor";
    case bluetooth_control::Appearance::RUNNING_WALKING_SENSOR_IN_SHOE:
      return "Running/Walking Sensor: In Shoe";
    case bluetooth_control::Appearance::RUNNING_WALKING_SENSOR_ON_SHOE:
      return "Running/Walking Sensor: On Shoe";
    case bluetooth_control::Appearance::RUNNING_WALKING_SENSOR_ON_HIP:
      return "Running/Walking Sensor: On Hip";
    case bluetooth_control::Appearance::CYCLING:
      return "Cycling Device";
    case bluetooth_control::Appearance::CYCLING_COMPUTER:
      return "Cycling: Computer";
    case bluetooth_control::Appearance::CYCLING_SPEED_SENSOR:
      return "Cycling: Speed Sensor";
    case bluetooth_control::Appearance::CYCLING_CADENCE_SENSOR:
      return "Cycling: Cadence Sensor";
    case bluetooth_control::Appearance::CYCLING_POWER_SENSOR:
      return "Cycling: Power Sensor";
    case bluetooth_control::Appearance::CYCLING_SPEED_AND_CADENCE_SENSOR:
      return "Cycling: Speed and Cadence Sensor";
    case bluetooth_control::Appearance::PULSE_OXIMETER:
      return "Pulse Oximeter";
    case bluetooth_control::Appearance::PULSE_OXIMETER_FINGERTIP:
      return "Pulse Oximeter: Fingertip";
    case bluetooth_control::Appearance::PULSE_OXIMETER_WRIST:
      return "Pulse Oximeter: Wrist";
    case bluetooth_control::Appearance::WEIGHT_SCALE:
      return "Weight Scale";
    case bluetooth_control::Appearance::PERSONAL_MOBILITY:
      return "Personal Mobility Device";
    case bluetooth_control::Appearance::PERSONAL_MOBILITY_WHEELCHAIR:
      return "Personal Mobility: Wheelchair";
    case bluetooth_control::Appearance::PERSONAL_MOBILITY_SCOOTER:
      return "Personal Mobility: Scooter";
    case bluetooth_control::Appearance::GLUCOSE_MONITOR:
      return "Glucose Monitor";
    case bluetooth_control::Appearance::SPORTS_ACTIVITY:
      return "Sports Activity Device";
    case bluetooth_control::Appearance::SPORTS_ACTIVITY_LOCATION_DISPLAY:
      return "Sports Activity: Location Display";
    case bluetooth_control::Appearance::
        SPORTS_ACTIVITY_LOCATION_AND_NAV_DISPLAY:
      return "Sports Activity: Location and Navigation Display";
    case bluetooth_control::Appearance::SPORTS_ACTIVITY_LOCATION_POD:
      return "Sports Activity: Location Pod";
    case bluetooth_control::Appearance::SPORTS_ACTIVITY_LOCATION_AND_NAV_POD:
      return "Sports Activity: Location and Navigation Pod";
    default:
      break;
  }
  return "UNKNOWN";
}

std::string TechnologyTypeToString(bluetooth_control::TechnologyType type) {
  switch (type) {
    case bluetooth_control::TechnologyType::LOW_ENERGY:
      return "Low Energy";
    case bluetooth_control::TechnologyType::CLASSIC:
      return "Classic (BR/EDR)";
    case bluetooth_control::TechnologyType::DUAL_MODE:
      return "Dual-Mode (BR/EDR/LE)";
  }

  FXL_NOTREACHED();
  return "(unknown)";
}

std::string BoolToString(bool val) {
  return val ? "yes" : "no";
}

std::string ErrorCodeToString(bluetooth::ErrorCode error_code) {
  switch (error_code) {
    case bluetooth::ErrorCode::UNKNOWN:
      return "UNKNOWN";
    case bluetooth::ErrorCode::FAILED:
      return "FAILED";
    case bluetooth::ErrorCode::NOT_FOUND:
      return "NOT_FOUND";
    case bluetooth::ErrorCode::BAD_STATE:
      return "BAD_STATE";
    case bluetooth::ErrorCode::IN_PROGRESS:
      return "IN_PROGRESS";
    case bluetooth::ErrorCode::PROTOCOL_ERROR:
      return "PROTOCOL_ERROR";
    default:
      break;
  }

  return "(unknown)";
}

void PrintAdapterInfo(const bluetooth_control::AdapterInfo& adapter_info,
                      size_t indent) {
  CLI_LOG_INDENT(indent) << "id: " << adapter_info.identifier;
  CLI_LOG_INDENT(indent) << "address: " << adapter_info.address;
  CLI_LOG_INDENT(indent) << "discoverable: "
                         << BoolToString(
                                adapter_info.state->discoverable->value);
}

void PrintRemoteDevice(const bluetooth_control::RemoteDevice& remote_device,
                       size_t indent) {
  CLI_LOG_INDENT(indent) << "id: " << remote_device.identifier;
  CLI_LOG_INDENT(indent) << "address: " << remote_device.address;
  CLI_LOG_INDENT(indent) << "type: "
                         << TechnologyTypeToString(remote_device.technology);
  if (!remote_device.name.get().empty())
    CLI_LOG_INDENT(indent) << "name: " << remote_device.name;
  CLI_LOG_INDENT(indent) << "appearance: "
                         << AppearanceToString(remote_device.appearance);

  if (!remote_device.service_uuids->empty()) {
    CLI_LOG_INDENT(indent) << "services: ";
    for (const auto& service : *remote_device.service_uuids) {
      CLI_LOG_INDENT(indent + 1) << service;
    }
  }
}

}  // namespace bluetoothcli

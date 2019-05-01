// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_HARDCODED_H_
#define GARNET_BIN_UI_INPUT_READER_HARDCODED_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <hid-parser/parser.h>
#include <hid-parser/usages.h>
#include <hid/acer12.h>
#include <hid/paradise.h>

#include <array>
#include <cstddef>

#include "garnet/bin/ui/input_reader/hid_decoder.h"
#include "garnet/bin/ui/input_reader/protocols.h"

namespace ui_input {

class Hardcoded {
 public:
  const std::string& name() const { return name_; }

  // Matches a protocol with a hardcoded HID Report Descriptor |desc|.
  // Returns |Protocol::Other| if there's no match. |hid_decoder| is passed
  // so some setup can be done if it's a special device.
  Protocol MatchProtocol(const std::vector<uint8_t> desc,
                         HidDecoder* hid_decoder);
  void Initialize(Protocol protocol);
  void NotifyRegistry(fuchsia::ui::input::InputDeviceRegistry* registry);
  void Read(const std::vector<uint8_t> report, int report_len, bool discard);

  bool ParseGamepadDescriptor(const hid::ReportField* fields, size_t count);
  bool ParseAmbientLightDescriptor(const hid::ReportField* fields,
                                   size_t count);

 private:
  struct DataLocator {
    uint32_t begin;
    uint32_t count;
    uint32_t match;
  };

  struct HidGamepadSimple {
    int32_t left_x;
    int32_t left_y;
    int32_t right_x;
    int32_t right_y;
    uint32_t hat_switch;
  };

  struct HidAmbientLightSimple {
    int16_t illuminance;
  };

  void ParseKeyboardReport(uint8_t* report, size_t len,
                           fuchsia::ui::input::InputReport* keyboard_report);
  void ParseMouseReport(uint8_t* report, size_t len,
                        fuchsia::ui::input::InputReport* mouse_report);
  bool ParseGamepadMouseReport(uint8_t* report, size_t len,
                               fuchsia::ui::input::InputReport* mouse_report);
  bool ParseAcer12TouchscreenReport(
      uint8_t* report, size_t len,
      fuchsia::ui::input::InputReport* touchscreen_report);
  bool ParseAcer12StylusReport(uint8_t* report, size_t len,
                               fuchsia::ui::input::InputReport* stylus_report);
  bool ParseSamsungTouchscreenReport(
      uint8_t* report, size_t len,
      fuchsia::ui::input::InputReport* touchscreen_report);
  bool ParseEGalaxTouchscreenReport(
      uint8_t* report, size_t len,
      fuchsia::ui::input::InputReport* touchscreen_report);
  bool ParseParadiseStylusReport(
      uint8_t* report, size_t len,
      fuchsia::ui::input::InputReport* stylus_report);
  bool ParseEyoyoTouchscreenReport(
      uint8_t* report, size_t len,
      fuchsia::ui::input::InputReport* touchscreen_report);
  bool ParseFt3x27TouchscreenReport(
      uint8_t* r, size_t len,
      fuchsia::ui::input::InputReport* touchscreen_report);
  bool ParseParadiseTouchscreenReportV1(
      uint8_t* r, size_t len,
      fuchsia::ui::input::InputReport* touchscreen_report);
  bool ParseParadiseTouchscreenReportV2(
      uint8_t* r, size_t len,
      fuchsia::ui::input::InputReport* touchscreen_report);
  bool ParseParadiseTouchpadReportV1(
      uint8_t* r, size_t len, fuchsia::ui::input::InputReport* touchpad_report);
  bool ParseParadiseTouchpadReportV2(
      uint8_t* r, size_t len, fuchsia::ui::input::InputReport* touchpad_report);
  bool ParseParadiseSensorReport(
      uint8_t* report, size_t len, uint8_t* sensor_idx,
      fuchsia::ui::input::InputReport* sensor_report);
  bool ParseAmbientLightSensorReport(
      const uint8_t* report, size_t len, uint8_t* sensor_idx,
      fuchsia::ui::input::InputReport* sensor_report);
  bool ParseReport(const uint8_t* report, size_t len,
                   HidGamepadSimple* gamepad);
  bool ParseReport(const uint8_t* report, size_t len,
                   HidAmbientLightSimple* light);

  template <typename ReportT>
  bool ParseParadiseTouchscreenReport(
      uint8_t* report, size_t len,
      fuchsia::ui::input::InputReport* touchscreen_report);
  template <typename ReportT>
  bool ParseParadiseTouchpadReport(
      uint8_t* report, size_t len,
      fuchsia::ui::input::InputReport* mouse_report);

  std::vector<DataLocator> decoder_;
  acer12_touch_t acer12_touch_reports_[2] = {};
  // Used for converting absolute coords from paradise into relative deltas
  int32_t mouse_abs_x_ = -1;
  int32_t mouse_abs_y_ = -1;

  const std::string name_ = "Hardcoded Device";

  static const uint8_t kMaxSensorCount = 16;
  static const uint8_t kNoSuchSensor = 0xFF;

  bool has_keyboard_ = false;
  fuchsia::ui::input::KeyboardDescriptorPtr keyboard_descriptor_;
  bool has_mouse_ = false;
  fuchsia::ui::input::MouseDescriptorPtr mouse_descriptor_;
  bool has_stylus_ = false;
  fuchsia::ui::input::StylusDescriptorPtr stylus_descriptor_;
  bool has_touchscreen_ = false;
  fuchsia::ui::input::TouchscreenDescriptorPtr touchscreen_descriptor_;
  bool has_sensors_ = false;
  // Arrays are indexed by the sensor number that was assigned by Zircon.
  // Keeps track of the physical sensors multiplexed over the file descriptor.
  std::array<fuchsia::ui::input::SensorDescriptorPtr, kMaxSensorCount>
      sensor_descriptors_;
  std::array<fuchsia::ui::input::InputDevicePtr, kMaxSensorCount>
      sensor_devices_;

  TouchDeviceType touch_device_type_ = TouchDeviceType::NONE;
  MouseDeviceType mouse_device_type_ = MouseDeviceType::NONE;
  SensorDeviceType sensor_device_type_ = SensorDeviceType::NONE;

  // Keep track of which sensor gave us a report. Index into
  // |sensor_descriptors_| and |sensor_devices_|.
  uint8_t sensor_idx_ = kNoSuchSensor;

  fuchsia::ui::input::InputReportPtr keyboard_report_;
  fuchsia::ui::input::InputReportPtr mouse_report_;
  fuchsia::ui::input::InputReportPtr touchscreen_report_;
  fuchsia::ui::input::InputReportPtr stylus_report_;
  fuchsia::ui::input::InputReportPtr sensor_report_;

  fuchsia::ui::input::InputDevicePtr input_device_;

  Protocol protocol_;
};
}  // namespace ui_input

#endif  // GARNET_BIN_UI_INPUT_READER_HARDCODED_H_

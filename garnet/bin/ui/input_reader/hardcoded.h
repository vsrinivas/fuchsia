// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_HARDCODED_H_
#define GARNET_BIN_UI_INPUT_READER_HARDCODED_H_

#include <cstddef>

#include <fuchsia/ui/input/cpp/fidl.h>

#include <hid-parser/parser.h>
#include <hid-parser/usages.h>
#include <hid/acer12.h>
#include <hid/paradise.h>

namespace mozart {

class Hardcoded {
 public:
  const std::string& name() const { return name_; }

  bool ParseGamepadDescriptor(const hid::ReportField* fields, size_t count);
  bool ParseButtonsDescriptor(const hid::ReportField* fields, size_t count);
  bool ParseAmbientLightDescriptor(const hid::ReportField* fields,
                                   size_t count);

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
  bool ParseButtonsReport(const uint8_t* report, size_t len,
                          fuchsia::ui::input::InputReport* buttons_report);
  bool ParseParadiseSensorReport(
      uint8_t* report, size_t len, uint8_t* sensor_idx,
      fuchsia::ui::input::InputReport* sensor_report);
  bool ParseAmbientLightSensorReport(
      const uint8_t* report, size_t len, uint8_t* sensor_idx,
      fuchsia::ui::input::InputReport* sensor_report);

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

  struct HidButtons {
    int8_t volume;
    bool mic_mute;
  };

  struct HidAmbientLightSimple {
    int16_t illuminance;
  };

  bool ParseReport(const uint8_t* report, size_t len,
                   HidGamepadSimple* gamepad);
  bool ParseReport(const uint8_t* report, size_t len, HidButtons* data);
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
};
}  // namespace mozart

#endif  // GARNET_BIN_UI_INPUT_READER_HARDCODED_H_

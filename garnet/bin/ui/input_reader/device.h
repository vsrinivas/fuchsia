// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_DEVICE_H_
#define GARNET_BIN_UI_INPUT_READER_DEVICE_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <hid-parser/parser.h>

#include "garnet/bin/ui/input_reader/protocols.h"

namespace ui_input {

// This class represents a single HID input device. The purpose of a Device
// is to parse raw report bytes into an InputReport form. The report bytes
// are interpreted by the Report descriptor which is read at the initialization
// of the device.
class Device {
 public:
  struct Descriptor {
    Protocol protocol;

    bool has_keyboard = false;
    fuchsia::ui::input::KeyboardDescriptorPtr keyboard_descriptor;

    bool has_media_buttons = false;
    fuchsia::ui::input::MediaButtonsDescriptorPtr buttons_descriptor;

    bool has_mouse = false;
    MouseDeviceType mouse_type;
    fuchsia::ui::input::MouseDescriptorPtr mouse_descriptor;

    bool has_stylus = false;
    fuchsia::ui::input::StylusDescriptorPtr stylus_descriptor;

    bool has_touchscreen = false;
    TouchDeviceType touch_type;
    fuchsia::ui::input::TouchscreenDescriptorPtr touchscreen_descriptor;

    bool has_sensor = false;
    SensorDeviceType sensor_type;
    int sensor_id;
    fuchsia::ui::input::SensorDescriptorPtr sensor_descriptor;
  };

  virtual ~Device() = default;
  // This needs to be called to initialize the device.
  // Returns false if the report descriptor does not match the given device.
  virtual bool ParseReportDescriptor(
      const hid::ReportDescriptor& report_descriptor,
      Descriptor* device_descriptor) = 0;

  // This parses |data| which is the given raw report, into |report|.
  // Returns false if the report does not match the given device.
  virtual bool ParseReport(const uint8_t* data, size_t len,
                           fuchsia::ui::input::InputReport* report) = 0;

  // Returns the one byte ReportId identifier of this device. This ReportId
  // is parsed out of the Report Descriptor.
  virtual uint8_t ReportId() const = 0;
};

}  // namespace ui_input

#endif  // GARNET_BIN_UI_INPUT_READER_DEVICE_H_

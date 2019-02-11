// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_INPUT_INTERPRETER_H_
#define GARNET_BIN_UI_INPUT_READER_INPUT_INTERPRETER_H_

#include <hid/acer12.h>
#include <lib/zx/event.h>
#include <zircon/types.h>

#include <array>
#include <string>
#include <vector>

#include "garnet/bin/ui/input_reader/hid_decoder.h"
#include "garnet/bin/ui/input_reader/protocols.h"

#include <fuchsia/ui/input/cpp/fidl.h>

namespace mozart {

// Each InputInterpreter instance observes and routes events coming in from one
// file descriptor under /dev/class/input. Each file descriptor may multiplex
// events from one or more physical devices, though typically there is a 1:1
// correspondence for input devices like keyboards and mice. Sensors are an
// atypical case, where many sensors have their events routed through one
// logical file descriptor, since they share a hardware FIFO queue.

class InputInterpreter {
 public:
  enum ReportType {
    kKeyboard,
    kMouse,
    kStylus,
    kTouchscreen,
  };

  static std::unique_ptr<InputInterpreter> Open(
      int dirfd, std::string filename,
      fuchsia::ui::input::InputDeviceRegistry* registry);

  InputInterpreter(std::unique_ptr<HidDecoder> hid_decoder,
                   fuchsia::ui::input::InputDeviceRegistry* registry);
  ~InputInterpreter();

  bool Initialize();
  bool Read(bool discard);

  const std::string& name() const { return hid_decoder_->name(); }
  zx_handle_t handle() { return event_.get(); }

 private:
  static const uint8_t kMaxSensorCount = 16;
  static const uint8_t kNoSuchSensor = 0xFF;

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

  struct HidButtons {
    int8_t volume;
    bool mic_mute;
  };

  struct DataLocator {
    uint32_t begin;
    uint32_t count;
    uint32_t match;
  };

  // Helper function called during Init() that determines which protocol
  // is going to be used. If it returns true then |protocol_| has been
  // set correctly.
  bool ParseProtocol();

  bool ParseReport(const uint8_t* report, size_t len,
                   HidGamepadSimple* gamepad);
  bool ParseReport(const uint8_t* report, size_t len,
                   HidAmbientLightSimple* light);
  bool ParseReport(const uint8_t* report, size_t len, HidButtons* data);
  bool ParseReport(const uint8_t* report, size_t len,
                   Touchscreen::Report* touchscreen);
  bool ParseReport(const uint8_t* report, size_t len, Mouse::Report* mouse);

  bool SetDescriptor(Touchscreen::Descriptor* touch_desc);

  bool ParseGamepadDescriptor(const hid::ReportField* fields, size_t count);
  bool ParseAmbientLightDescriptor(const hid::ReportField* fields,
                                   size_t count);
  bool ParseButtonsDescriptor(const hid::ReportField* fields, size_t count);

  void NotifyRegistry();

  void ParseKeyboardReport(uint8_t* report, size_t len,
                           fuchsia::ui::input::InputReport* keyboard_report);
  void ParseMouseReport(uint8_t* report, size_t len,
                        fuchsia::ui::input::InputReport* mouse_report);
  bool ParseHidMouseReport(uint8_t* report, size_t len,
                           fuchsia::ui::input::InputReport* mouse_report);
  bool ParseGamepadMouseReport(uint8_t* report, size_t len,
                               fuchsia::ui::input::InputReport* mouse_report);
  bool ParseTouchscreenReport(
      uint8_t* report, size_t len,
      fuchsia::ui::input::InputReport* touchscreen_report);
  bool ParseTouchpadReport(uint8_t* report, size_t len,
                           fuchsia::ui::input::InputReport* mouse_report);
  bool ParseAcer12TouchscreenReport(
      uint8_t* report, size_t len,
      fuchsia::ui::input::InputReport* touchscreen_report);
  bool ParseAcer12StylusReport(uint8_t* report, size_t len,
                               fuchsia::ui::input::InputReport* stylus_report);
  bool ParseSamsungTouchscreenReport(
      uint8_t* report, size_t len,
      fuchsia::ui::input::InputReport* touchscreen_report);
  template <typename ReportT>
  bool ParseParadiseTouchscreenReport(
      uint8_t* report, size_t len,
      fuchsia::ui::input::InputReport* touchscreen_report);
  bool ParseEGalaxTouchscreenReport(
      uint8_t* report, size_t len,
      fuchsia::ui::input::InputReport* touchscreen_report);
  template <typename ReportT>
  bool ParseParadiseTouchpadReport(
      uint8_t* report, size_t len,
      fuchsia::ui::input::InputReport* mouse_report);
  bool ParseParadiseSensorReport(
      uint8_t* report, size_t len, uint8_t* sensor_idx,
      fuchsia::ui::input::InputReport* sensor_report);
  bool ParseParadiseStylusReport(
      uint8_t* report, size_t len,
      fuchsia::ui::input::InputReport* stylus_report);
  bool ParseEyoyoTouchscreenReport(
      uint8_t* report, size_t len,
      fuchsia::ui::input::InputReport* touchscreen_report);
  bool ParseFt3x27TouchscreenReport(
      uint8_t* r, size_t len,
      fuchsia::ui::input::InputReport* touchscreen_report);

  bool ParseAmbientLightSensorReport(
      const uint8_t* report, size_t len, uint8_t* sensor_idx,
      fuchsia::ui::input::InputReport* sensor_report);
  bool ParseButtonsReport(const uint8_t* report, size_t len,
                          fuchsia::ui::input::InputReport* buttons_report);

  fuchsia::ui::input::InputDeviceRegistry* registry_;

  zx::event event_;

  acer12_touch_t acer12_touch_reports_[2];

  bool has_keyboard_ = false;
  fuchsia::ui::input::KeyboardDescriptorPtr keyboard_descriptor_;
  bool has_buttons_ = false;
  fuchsia::ui::input::ButtonsDescriptorPtr buttons_descriptor_;
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

  // Global variables necessary to do conversion from touchpad information
  // into mouse information. All information is from the previous seen report,
  // which enables us to do relative deltas and finger tracking.

  // True if any fingers are pressed on the touchpad.
  bool has_touch_ = false;
  // True if the tracking finger is no longer pressed, but other fingers are
  // still pressed.
  bool tracking_finger_was_lifted_ = false;
  // Used to keep track of which finger is controlling the mouse on a touchpad
  uint32_t tracking_finger_id_;
  // Used for converting absolute coords from paradise into relative deltas
  int32_t mouse_abs_x_ = -1;
  int32_t mouse_abs_y_ = -1;

  // Keep track of which sensor gave us a report. Index into
  // |sensor_descriptors_| and |sensor_devices_|.
  uint8_t sensor_idx_ = kNoSuchSensor;

  fuchsia::ui::input::InputReportPtr keyboard_report_;
  fuchsia::ui::input::InputReportPtr mouse_report_;
  fuchsia::ui::input::InputReportPtr touchscreen_report_;
  fuchsia::ui::input::InputReportPtr stylus_report_;
  fuchsia::ui::input::InputReportPtr sensor_report_;
  fuchsia::ui::input::InputReportPtr buttons_report_;

  fuchsia::ui::input::InputDevicePtr input_device_;

  std::unique_ptr<HidDecoder> hid_decoder_;

  Protocol protocol_;
  std::vector<DataLocator> decoder_;
  Touchscreen ts_;
  Mouse mouse_;
};

}  // namespace mozart

#endif  // GARNET_BIN_UI_INPUT_READER_INPUT_INTERPRETER_H_

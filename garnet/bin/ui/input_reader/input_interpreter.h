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

#include "garnet/bin/ui/input_reader/hardcoded.h"
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

  // Helper function called at the end of ParseProtocol. It will set
  // InputInterpreter's |procotol_| and various device descriptors based
  // on |descriptor|. It will move the DescriptorPtrs out of |descriptor|, so
  // |descriptor| should not be used after calling this function.
  bool ConsumeDescriptor(Device::Descriptor* descriptor);

  // Helper function called during Init() that determines which protocol
  // is going to be used. If it returns true then |protocol_| has been
  // set correctly.
  bool ParseProtocol();

  bool ParseReport(const uint8_t* report, size_t len,
                   Touch::Report* touchscreen);
  bool SetDescriptor(Touch::Descriptor* touch_desc);

  void NotifyRegistry();

  bool ParseTouchscreenReport(
      uint8_t* report, size_t len,
      fuchsia::ui::input::InputReport* touchscreen_report);
  bool ParseTouchpadReport(uint8_t* report, size_t len,
                           fuchsia::ui::input::InputReport* mouse_report);

  fuchsia::ui::input::InputDeviceRegistry* registry_;

  zx::event event_;

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
  Touch ts_;
  Mouse mouse_;
  Hardcoded hardcoded_;
};

}  // namespace mozart

#endif  // GARNET_BIN_UI_INPUT_READER_INPUT_INTERPRETER_H_

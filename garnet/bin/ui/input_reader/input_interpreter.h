// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_INPUT_INTERPRETER_H_
#define GARNET_BIN_UI_INPUT_READER_INPUT_INTERPRETER_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <hid/acer12.h>
#include <lib/zx/event.h>
#include <zircon/types.h>

#include <array>
#include <string>
#include <vector>

#include "garnet/bin/ui/input_reader/buttons.h"
#include "garnet/bin/ui/input_reader/hardcoded.h"
#include "garnet/bin/ui/input_reader/hid_decoder.h"
#include "garnet/bin/ui/input_reader/pointer.h"
#include "garnet/bin/ui/input_reader/protocols.h"
#include "garnet/bin/ui/input_reader/sensor.h"
#include "garnet/bin/ui/input_reader/stylus.h"
#include "garnet/bin/ui/input_reader/touchpad.h"
#include "garnet/bin/ui/input_reader/touchscreen.h"

namespace ui_input {

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
  // Each InputDevice represents a logical device exposed by a HID device.
  // Some HID devices have multiple InputDevices (e.g: A keyboard/mouse
  // combo with a single USB cable).
  struct InputDevice {
    // The Device struct that parses the reports.
    std::unique_ptr<Device> device;
    // The structured report that is parsed by |device|.
    fuchsia::ui::input::InputReportPtr report;
    // Holds descriptions of what this device contains.
    Device::Descriptor descriptor;
    // The pointer where reports are sent to by this device.
    fuchsia::ui::input::InputDevicePtr input_device;
  };

  Protocol ExtractProtocol(hid::Usage input);

  // This function takes a ReportDescriptor that describes an input report.
  // It will use the report descriptor to create a matching InputDevice. If it
  // returns true then the |devices_| array will have been extended by one extra
  // |InputDevice|.
  bool ParseHidInputReportDescriptor(const hid::ReportDescriptor* input_desc);

  // Takes in a ReportDescriptor representing a feature report and sends it
  // to a the device. Should be called on each feature report descriptor in
  // order to initialize the device. Returns true if the report descriptor
  // doesn't match, or if it matches and successfully initializes the device.
  // Only returns false if there is an error.
  bool ParseHidFeatureReportDescriptor(
      const hid::ReportDescriptor& report_desc);

  // Helper function called during Init() that determines which protocol
  // is going to be used. It is responsible for reading the HID device's
  // Report Descriptor and determining what type of device it is.
  // If it returns true then either |protocol_| will have been set
  // (if the device is a hardcoded device), or |devices_| array will contain
  // the full list of generic devices the HID report descriptor describes.
  bool ParseProtocol();

  void NotifyRegistry();

  fuchsia::ui::input::InputDeviceRegistry* registry_;

  zx::event event_;

  // The array of Devices that are managed by this InputInterpreter.
  std::vector<InputDevice> devices_;

  std::unique_ptr<HidDecoder> hid_decoder_;
  Protocol protocol_;
  Hardcoded hardcoded_ = {};
};

}  // namespace ui_input

#endif  // GARNET_BIN_UI_INPUT_READER_INPUT_INTERPRETER_H_

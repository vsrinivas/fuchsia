// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_TEST_UTILS_BUTTON_CHECKER_H_
#define SRC_CAMERA_DRIVERS_TEST_UTILS_BUTTON_CHECKER_H_

#include <fuchsia/hardware/input/cpp/fidl.h>
#include <zircon/types.h>

#include <memory>
#include <utility>
#include <vector>

#include <hid-parser/report.h>
#include <hid-parser/usages.h>

// This class connects to input devices and allows for queries to input buttons or switches.
// Currently, this class only supports checking the state of the mute switch.
class ButtonChecker {
 public:
  enum class ButtonState {
    UNKNOWN,  // Button state could not be determined or is undefined.
    DOWN,     // Button is pressed, on, or active.
    UP,       // Button is not pressed, off, or inactive.
  };

  // Creates a new ButtonChecker instance. Returns nullptr on failure.
  static std::unique_ptr<ButtonChecker> Create();

  // Gets the state of the Mute button/switch, if available.
  ButtonState GetMuteState();

 private:
  // Create a device proxy on the provided filesystem path. Returns an unbound proxy on failure.
  static fuchsia::hardware::input::DeviceSyncPtr BindDevice(const std::string& path);

  // Populates a report field for a mute button, if present, on the device. Returns false iff the
  // field was successfully populated.
  static bool GetMuteFieldForDevice(fuchsia::hardware::input::DeviceSyncPtr& device,
                                    hid::ReportField* mute_field_out);
  std::vector<std::pair<fuchsia::hardware::input::DeviceSyncPtr, hid::ReportField>> devices_;
};

// Convenience wrapper to check the mute state of a device. Returns true if the device is confirmed
// to be unmuted. If the device is muted or its mute state could not be determined, a warning is
// printed to stderr.
bool VerifyDeviceUnmuted(bool consider_unknown_as_unmuted = false);

#endif  // SRC_CAMERA_DRIVERS_TEST_UTILS_BUTTON_CHECKER_H_

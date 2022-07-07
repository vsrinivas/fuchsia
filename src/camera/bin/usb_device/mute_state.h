// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_USB_DEVICE_MUTE_STATE_H_
#define SRC_CAMERA_BIN_USB_DEVICE_MUTE_STATE_H_

namespace camera {

// Represents the mute state of a device as defined by the Device.WatchMuteState API.
struct MuteState {
  bool software_muted = false;
  bool hardware_muted = false;
  // Returns true iff any mute is active.
  bool muted() const { return software_muted || hardware_muted; }
  bool operator==(const MuteState& other) const {
    return other.software_muted == software_muted && other.hardware_muted == hardware_muted;
  }
};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_USB_DEVICE_MUTE_STATE_H_

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_EXAMPLES_BT_BEACON_READER_BEACONS_H_
#define SRC_CONNECTIVITY_BLUETOOTH_EXAMPLES_BT_BEACON_READER_BEACONS_H_

#include <fuchsia/bluetooth/le/cpp/fidl.h>

#include <memory>

namespace bt_beacon_reader {

// Represents the detection of an iBeacon
class IBeaconDetection {
 public:
  // Examine a BLE detection and determine if it is a beacon.
  // If it is a beacon, fills out this class and returns it.
  // Otherwise, returns a nullptr.
  static std::unique_ptr<IBeaconDetection> Create(const fuchsia::bluetooth::le::Peer& device);

  uint8_t power_lvl_ = 0;
  std::string uuid_;
  uint16_t major_, minor_;

 private:
  // Reads in the manufacturing data from a BLE detection
  // to fill out the class.
  void Read(const std::vector<uint8_t>& data);
};

// Represents the detection of a Tilt Hydrometer beacon
class TiltDetection {
 public:
  static std::unique_ptr<TiltDetection> Create(const fuchsia::bluetooth::le::Peer& peer);

  void Print();

 private:
  // Examine a BLE detection and determine if it is a beacon.
  // If it is a beacon, fills out this class and returns true.
  void Read(const std::unique_ptr<IBeaconDetection>& beacon);

  uint16_t temperature_F_;
  float gravity_;
  uint8_t color_;
  std::string color_string_;
};

}  // namespace bt_beacon_reader

#endif  // SRC_CONNECTIVITY_BLUETOOTH_EXAMPLES_BT_BEACON_READER_BEACONS_H_

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "beacons.h"

#include <stdio.h>  // snprintf

namespace ble = fuchsia::bluetooth::le;

namespace bt_beacon_reader {

std::unique_ptr<IBeaconDetection> IBeaconDetection::Create(const ble::Peer& peer) {
  if (!peer.has_data() || !peer.data().has_manufacturer_data() ||
      peer.data().manufacturer_data().size() != 1) {
    return nullptr;
  }
  const std::vector<uint8_t>& data = peer.data().manufacturer_data()[0].data;
  if (data.size() < 21) {
    return nullptr;
  }
  if (data[0] != 0x02) {
    return nullptr;
  }
  if (data[1] != data.size() - 2) {
    return nullptr;
  }
  std::unique_ptr<IBeaconDetection> beacon(new IBeaconDetection);
  beacon->Read(data);
  return beacon;
}

void IBeaconDetection::Read(const std::vector<uint8_t>& data) {
  char temp[3];
  power_lvl_ = 0;
  uuid_.clear();
  for (int i = 2; i < 18; ++i) {
    snprintf(temp, 3, "%02x", data[i]);
    uuid_.append(temp, 2);
  }
  major_ = static_cast<uint16_t>((data[18] << 8) + data[19]);
  minor_ = static_cast<uint16_t>((data[20] << 8) + data[21]);
  // The power level field seems to be optional...
  if (data.size() > 22) {
    power_lvl_ = data[22];
  }
}

std::unique_ptr<TiltDetection> TiltDetection::Create(const ble::Peer& peer) {
  std::unique_ptr<IBeaconDetection> beacon = IBeaconDetection::Create(peer);
  if (!beacon) {
    return nullptr;
  }

  // All Tilt Hydrometers have uuids of the form:
  // a495bb-0c5b14b44b5121370f02d74de where the dash is 0-8,
  // corresponding to the 8 colors.
  if (beacon->uuid_.compare(0, 6, "a495bb") ||
      beacon->uuid_.compare(8, 24, "c5b14b44b5121370f02d74de")) {
    return nullptr;
  }
  // OK, it is a tilt!
  std::unique_ptr<TiltDetection> tilt(new TiltDetection);
  tilt->Read(beacon);
  return tilt;
}

void TiltDetection::Print() {
  printf("Tilt %s: Temp: %dF, Gravity: %1.3f\n", color_string_.c_str(), temperature_F_, gravity_);
}

void TiltDetection::Read(const std::unique_ptr<IBeaconDetection>& beacon) {
  temperature_F_ = beacon->major_;
  color_ = beacon->uuid_[6] - '0';
  std::string colors[] = {"Invalid", "Red",  "Green",  "Black", "Purple",
                          "Orange",  "Blue", "Yellow", "Pink"};
  color_string_ = colors[color_ % 9];

  // negative gravities are just expressed as their value % 1000.
  // Since a specific gravity of beer at 1.5 is unreasonable, we'll draw the line
  // there.
  gravity_ = beacon->minor_ / 1000.0f;
  if (beacon->minor_ < 500) {
    gravity_ += 1.0f;
  }
}

}  // namespace bt_beacon_reader

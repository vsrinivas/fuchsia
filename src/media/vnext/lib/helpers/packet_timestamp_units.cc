// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/helpers/packet_timestamp_units.h"

namespace fmlib {

//  static
std::unique_ptr<PacketTimestampUnits> PacketTimestampUnits::Create(
    const fuchsia::media2::PacketTimestampUnits* timestamp_units) {
  if (!timestamp_units) {
    return nullptr;
  }

  return std::make_unique<PacketTimestampUnits>(*timestamp_units);
}

PacketTimestampUnits::PacketTimestampUnits(int64_t packet_timestamp_interval,
                                           zx::duration presentation_interval)
    : packet_timestamp_interval_(packet_timestamp_interval),
      presentation_interval_(presentation_interval) {}

PacketTimestampUnits::PacketTimestampUnits(
    const fuchsia::media2::PacketTimestampUnits& timestamp_units)
    : packet_timestamp_interval_(timestamp_units.packet_timestamp_interval),
      presentation_interval_(zx::duration(timestamp_units.presentation_interval)) {}

fuchsia::media2::PacketTimestampUnits PacketTimestampUnits::fidl() const {
  return fuchsia::media2::PacketTimestampUnits{
      .packet_timestamp_interval = packet_timestamp_interval_,
      .presentation_interval = presentation_interval_.get()};
}

fuchsia::media2::PacketTimestampUnitsPtr PacketTimestampUnits::fidl_ptr() const {
  if (!is_valid()) {
    return nullptr;
  }

  auto result = fuchsia::media2::PacketTimestampUnits::New();
  result->packet_timestamp_interval = packet_timestamp_interval_;
  result->presentation_interval = presentation_interval_.get();
  return result;
}

PacketTimestampUnits::operator fuchsia::media2::PacketTimestampUnits() const {
  return fuchsia::media2::PacketTimestampUnits{
      .packet_timestamp_interval = packet_timestamp_interval_,
      .presentation_interval = presentation_interval_.get()};
}

int64_t PacketTimestampUnits::ToTimestamp(zx::duration presentation_time) const {
  // TODO(dalesat): Should use 128-bit math.
  return (presentation_time.get() * packet_timestamp_interval_) / presentation_interval_.get();
}

zx::duration PacketTimestampUnits::ToPresentationTime(int64_t timestamp) const {
  // TODO(dalesat): Should use 128-bit math.
  return zx::duration((timestamp * presentation_interval_.get()) / packet_timestamp_interval_);
}

}  // namespace fmlib

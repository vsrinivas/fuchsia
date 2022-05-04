// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_HELPERS_PACKET_TIMESTAMP_UNITS_H_
#define SRC_MEDIA_VNEXT_LIB_HELPERS_PACKET_TIMESTAMP_UNITS_H_

#include <fuchsia/media2/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

namespace fmlib {

// Smarter version of |fuchsia::media2::PacketTimestampUnits|.
class PacketTimestampUnits {
 public:
  // Creates a unique pointer to an instance of |PacketTimestampUnits| from |*timestamps| if
  // |timestamps| is not null, a null unique pointer otherwise.
  static std::unique_ptr<PacketTimestampUnits> Create(
      const fuchsia::media2::PacketTimestampUnits* timestamp_units);

  // Constructs an invalid |PacketTimestampUnits| instance with zero timestamp and presentation
  // values.
  PacketTimestampUnits() = default;

  // Constructs a |PacketTimestampUnits| instance with the given values.
  PacketTimestampUnits(int64_t packet_timestamp_interval, zx::duration presentation_interval);

  // Constructs a |PacketTimestampUnits| from a |fuchsia::media2::PacketTimestampUnits|.
  explicit PacketTimestampUnits(const fuchsia::media2::PacketTimestampUnits& timestamp_units);

  // Determines whether both values of this instance are zero.
  bool is_valid() const {
    return packet_timestamp_interval_ != 0 && presentation_interval_ != zx::sec(0);
  }

  // Determines whether both values of this instance are zero.
  explicit operator bool() const { return is_valid(); }

  // Returns the packet timestamp value interval corresponding to |presentation_interval()|.
  int64_t packet_timestamp_interval() const { return packet_timestamp_interval_; }

  // Returns the presentation time interval corresponding to |packet_timestamp_interval()|.
  zx::duration presentation_interval() const { return presentation_interval_; }

  // Returns an equivalent |fuchsia::media2::PacketTimestampUnits|.
  fuchsia::media2::PacketTimestampUnits fidl() const;

  // Returns an equivalent |fuchsia::media2::PacketTimestampUnits|.
  explicit operator fuchsia::media2::PacketTimestampUnits() const;

  // Returns a unique pointer to an equivalent |fuchsia::media2::PacketTimestampUnits|. Never
  // returns null.
  fuchsia::media2::PacketTimestampUnitsPtr fidl_ptr() const;

  // Converts a presentation time to a timestamp.
  int64_t ToTimestamp(zx::duration presentation_time) const;

  // Converts a timestamp to a presentation time.
  zx::duration ToPresentationTime(int64_t timestamp) const;

 private:
  int64_t packet_timestamp_interval_ = 0;
  zx::duration presentation_interval_;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_HELPERS_PACKET_TIMESTAMP_UNITS_H_

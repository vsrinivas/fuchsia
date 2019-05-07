// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_TIMESTAMP_EXTRAPOLATOR_H_
#define GARNET_BIN_MEDIA_CODECS_TIMESTAMP_EXTRAPOLATOR_H_

#include <lib/zx/time.h>

#include <map>
#include <optional>
#include <ostream>
#include <variant>

// `TimestampExtrapolator` extrapolates timestamp-like values for a given
// timebase for forward offsets in a stream of data.
class TimestampExtrapolator {
 public:
  // Creates a `TimestampExtrapolator` where `timebase` is the number of
  // ticks per second of real time and `byte_duration` is the amount of real
  // time consumed by a single byte of the uncompressed input.
  //
  // For example, with PCM audio at 48000Hz, the bytes per second is
  //
  //    `48000 * number_of_channels * bytes_per_sample`
  TimestampExtrapolator(uint64_t timebase, uint64_t bytes_per_second);

  // Creates a TimestampExtrapolator that can only carry over timestamps it has
  // been informed of.
  TimestampExtrapolator();

  // Informs the extrapolator with an input `timestamp`, where `offset` is the
  // index of the byte in the uncompressed stream to which the `timestamp`
  // corresponds. This replaces any previously informed timestamp.
  void Inform(size_t offset, uint64_t timestamp);

  // Given a novel `offset` >= the the offset of the last informed timestamp,
  // extrapolate a timestamp value. This consumes the most recently informed
  // timestamp, leaving the extrapolator without a timestamp until another is
  // provided by `Inform()`.
  //
  // Returns std::nullopt if there is no informed timestamp or no timebase for
  // extrapolation. Asserts that `offset` >= the offset of the last informed
  // timestamp.
  std::optional<uint64_t> Extrapolate(size_t offset);

  bool has_information() const { return last_information_.has_value(); };

 private:
  struct IndexedTimestamp {
    size_t offset;
    uint64_t timestamp;
  };

  std::optional<uint64_t> timebase_;
  uint64_t bytes_per_second_ = 0;
  std::optional<IndexedTimestamp> last_information_;
};

#endif  // GARNET_BIN_MEDIA_CODECS_TIMESTAMP_EXTRAPOLATOR_H_

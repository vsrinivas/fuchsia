// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "timestamp_extrapolator.h"

#include <zircon/assert.h>

TimestampExtrapolator::TimestampExtrapolator(uint64_t timebase,
                                             uint64_t bytes_per_second)
    : timebase_(timebase), bytes_per_second_(bytes_per_second) {}

TimestampExtrapolator::TimestampExtrapolator() {}

void TimestampExtrapolator::Inform(size_t offset, uint64_t timestamp) {
  last_information_ = {.offset = offset, .timestamp = timestamp};
}

std::optional<uint64_t> TimestampExtrapolator::Extrapolate(size_t offset) {
  if (!last_information_) {
    return std::nullopt;
  }

  auto last_information = *last_information_;
  last_information_.reset();

  ZX_DEBUG_ASSERT_MSG(last_information.offset <= offset,
                      "offset %lu behind last informed timestamp's offset %lu",
                      offset, last_information.offset);

  if (!timebase_) {
    if (offset == last_information_->offset) {
      return last_information.timestamp;
    } else {
      return std::nullopt;
    }
  }

  uint64_t delta = offset - last_information.offset;
  return {last_information.timestamp +
          delta * (*timebase_) / bytes_per_second_};
}

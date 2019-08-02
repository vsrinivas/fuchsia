// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_METRIC_BROKER_CONFIG_COBALT_EVENT_CODES_H_
#define GARNET_BIN_METRIC_BROKER_CONFIG_COBALT_EVENT_CODES_H_

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

namespace broker_service::cobalt {

// This is defined as part of the cobalt API.
constexpr uint64_t kMaxDimensionsPerEvent = 5;

// Convenient wrapper and type alias for dealing with event codes.
// In cobalt event codes are order based, meaning for each metric, the
// possible list of associated event codes is index based.
struct EventCodes {
  using CodeType = std::optional<uint32_t>;
  using CodeEntry = std::pair<uint32_t, CodeType>;

  EventCodes() = default;
  // Sparse constructor.
  EventCodes(CodeEntry* entries, uint64_t num_entries);

  std::array<std::optional<uint32_t>, kMaxDimensionsPerEvent> codes = {};
};

}  // namespace broker_service::cobalt

#endif  // GARNET_BIN_METRIC_BROKER_CONFIG_COBALT_EVENT_CODES_H_

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_UTILS_COBALT_EVENT_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_UTILS_COBALT_EVENT_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/fit/function.h>

#include <ostream>

namespace feedback {

struct CobaltEvent {
  // Represents a Cobalt event.
  //
  // Only supports a single-dimension occurrence and count events.
  enum class Type {
    Occurrence,
    Count,
  };

  CobaltEvent(
      Type type, uint32_t metric_id, uint32_t event_code,
      fit::callback<void(fuchsia::cobalt::Status)> callback = [](fuchsia::cobalt::Status) {})
      : CobaltEvent(type, metric_id, event_code, /*count=*/0u, std::move(callback)) {}

  CobaltEvent(
      Type type, uint32_t metric_id, uint32_t event_code, uint64_t count,
      fit::callback<void(fuchsia::cobalt::Status)> callback = [](fuchsia::cobalt::Status) {})
      : type(type),
        metric_id(metric_id),
        event_code(event_code),
        count(count),
        callback(std::move(callback)) {}

  // Make this object move only
  CobaltEvent(const CobaltEvent& other) = delete;
  CobaltEvent& operator=(const CobaltEvent& other) = delete;
  CobaltEvent(CobaltEvent&& other) = default;
  CobaltEvent& operator=(CobaltEvent&& other) = default;

  std::string ToString() const;

  Type type;
  uint32_t metric_id = 0;
  uint32_t event_code = 0;
  uint64_t count = 0;
  fit::callback<void(fuchsia::cobalt::Status)> callback;
};

bool operator==(const CobaltEvent& lhs, const CobaltEvent& rhs);
std::ostream& operator<<(std::ostream& os, const CobaltEvent& event);

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_UTILS_COBALT_EVENT_H_

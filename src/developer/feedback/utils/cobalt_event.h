// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_UTILS_COBALT_EVENT_H_
#define SRC_DEVELOPER_FEEDBACK_UTILS_COBALT_EVENT_H_

#include <fuchsia/cobalt/cpp/fidl.h>

#include <ostream>
#include <type_traits>

#include "src/developer/feedback/utils/cobalt_metrics.h"

namespace feedback {

struct CobaltEvent {
  // Represents a Cobalt event.
  //
  // Only supports a single-dimension occurrence and count events.
  enum class Type {
    Occurrence,
    Count,
  };

  CobaltEvent(uint32_t metric_id, uint32_t event_code)
      : type(Type::Occurrence), metric_id(metric_id), event_code(event_code), count(0u) {}

  CobaltEvent(uint32_t metric_id, uint32_t event_code, uint64_t count)
      : type(Type::Count), metric_id(metric_id), event_code(event_code), count(count) {}

  // Define constructors that allow for the omission of a metric id.
  template <typename EventCodeType>
  explicit CobaltEvent(EventCodeType event_code)
      : type(Type::Occurrence),
        metric_id(MetricIDForEventCode(event_code)),
        event_code(static_cast<uint32_t>(event_code)),
        count(0u) {
    static_assert(std::is_enum<EventCodeType>::value, "EventCodeType must be an enum");
  }

  template <typename EventCodeType>
  CobaltEvent(EventCodeType event_code, uint64_t count)
      : type(Type::Count),
        metric_id(MetricIDForEventCode(event_code)),
        event_code(static_cast<uint32_t>(event_code)),
        count(count) {
    static_assert(std::is_enum<EventCodeType>::value, "EventCodeType must be an enum");
  }

  std::string ToString() const;

  Type type;
  uint32_t metric_id = 0;
  uint32_t event_code = 0;
  uint64_t count = 0;
};

bool operator==(const CobaltEvent& lhs, const CobaltEvent& rhs);
std::ostream& operator<<(std::ostream& os, const CobaltEvent& event);

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_UTILS_COBALT_EVENT_H_

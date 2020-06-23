// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_COBALT_EVENT_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_COBALT_EVENT_H_

#include <fuchsia/cobalt/cpp/fidl.h>

#include <ostream>
#include <type_traits>

#include "src/developer/forensics/utils/cobalt/metrics.h"

namespace forensics {
namespace cobalt {

struct Event {
  Event(EventType type, uint32_t metric_id, const std::vector<uint32_t>& dimensions, uint64_t count)
      : type(type), metric_id(metric_id), dimensions(dimensions), count(count) {}

  // Define constructors that allow for the omission of a metric id.
  template <typename DimensionType>
  explicit Event(DimensionType dimension)
      : type(EventTypeForEventCode(dimension)),
        metric_id(MetricIDForEventCode(dimension)),
        dimensions({static_cast<uint32_t>(dimension)}),
        count(0u) {
    static_assert(std::is_enum<DimensionType>::value, "DimensionType must be an enum");
  }

  template <typename DimensionType>
  Event(DimensionType dimension, uint64_t count)
      : type(EventTypeForEventCode(dimension)),
        metric_id(MetricIDForEventCode(dimension)),
        dimensions({static_cast<uint32_t>(dimension)}),
        count(count) {
    static_assert(std::is_enum<DimensionType>::value, "DimensionType must be an enum");
  }

  template <typename DimensionTypesH, typename... DimensionTypesT,
            typename = std::enable_if_t<(std::is_enum_v<DimensionTypesT> && ...)>>
  explicit Event(DimensionTypesH dimensions_h, DimensionTypesT... dimensions_t)
      : type(EventType::kMultidimensionalOccurrence),
        metric_id(MetricIDForEventCode(dimensions_h, dimensions_t...)),
        dimensions(std::vector<uint32_t>({
            static_cast<uint32_t>(dimensions_h),
            static_cast<uint32_t>(dimensions_t)...,
        })),
        count(1u) {
    static_assert(std::is_enum_v<DimensionTypesH>, "DimensionTypes must be enums");
  }

  std::string ToString() const;

  EventType type;
  uint32_t metric_id = 0;
  std::vector<uint32_t> dimensions;

  union {
    uint64_t count;          // Used for Count metrics.
    uint64_t usecs_elapsed;  // Used for Time Elapsed metrics.
  };
};

bool operator==(const Event& lhs, const Event& rhs);
std::ostream& operator<<(std::ostream& os, const Event& event);

}  // namespace cobalt
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_COBALT_EVENT_H_

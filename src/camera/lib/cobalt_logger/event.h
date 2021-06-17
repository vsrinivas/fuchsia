// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_COBALT_LOGGER_EVENT_H_
#define SRC_CAMERA_LIB_COBALT_LOGGER_EVENT_H_

#include <ostream>
#include <type_traits>

#include "src/camera/lib/cobalt_logger/metrics.h"

namespace camera::cobalt {

class Event {
 public:
  explicit Event(EventType type, uint32_t metric_id, std::vector<uint32_t> dimensions)
      : type_(type), metric_id_(metric_id), dimensions_(std::move(dimensions)) {}

  virtual ~Event() = default;

  virtual std::string ToString() const;

  EventType GetType() const { return type_; }
  uint32_t GetMetricId() const { return metric_id_; }
  const std::vector<uint32_t>& GetDimensions() const { return dimensions_; }

 private:
  EventType type_;
  uint32_t metric_id_;
  std::vector<uint32_t> dimensions_;
};

template <typename PayloadType>
class CameraEvent final : public Event {
 public:
  explicit CameraEvent(EventType type, uint32_t metric_id, std::vector<uint32_t> dimensions,
                       PayloadType payload)
      : Event(type, metric_id, std::move(dimensions)), payload_(payload) {}

  ~CameraEvent() override = default;

  std::string ToString() const override;

 private:
  PayloadType payload_;
};

template <typename PayloadType>
std::ostream& operator<<(std::ostream& os, const CameraEvent<PayloadType>& event);

}  // namespace camera::cobalt

#endif  // SRC_CAMERA_LIB_COBALT_LOGGER_EVENT_H_

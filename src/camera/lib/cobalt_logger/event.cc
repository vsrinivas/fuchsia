// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/lib/cobalt_logger/event.h"

#include <fuchsia/metrics/cpp/fidl.h>

#include <sstream>

#include "src/lib/fxl/strings/string_printf.h"

namespace camera::cobalt {
namespace {

std::string ToString(const std::vector<uint32_t>& dimensions) {
  std::stringstream output;
  output << "{";
  for (uint32_t idx = 0; idx < dimensions.size(); idx++) {
    output << dimensions[idx];

    if (idx < dimensions.size() - 1) {
      output << ", ";
    }
  }
  output << "}";
  return output.str();
}

std::string ToString(const std::vector<fuchsia::metrics::HistogramBucket>& buckets) {
  std::stringstream output;
  output << "{";
  for (const auto& bucket : buckets) {
    output << "(" << bucket.index << ", " << bucket.count << ")";
  }
  output << "}";
  return output.str();
}

}  // namespace

std::string Event::ToString() const {
  return fxl::StringPrintf("{type: integer, metric_id: %u, dimensions: %s}", metric_id_,
                           camera::cobalt::ToString(dimensions_).c_str());
}

template <>
std::string CameraEvent<uint32_t>::ToString() const {
  return fxl::StringPrintf("{type: integer, metric_id: %u, dimensions: %s, payload: %u}",
                           GetMetricId(), camera::cobalt::ToString(GetDimensions()).c_str(),
                           payload_);
}

template<>
std::string CameraEvent<uint64_t>::ToString() const {
  return fxl::StringPrintf("{type: integer, metric_id: %u, dimensions: %s, payload: %lu}",
                           GetMetricId(), camera::cobalt::ToString(GetDimensions()).c_str(),
                           payload_);
}

template<>
std::string CameraEvent<int64_t>::ToString() const {
  return fxl::StringPrintf("{type: integer, metric_id: %u, dimensions: %s, payload: %ld}",
                           GetMetricId(), camera::cobalt::ToString(GetDimensions()).c_str(),
                           payload_);
}

template <>
std::string CameraEvent<std::string>::ToString() const {
  return fxl::StringPrintf("{type: integer, metric_id: %u, dimensions: %s, payload: %s}",
                           GetMetricId(), camera::cobalt::ToString(GetDimensions()).c_str(),
                           payload_.c_str());
}

template <>
std::string CameraEvent<std::vector<fuchsia::metrics::HistogramBucket>>::ToString() const {
  return fxl::StringPrintf("{type: integer, metric_id: %u, dimensions: %s, payload: %s}",
                           GetMetricId(), camera::cobalt::ToString(GetDimensions()).c_str(),
                           camera::cobalt::ToString(payload_).c_str());
}

template <typename PayloadType>
std::ostream& operator<<(std::ostream& os, const CameraEvent<PayloadType>& event) {
  return os << event.ToString();
}

}  // namespace camera::cobalt

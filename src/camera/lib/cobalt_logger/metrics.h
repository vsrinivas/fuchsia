// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_COBALT_LOGGER_METRICS_H_
#define SRC_CAMERA_LIB_COBALT_LOGGER_METRICS_H_

#include "src/camera/lib/cobalt_logger/camera_metrics.cb.h"

using namespace camera__metrics;

namespace camera::cobalt {

enum class FrameDropReason : uint32_t {
  kGeneral = CameraFrameDropCountsPerStreamMetricDimensionErrorCode_General,
  kInvalidFrame = CameraFrameDropCountsPerStreamMetricDimensionErrorCode_InvalidFrame,
  kNoMemory = CameraFrameDropCountsPerStreamMetricDimensionErrorCode_NoMemory,
  kFrameIdInUse = CameraFrameDropCountsPerStreamMetricDimensionErrorCode_FrameIdInUse,
  kInvalidTimestamp = CameraFrameDropCountsPerStreamMetricDimensionErrorCode_InvalidTimestamp,
  kTooManyFramesInFlight =
      CameraFrameDropCountsPerStreamMetricDimensionErrorCode::TooManyFramesInFlight,
  kMuted = CameraFrameDropCountsPerStreamMetricDimensionErrorCode_Muted,
  kNoClient = CameraFrameDropCountsPerStreamMetricDimensionErrorCode_NoClient,
};

enum class StreamType : uint32_t {
  kStream0 = CameraFrameDropCountsPerStreamMetricDimensionStreamType_Stream0,
  kStream1 = CameraFrameDropCountsPerStreamMetricDimensionStreamType_Stream1,
  kStream2 = CameraFrameDropCountsPerStreamMetricDimensionStreamType_Stream2,
  kStream3 = CameraFrameDropCountsPerStreamMetricDimensionStreamType_Stream3,
  kStream4 = CameraFrameDropCountsPerStreamMetricDimensionStreamType_Stream4,
  kStream5 = CameraFrameDropCountsPerStreamMetricDimensionStreamType_Stream5,
  kStream6 = CameraFrameDropCountsPerStreamMetricDimensionStreamType_Stream6,
  kStream7 = CameraFrameDropCountsPerStreamMetricDimensionStreamType_Stream7,
  kStream8 = CameraFrameDropCountsPerStreamMetricDimensionStreamType_Stream8,
  kStreamUnknown = 99,  // Corresponding to max_event_code
};

// Corresponds to |fuchsia::metrics::MetricEventLogger| public methods.
enum EventType {
  kOccurrence,
  kInteger,
  kIntegerHistogram,
  kString,
};

inline const char* FrameDropReasonToString(FrameDropReason reason) {
  switch (reason) {
    case FrameDropReason::kGeneral:
      return "General";
    case FrameDropReason::kInvalidFrame:
      return "InvalidFrame";
    case FrameDropReason::kNoMemory:
      return "NoMemory";
    case FrameDropReason::kFrameIdInUse:
      return "FrameIdInUse";
    case FrameDropReason::kInvalidTimestamp:
      return "InvalidTimestamp";
    case FrameDropReason::kTooManyFramesInFlight:
      return "TooManyFramesInFlight";
    case FrameDropReason::kMuted:
      return "Muted";
    case FrameDropReason::kNoClient:
      return "NoClient";
  }
}

}  // namespace camera::cobalt

#endif  // SRC_CAMERA_LIB_COBALT_LOGGER_METRICS_H_

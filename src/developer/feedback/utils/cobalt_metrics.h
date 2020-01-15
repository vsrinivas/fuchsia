// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_UTILS_COBALT_METRICS_H_
#define SRC_DEVELOPER_FEEDBACK_UTILS_COBALT_METRICS_H_

#include "src/developer/feedback/utils/metrics_registry.cb.h"

namespace feedback {

constexpr auto kProjectId = cobalt_registry::kProjectId;

enum class RebootReason {
  kKernelPanic = cobalt_registry::RebootMetricDimensionReason::KernelPanic,
  kOOM = cobalt_registry::RebootMetricDimensionReason::Oom,
};

enum class CrashState {
  kUnknown = cobalt_registry::CrashMetricDimensionState::Unknown,
  kFiled = cobalt_registry::CrashMetricDimensionState::Filed,
  kUploaded = cobalt_registry::CrashMetricDimensionState::Uploaded,
  kArchived = cobalt_registry::CrashMetricDimensionState::Archived,
  kGarbageCollected = cobalt_registry::CrashMetricDimensionState::GarbageCollected,
  kDropped = cobalt_registry::CrashMetricDimensionState::Dropped,
};

enum class UploadAttemptState {
  kUnknown = cobalt_registry::CrashUploadAttemptsMetricDimensionState::Unknown,
  kUploadAttempt = cobalt_registry::CrashUploadAttemptsMetricDimensionState::UploadAttempt,
  kUploaded = cobalt_registry::CrashUploadAttemptsMetricDimensionState::Uploaded,
  kArchived = cobalt_registry::CrashUploadAttemptsMetricDimensionState::Archived,
  kGarbageCollected = cobalt_registry::CrashUploadAttemptsMetricDimensionState::GarbageCollected,
};

enum class CrashpadFunctionError {
  kUnknown = cobalt_registry::CrashpadErrorsMetricDimensionFunction::Unknown,
  kInitializeDatabase = cobalt_registry::CrashpadErrorsMetricDimensionFunction::InitializeDatabase,
  kPrepareNewCrashReport =
      cobalt_registry::CrashpadErrorsMetricDimensionFunction::PrepareNewCrashReport,
  kFinishedWritingCrashReport =
      cobalt_registry::CrashpadErrorsMetricDimensionFunction::FinishedWritingCrashReport,
  kGetReportForUploading =
      cobalt_registry::CrashpadErrorsMetricDimensionFunction::GetReportForUploading,
  kRecordUploadComplete =
      cobalt_registry::CrashpadErrorsMetricDimensionFunction::RecordUploadComplete,
  kSkipReportUpload = cobalt_registry::CrashpadErrorsMetricDimensionFunction::SkipReportUpload,
  kLookUpCrashReport = cobalt_registry::CrashpadErrorsMetricDimensionFunction::LookUpCrashReport,
};

enum class TimedOutData {
  kUnknown = cobalt_registry::FeedbackDataCollectionTimeoutMetricDimensionData::Unknown,
  kSystemLog = cobalt_registry::FeedbackDataCollectionTimeoutMetricDimensionData::SystemLog,
  kKernelLog = cobalt_registry::FeedbackDataCollectionTimeoutMetricDimensionData::KernelLog,
  kScreenshot = cobalt_registry::FeedbackDataCollectionTimeoutMetricDimensionData::Screenshot,
  kInspect = cobalt_registry::FeedbackDataCollectionTimeoutMetricDimensionData::Inspect,
  kChannel = cobalt_registry::FeedbackDataCollectionTimeoutMetricDimensionData::Channel,
  kProductInfo = cobalt_registry::FeedbackDataCollectionTimeoutMetricDimensionData::ProductInfo,
  kBoardInfo = cobalt_registry::FeedbackDataCollectionTimeoutMetricDimensionData::BoardInfo,
};

inline constexpr uint32_t MetricIDForEventCode(const TimedOutData data) {
  return cobalt_registry::kFeedbackDataCollectionTimeoutMetricId;
}

inline constexpr uint32_t MetricIDForEventCode(const RebootReason reason) {
  return cobalt_registry::kRebootMetricId;
}

inline constexpr uint32_t MetricIDForEventCode(const CrashState state) {
  return cobalt_registry::kCrashMetricId;
}

inline constexpr uint32_t MetricIDForEventCode(const UploadAttemptState state) {
  return cobalt_registry::kCrashUploadAttemptsMetricId;
}

inline constexpr uint32_t MetricIDForEventCode(const CrashpadFunctionError function) {
  return cobalt_registry::kCrashpadErrorsMetricId;
}

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_UTILS_COBALT_METRICS_H_

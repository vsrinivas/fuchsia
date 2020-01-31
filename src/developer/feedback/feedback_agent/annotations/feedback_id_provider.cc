// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be // found in the LICENSE
// file.

#include "src/developer/feedback/feedback_agent/annotations/feedback_id_provider.h"

#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/lib/uuid/uuid.h"

namespace feedback {

FeedbackIdProvider::FeedbackIdProvider()
    : SingleSyncAnnotationProvider(kAnnotationDeviceFeedbackId) {}

std::set<std::string> FeedbackIdProvider::GetSupportedAnnotations() {
  return {
      kAnnotationDeviceFeedbackId,
  };
}

std::optional<std::string> FeedbackIdProvider::GetAnnotation() {
  if (std::string feedback_id = "";
      files::ReadFileToString(kFeedbackIdPath, &feedback_id) && uuid::IsValid(feedback_id)) {
    return feedback_id;
  }

  FX_LOGS(ERROR) << "Failed to read feedback id";
  return std::nullopt;
}

}  // namespace feedback

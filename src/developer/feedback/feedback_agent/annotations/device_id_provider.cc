// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/annotations/device_id_provider.h"

#include <string>

#include "src/developer/feedback/feedback_agent/annotations/aliases.h"
#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/lib/uuid/uuid.h"

namespace feedback {

DeviceIdProvider::DeviceIdProvider() : SingleSyncAnnotationProvider(kAnnotationDeviceFeedbackId) {}

AnnotationKeys DeviceIdProvider::GetSupportedAnnotations() {
  return {
      kAnnotationDeviceFeedbackId,
  };
}

std::optional<AnnotationValue> DeviceIdProvider::GetAnnotation() {
  if (std::string device_id = "";
      files::ReadFileToString(kDeviceIdPath, &device_id) && uuid::IsValid(device_id)) {
    return device_id;
  }

  FX_LOGS(ERROR) << "Failed to read feedback id";
  return std::nullopt;
}

}  // namespace feedback

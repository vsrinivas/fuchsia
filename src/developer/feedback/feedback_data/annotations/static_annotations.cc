// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_data/annotations/static_annotations.h"

#include <optional>
#include <string>

#include "src/developer/feedback/feedback_data/annotations/aliases.h"
#include "src/developer/feedback/feedback_data/annotations/board_name_provider.h"
#include "src/developer/feedback/feedback_data/annotations/utils.h"
#include "src/developer/feedback/feedback_data/constants.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/trim.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

const AnnotationKeys kSupportedAnnotations = {
    kAnnotationBuildBoard,       kAnnotationBuildProduct, kAnnotationBuildLatestCommitDate,
    kAnnotationBuildVersion,     kAnnotationBuildIsDebug, kAnnotationDeviceBoardName,
    kAnnotationDeviceFeedbackId,
};

std::optional<std::string> ReadStringFromFilepath(const std::string& filepath) {
  std::string content;
  if (!files::ReadFileToString(filepath, &content)) {
    return std::nullopt;
  }
  return fxl::TrimString(content, "\r\n").ToString();
}

std::optional<AnnotationValue> ReadAnnotationValueFromFilepath(const AnnotationKey& key,
                                                               const std::string& filepath) {
  const auto value = ReadStringFromFilepath(filepath);
  if (!value.has_value()) {
    FX_LOGS(WARNING) << "Failed to build annotation " << key;
  }
  return value;
}

std::optional<AnnotationValue> BuildAnnotationValue(const AnnotationKey& key,
                                                    DeviceIdProvider* device_id_provider) {
  if (key == kAnnotationBuildBoard) {
    return ReadAnnotationValueFromFilepath(key, "/config/build-info/board");
  } else if (key == kAnnotationBuildProduct) {
    return ReadAnnotationValueFromFilepath(key, "/config/build-info/product");
  } else if (key == kAnnotationBuildLatestCommitDate) {
    return ReadAnnotationValueFromFilepath(key, "/config/build-info/latest-commit-date");
  } else if (key == kAnnotationBuildVersion) {
    return ReadAnnotationValueFromFilepath(key, "/config/build-info/version");
  } else if (key == kAnnotationBuildIsDebug) {
#ifndef NDEBUG
    return "true";
#else
    return "false";
#endif
  } else if (key == kAnnotationDeviceBoardName) {
    return GetBoardName();
  } else if (key == kAnnotationDeviceFeedbackId) {
    return device_id_provider->GetId();
  }

  // We should never attempt to build a non-static annotation as a static annotation.
  FX_LOGS(FATAL) << "Attempting to get non-static annotation " << key << " as a static annotation";
  return std::nullopt;
}

}  // namespace

Annotations GetStaticAnnotations(const AnnotationKeys& allowlist,
                                 DeviceIdProvider* device_id_provider) {
  Annotations annotations;

  for (const auto& key : RestrictAllowlist(allowlist, kSupportedAnnotations)) {
    const auto value = BuildAnnotationValue(key, device_id_provider);
    if (value.has_value()) {
      annotations[key] = value.value();
    }
  }
  return annotations;
}

}  // namespace feedback

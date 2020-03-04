// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/annotations/build_info_provider.h"

#include <optional>
#include <string>

#include "src/developer/feedback/feedback_agent/annotations/aliases.h"
#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/trim.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

std::optional<std::string> ReadStringFromFile(const std::string& filepath) {
  std::string content;
  if (!files::ReadFileToString(filepath, &content)) {
    FX_LOGS(ERROR) << "failed to read content from " << filepath;
    return std::nullopt;
  }
  return fxl::TrimString(content, "\r\n").ToString();
}

const Annotations& GetAnnotationFilepaths() {
  static const Annotations annotation_filepaths = {
      {kAnnotationBuildBoard, "/config/build-info/board"},
      {kAnnotationBuildProduct, "/config/build-info/product"},
      {kAnnotationBuildLatestCommitDate, "/config/build-info/latest-commit-date"},
      {kAnnotationBuildVersion, "/config/build-info/version"},
  };
  return annotation_filepaths;
}

std::optional<AnnotationValue> GetAnnotation(const AnnotationKey& annotation_key) {
  if (annotation_key == kAnnotationBuildIsDebug) {
#ifndef NDEBUG
    return "true";
#else
    return "false";
#endif
  }

  auto annotation_filepaths = GetAnnotationFilepaths();
  if (annotation_filepaths.find(annotation_key) == annotation_filepaths.end()) {
    return std::nullopt;
  }

  return ReadStringFromFile(annotation_filepaths.at(annotation_key));
}

}  // namespace

BuildInfoProvider::BuildInfoProvider(const AnnotationKeys& annotations_to_get)
    : annotations_to_get_(annotations_to_get) {
  const auto supported_annotations = GetSupportedAnnotations();
  for (const auto& annotation : annotations_to_get_) {
    FX_CHECK(supported_annotations.find(annotation) != supported_annotations.end());
  }
}

AnnotationKeys BuildInfoProvider::GetSupportedAnnotations() {
  AnnotationKeys annotations;
  for (const auto& [key, _] : GetAnnotationFilepaths()) {
    annotations.insert(key);
  }
  annotations.insert(kAnnotationBuildIsDebug);
  return annotations;
}

fit::promise<Annotations> BuildInfoProvider::GetAnnotations() {
  Annotations annotations;
  for (const auto& key : annotations_to_get_) {
    auto annotation_value = GetAnnotation(key);
    if (annotation_value) {
      annotations[key] = std::move(annotation_value.value());
    } else {
      FX_LOGS(WARNING) << "Failed to build annotation " << key;
    }
  }

  return fit::make_ok_promise(annotations);
}

}  // namespace feedback

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be // found in the LICENSE
// file.

#include "src/developer/feedback/feedback_agent/annotations/build_info_provider.h"

#include <map>
#include <optional>
#include <string>

#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/trim.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

using fuchsia::feedback::Annotation;

std::optional<std::string> ReadStringFromFile(const std::string& filepath) {
  std::string content;
  if (!files::ReadFileToString(filepath, &content)) {
    FX_LOGS(ERROR) << "failed to read content from " << filepath;
    return std::nullopt;
  }
  return fxl::TrimString(content, "\r\n").ToString();
}

const std::map<std::string, std::string>& GetAnnotationFilepaths() {
  static const std::map<std::string, std::string> annotation_filepaths = {
      {kAnnotationBuildBoard, "/config/build-info/board"},
      {kAnnotationBuildProduct, "/config/build-info/product"},
      {kAnnotationBuildLatestCommitDate, "/config/build-info/latest-commit-date"},
      {kAnnotationBuildVersion, "/config/build-info/version"},
  };
  return annotation_filepaths;
}

std::optional<std::string> GetAnnotation(const std::string& annotation_key) {
  auto annotation_filepaths = GetAnnotationFilepaths();
  if (annotation_filepaths.find(annotation_key) == annotation_filepaths.end()) {
    return std::nullopt;
  }

  return ReadStringFromFile(annotation_filepaths.at(annotation_key));
}

}  // namespace

BuildInfoProvider::BuildInfoProvider(const std::set<std::string>& annotations_to_get)
    : annotations_to_get_(annotations_to_get) {
  const auto supported_annotations = GetSupportedAnnotations();
  for (const auto& annotation : annotations_to_get_) {
    FX_CHECK(supported_annotations.find(annotation) != supported_annotations.end());
  }
}

std::set<std::string> BuildInfoProvider::GetSupportedAnnotations() {
  std::set<std::string> annotations;
  for (const auto& [key, _] : GetAnnotationFilepaths()) {
    annotations.insert(key);
  }
  return annotations;
}

fit::promise<std::vector<Annotation>> BuildInfoProvider::GetAnnotations() {
  std::vector<Annotation> annotations;
  for (const auto& annotation_key : annotations_to_get_) {
    auto annotation_value = GetAnnotation(annotation_key);
    if (annotation_value) {
      Annotation annotation;
      annotation.key = annotation_key;
      annotation.value = std::move(annotation_value.value());

      annotations.push_back(std::move(annotation));
    } else {
      FX_LOGS(WARNING) << "Failed to build annotation " << annotation_key;
    }
  }

  return fit::make_ok_promise(annotations);
}

}  // namespace feedback

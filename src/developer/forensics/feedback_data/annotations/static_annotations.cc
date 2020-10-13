// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/annotations/static_annotations.h"

#include <lib/syslog/cpp/macros.h>

#include <optional>
#include <string>

#include "src/developer/forensics/feedback_data/annotations/board_name_provider.h"
#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/feedback_data/annotations/utils.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/trim.h"

namespace forensics {
namespace feedback_data {
namespace {

const AnnotationKeys kSupportedAnnotations = {
    kAnnotationBuildBoard,   kAnnotationBuildProduct, kAnnotationBuildLatestCommitDate,
    kAnnotationBuildVersion, kAnnotationBuildIsDebug, kAnnotationDeviceBoardName,
};

AnnotationOr ReadStringFromFilepath(const std::string& filepath) {
  std::string content;
  if (!files::ReadFileToString(filepath, &content)) {
    return AnnotationOr(Error::kFileReadFailure);
  }
  return AnnotationOr(std::string(fxl::TrimString(content, "\r\n")));
}

AnnotationOr ReadAnnotationOrFromFilepath(const AnnotationKey& key, const std::string& filepath) {
  const auto value = ReadStringFromFilepath(filepath);
  return value;
}

AnnotationOr BuildAnnotationOr(const AnnotationKey& key) {
  if (key == kAnnotationBuildBoard) {
    return ReadAnnotationOrFromFilepath(key, "/config/build-info/board");
  } else if (key == kAnnotationBuildProduct) {
    return ReadAnnotationOrFromFilepath(key, "/config/build-info/product");
  } else if (key == kAnnotationBuildLatestCommitDate) {
    return ReadAnnotationOrFromFilepath(key, "/config/build-info/latest-commit-date");
  } else if (key == kAnnotationBuildVersion) {
    return ReadAnnotationOrFromFilepath(key, "/config/build-info/version");
  } else if (key == kAnnotationBuildIsDebug) {
#ifndef NDEBUG
    return AnnotationOr("true");
#else
    return AnnotationOr("false");
#endif
  } else if (key == kAnnotationDeviceBoardName) {
    return GetBoardName();
  }
  // We should never attempt to build a non-static annotation as a static annotation.
  FX_LOGS(FATAL) << "Attempting to get non-static annotation " << key << " as a static annotation";
  return AnnotationOr(Error::kNotSet);
}

}  // namespace

Annotations GetStaticAnnotations(const AnnotationKeys& allowlist) {
  Annotations annotations;

  for (const auto& key : RestrictAllowlist(allowlist, kSupportedAnnotations)) {
    annotations.insert({key, BuildAnnotationOr(key)});
  }
  return annotations;
}

}  // namespace feedback_data
}  // namespace forensics

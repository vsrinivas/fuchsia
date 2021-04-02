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
    kAnnotationBuildBoard,      kAnnotationBuildProduct,         kAnnotationBuildLatestCommitDate,
    kAnnotationBuildVersion,    kAnnotationBuildVersionPreviousBoot, kAnnotationBuildIsDebug,
    kAnnotationDeviceBoardName, kAnnotationSystemBootIdCurrent,  kAnnotationSystemBootIdPrevious,
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

AnnotationOr BuildAnnotationOr(const AnnotationKey& key, const PreviousBootFile boot_id_file,
                               const PreviousBootFile build_version_file) {
  if (key == kAnnotationBuildBoard) {
    return ReadAnnotationOrFromFilepath(key, "/config/build-info/board");
  } else if (key == kAnnotationBuildProduct) {
    return ReadAnnotationOrFromFilepath(key, "/config/build-info/product");
  } else if (key == kAnnotationBuildLatestCommitDate) {
    return ReadAnnotationOrFromFilepath(key, "/config/build-info/latest-commit-date");
  } else if (key == kAnnotationBuildVersion) {
    return ReadAnnotationOrFromFilepath(key, build_version_file.CurrentBootPath());
  } else if (key == kAnnotationBuildVersionPreviousBoot) {
    return ReadAnnotationOrFromFilepath(key, build_version_file.PreviousBootPath());
  } else if (key == kAnnotationBuildIsDebug) {
#ifndef NDEBUG
    return AnnotationOr("true");
#else
    return AnnotationOr("false");
#endif
  } else if (key == kAnnotationDeviceBoardName) {
    return GetBoardName();
  } else if (key == kAnnotationSystemBootIdCurrent) {
    return ReadAnnotationOrFromFilepath(key, boot_id_file.CurrentBootPath());
  } else if (key == kAnnotationSystemBootIdPrevious) {
    return ReadAnnotationOrFromFilepath(key, boot_id_file.PreviousBootPath());
  }
  // We should never attempt to build a non-static annotation as a static annotation.
  FX_LOGS(FATAL) << "Attempting to get non-static annotation " << key << " as a static annotation";
  return AnnotationOr(Error::kNotSet);
}

}  // namespace

Annotations GetStaticAnnotations(const AnnotationKeys& allowlist,
                                 const PreviousBootFile boot_id_file,
                                 const PreviousBootFile build_version_file) {
  Annotations annotations;

  for (const auto& key : RestrictAllowlist(allowlist, kSupportedAnnotations)) {
    annotations.insert({key, BuildAnnotationOr(key, boot_id_file, build_version_file)});
  }
  return annotations;
}

}  // namespace feedback_data
}  // namespace forensics

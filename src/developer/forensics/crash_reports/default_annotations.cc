// Copyright 2021 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/default_annotations.h"

#include <lib/syslog/cpp/macros.h>

#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/trim.h"

namespace forensics::crash_reports {
namespace {

ErrorOr<std::string> ReadStringFromFile(const std::string& filepath) {
  std::string content;
  if (!files::ReadFileToString(filepath, &content)) {
    FX_LOGS(ERROR) << "Failed to read content from " << filepath;
    return Error::kFileReadFailure;
  }
  return std::string(fxl::TrimString(content, "\r\n"));
}

}  // namespace

ErrorOr<std::string> GetBuildVersion(const std::string& build_version_path) {
  return ReadStringFromFile(build_version_path);
}

crash_reports::AnnotationMap GetDefaultAnnotations(const std::string& build_version_path,
                                                   const std::string& build_board_path,
                                                   const std::string& build_product_path,
                                                   const std::string& build_commit_date_path) {
  const auto build_version = GetBuildVersion(build_version_path);

  crash_reports::AnnotationMap default_annotations;
  default_annotations.Set("osName", "Fuchsia")
      .Set("osVersion", build_version)
      // TODO(fxbug.dev/70398): These keys are duplicates from feedback data, find a better way to
      // share them.
      .Set("build.version", build_version)
      .Set("build.board", ReadStringFromFile(build_board_path))
      .Set("build.product", ReadStringFromFile(build_product_path))
      .Set("build.latest-commit-date", ReadStringFromFile(build_commit_date_path));

  return default_annotations;
}

}  // namespace forensics::crash_reports

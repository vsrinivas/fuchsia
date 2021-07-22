// Copyright 2021 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/default_annotations.h"

#include <lib/syslog/cpp/macros.h>

#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/trim.h"

namespace forensics::feedback_data {
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

ErrorOr<std::string> GetCurrentBootId(const std::string& boot_id_path) {
  return ReadStringFromFile(boot_id_path);
}

ErrorOr<std::string> GetPreviousBootId(const std::string& previous_boot_id_path) {
  return ReadStringFromFile(previous_boot_id_path);
}

ErrorOr<std::string> GetCurrentBuildVersion(const std::string& build_version_path) {
  return ReadStringFromFile(build_version_path);
}

ErrorOr<std::string> GetPreviousBuildVersion(const std::string& previous_build_version_path) {
  return ReadStringFromFile(previous_build_version_path);
}

}  // namespace forensics::feedback_data

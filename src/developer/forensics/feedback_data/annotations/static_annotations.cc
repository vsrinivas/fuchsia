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
    kAnnotationBuildBoard,
    kAnnotationBuildProduct,
    kAnnotationBuildLatestCommitDate,
    kAnnotationBuildVersion,
    kAnnotationBuildVersionPreviousBoot,
    kAnnotationBuildIsDebug,
    kAnnotationDeviceBoardName,
    kAnnotationSystemBootIdCurrent,
    kAnnotationSystemBootIdPrevious,
    kAnnotationSystemLastRebootReason,
    kAnnotationSystemLastRebootUptime,
};

AnnotationOr ReadStringFromFilepath(const std::string& filepath) {
  std::string content;
  if (!files::ReadFileToString(filepath, &content)) {
    return Error::kFileReadFailure;
  }
  return std::string(fxl::TrimString(content, "\r\n"));
}

AnnotationOr ReadAnnotationOrFromFilepath(const AnnotationKey& key, const std::string& filepath) {
  const auto value = ReadStringFromFilepath(filepath);
  return value;
}

AnnotationOr BuildAnnotationOr(const AnnotationKey& key,
                               const ErrorOr<std::string>& current_boot_id,
                               const ErrorOr<std::string>& previous_boot_id,
                               const ErrorOr<std::string>& current_build_version,
                               const ErrorOr<std::string>& previous_build_version,
                               const ErrorOr<std::string>& last_reboot_reason,
                               const ErrorOr<std::string>& last_reboot_uptime) {
  if (key == kAnnotationBuildBoard) {
    return ReadAnnotationOrFromFilepath(key, "/config/build-info/board");
  } else if (key == kAnnotationBuildProduct) {
    return ReadAnnotationOrFromFilepath(key, "/config/build-info/product");
  } else if (key == kAnnotationBuildLatestCommitDate) {
    return ReadAnnotationOrFromFilepath(key, "/config/build-info/latest-commit-date");
  } else if (key == kAnnotationBuildVersion) {
    return current_build_version;
  } else if (key == kAnnotationBuildVersionPreviousBoot) {
    return previous_build_version;
  } else if (key == kAnnotationBuildIsDebug) {
#ifndef NDEBUG
    return "true";
#else
    return "false";
#endif
  } else if (key == kAnnotationDeviceBoardName) {
    return GetBoardName();
  } else if (key == kAnnotationSystemBootIdCurrent) {
    return current_boot_id;
  } else if (key == kAnnotationSystemBootIdPrevious) {
    return previous_boot_id;
  } else if (key == kAnnotationSystemLastRebootReason) {
    return last_reboot_reason;
  } else if (key == kAnnotationSystemLastRebootUptime) {
    return last_reboot_uptime;
  }
  // We should never attempt to build a non-static annotation as a static annotation.
  FX_LOGS(FATAL) << "Attempting to get non-static annotation " << key << " as a static annotation";
  return Error::kNotSet;
}

}  // namespace

Annotations GetStaticAnnotations(const AnnotationKeys& allowlist,
                                 const ErrorOr<std::string>& current_boot_id,
                                 const ErrorOr<std::string>& previous_boot_id,
                                 const ErrorOr<std::string>& current_build_version,
                                 const ErrorOr<std::string>& previous_build_version,
                                 const ErrorOr<std::string>& last_reboot_reason,
                                 const ErrorOr<std::string>& last_reboot_uptime

) {
  Annotations annotations;

  for (const auto& key : RestrictAllowlist(allowlist, kSupportedAnnotations)) {
    annotations.insert(
        {key, BuildAnnotationOr(key, current_boot_id, previous_boot_id, current_build_version,
                                previous_build_version, last_reboot_reason, last_reboot_uptime)});
  }
  return annotations;
}

}  // namespace feedback_data
}  // namespace forensics

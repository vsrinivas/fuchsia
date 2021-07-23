// Copyright 2021 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_DEFAULT_ANNOTATIONS_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_DEFAULT_ANNOTATIONS_H_

#include <string>

#include "src/developer/forensics/feedback/constants.h"
#include "src/developer/forensics/utils/errors.h"

namespace forensics::feedback_data {

ErrorOr<std::string> GetCurrentBootId(
    const std::string& boot_id_path = feedback::kCurrentBootIdPath);

ErrorOr<std::string> GetPreviousBootId(
    const std::string& previousboot_id_path = feedback::kPreviousBootIdPath);

ErrorOr<std::string> GetCurrentBuildVersion(
    const std::string& build_version_path = feedback::kBuildVersionPath);

ErrorOr<std::string> GetPreviousBuildVersion(
    const std::string& previous_build_version_path = feedback::kPreviousBuildVersionPath);

}  // namespace forensics::feedback_data

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_DEFAULT_ANNOTATIONS_H_

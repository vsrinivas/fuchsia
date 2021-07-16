// Copyright 2021 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_DEFAULT_ANNOTATIONS_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_DEFAULT_ANNOTATIONS_H_

#include <string>

#include "src/developer/forensics/crash_reports/annotation_map.h"
#include "src/developer/forensics/feedback/constants.h"
#include "src/developer/forensics/utils/errors.h"

namespace forensics::crash_reports {

ErrorOr<std::string> GetBuildVersion(
    const std::string& build_version_path = feedback::kBuildVersionPath);

crash_reports::AnnotationMap GetDefaultAnnotations(
    const std::string& build_version_path = feedback::kBuildVersionPath,
    const std::string& build_board_path = feedback::kBuildBoardPath,
    const std::string& build_product_path = feedback::kBuildProductPath,
    const std::string& build_commit_date_path = feedback::kBuildCommitDatePath);

}  // namespace forensics::crash_reports

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_DEFAULT_ANNOTATIONS_H_

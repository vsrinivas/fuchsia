// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_NAMESPACE_INIT_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_NAMESPACE_INIT_H_

#include <string>

#include "src/developer/forensics/feedback/constants.h"
#include "src/developer/forensics/utils/cobalt/logger.h"

namespace forensics::feedback {

// Reboot reporting

// Return whether |not_a_fdr_path| existed in the file system and create it otherwise.
bool TestAndSetNotAFdr(const std::string& not_a_fdr_file = kNotAFdrFile);

// Moves the previous reboot reason to |to| from either |from| or |legacy_from|.
void MovePreviousRebootReason(const std::string& from = kCurrentGracefulRebootReasonFile,
                              const std::string& legacy_from = kLegacyGracefulRebootReasonFile,
                              const std::string& to = kPreviousGracefulRebootReasonFile);

// Feedback data
//
// Decompress and concatenate the logs from the previous boot in |dir| and store the at
// |write_path|.
void CreatePreviousLogsFile(cobalt::Logger* cobalt, const std::string& dir = kCurrentLogsDir,
                            const std::string& write_path = kPreviousLogsFilePath);

// Move the boot id stored at |current_boot_id_path| to |previous_boot_id_path| and write a new
// boot id to |current_boot_id_path|.
void MoveAndRecordBootId(const std::string& new_boot_id,
                         const std::string& previous_boot_id_path = kPreviousBootIdPath,
                         const std::string& current_boot_id_path = kCurrentBootIdPath);

// Move the build version stored at |current_build_version_path| to |previoius_build_version_path|
// and write the current build version to |current_build_version_path|.
void MoveAndRecordBuildVersion(
    const std::string& current_build_version,
    const std::string& previous_build_version_path = kPreviousBuildVersionPath,
    const std::string& current_build_version_path = kCurrentBuildVersionPath);

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_NAMESPACE_INIT_H_

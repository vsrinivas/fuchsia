// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_NAMESPACE_INIT_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_NAMESPACE_INIT_H_

#include <string>

#include "src/developer/forensics/feedback/constants.h"

namespace forensics::feedback {

// Reboot reporting

// Return whether |not_a_fdr_path| existed in the file system and create it otherwise.
bool TestAndSetNotAFdr(const std::string& not_a_fdr_file = kNotAFdrFile);

// Moves the previous reboot reason to |to| from |from|.
void MovePreviousRebootReason(const std::string& from = kCurrentGracefulRebootReasonFile,
                              const std::string& to = kPreviousGracefulRebootReasonFile);

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_NAMESPACE_INIT_H_

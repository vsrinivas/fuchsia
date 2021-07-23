// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_REBOOT_LOG_ANNOTATIONS_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_REBOOT_LOG_ANNOTATIONS_H_

#include "src/developer/forensics/feedback/reboot_log/reboot_log.h"
#include "src/developer/forensics/utils/errors.h"

namespace forensics::feedback {

// Utilities for converting a RebootLog into annotations for snapshots.
std::string LastRebootReasonAnnotation(const RebootLog& reboot_log);
ErrorOr<std::string> LastRebootUptimeAnnotation(const RebootLog& reboot_log);

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_REBOOT_LOG_ANNOTATIONS_H_

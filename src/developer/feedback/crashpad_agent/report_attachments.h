// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_REPORT_ATTACHMENTS_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_REPORT_ATTACHMENTS_H_

#include <fuchsia/feedback/cpp/fidl.h>

#include "third_party/crashpad/client/crash_report_database.h"

namespace feedback {

// Builds the final set of attachments to attach to the crash report and writes them to
// |crashpad_report|.
//
// * Most attachments are shared across all crash reports, e.g.,
//   |feedback_data|.attachment_bundle().
// * Some attachments are report-specific, e.g., Dart exception stack trace.
// * Adds any attachments from |report|.
void BuildAttachments(const fuchsia::feedback::CrashReport& report,
                      const fuchsia::feedback::Data& feedback_data,
                      crashpad::CrashReportDatabase::NewReport* crashpad_report);

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_REPORT_ATTACHMENTS_H_

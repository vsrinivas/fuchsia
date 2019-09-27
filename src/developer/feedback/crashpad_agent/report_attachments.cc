// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/report_attachments.h"

#include "src/developer/feedback/crashpad_agent/crash_report_util.h"
#include "src/developer/feedback/crashpad_agent/crashpad_report_util.h"

namespace feedback {

namespace {

void AddFeedbackAttachments(crashpad::CrashReportDatabase::NewReport* report,
                            const fuchsia::feedback::Data& feedback_data) {
  if (!feedback_data.has_attachment_bundle()) {
    return;
  }
  AddAttachment(feedback_data.attachment_bundle().key, feedback_data.attachment_bundle().value,
                report);
}

}  // namespace

void BuildAttachments(const fuchsia::feedback::CrashReport& report,
                      const fuchsia::feedback::Data& feedback_data,
                      crashpad::CrashReportDatabase::NewReport* crashpad_report,
                      bool* has_minidump) {
  // Feedback attachments common to all crash reports.
  AddFeedbackAttachments(crashpad_report, feedback_data);

  // Optional attachments filled by the client.
  ExtractAttachments(report, crashpad_report, has_minidump);
}

}  // namespace feedback

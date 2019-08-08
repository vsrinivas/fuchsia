// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/report_attachments.h"

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>

#include <memory>
#include <string>

#include "src/developer/feedback/crashpad_agent/crash_report_util.h"
#include "src/developer/feedback/crashpad_agent/crashpad_report_util.h"
#include "third_party/crashpad/util/file/file_writer.h"

namespace fuchsia {
namespace crash {

namespace {

// The crash server expects a specific filename for the attached stack trace in Dart crash reports.
const char kAttachmentDartStackTraceFilename[] = "DartError";

void AddFeedbackAttachments(crashpad::CrashReportDatabase::NewReport* report,
                            const fuchsia::feedback::Data& feedback_data) {
  if (!feedback_data.has_attachments()) {
    return;
  }
  for (const auto& attachment : feedback_data.attachments()) {
    AddAttachment(attachment.key, attachment.value, report);
  }
}

}  // namespace

void AddManagedRuntimeExceptionAttachments(crashpad::CrashReportDatabase::NewReport* report,
                                           const fuchsia::feedback::Data& feedback_data,
                                           ManagedRuntimeException* exception) {
  AddFeedbackAttachments(report, feedback_data);

  // Language-specific attachments.
  switch (exception->Which()) {
    case ManagedRuntimeException::Tag::Invalid:
      FX_LOGS(ERROR) << "invalid ManagedRuntimeException";
      break;
    case ManagedRuntimeException::Tag::kUnknown_:
      AddAttachment("data", exception->unknown_().data, report);
      break;
    case ManagedRuntimeException::Tag::kDart:
      AddAttachment(kAttachmentDartStackTraceFilename, exception->dart().stack_trace, report);
      break;
  }
}

void AddKernelPanicAttachments(crashpad::CrashReportDatabase::NewReport* report,
                               const fuchsia::feedback::Data& feedback_data,
                               fuchsia::mem::Buffer crash_log) {
  AddFeedbackAttachments(report, feedback_data);
  AddAttachment("kernel_panic_crash_log", std::move(crash_log), report);
}

void BuildAttachments(const fuchsia::feedback::CrashReport& report,
                      const fuchsia::feedback::Data& feedback_data,
                      crashpad::CrashReportDatabase::NewReport* crashpad_report) {
  // Default attachments common to all crash reports.
  AddFeedbackAttachments(crashpad_report, feedback_data);

  // Optional attachments filled by the client.
  ExtractAttachments(report, crashpad_report);
}

}  // namespace crash
}  // namespace fuchsia

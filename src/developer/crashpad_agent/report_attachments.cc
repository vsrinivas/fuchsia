// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/crashpad_agent/report_attachments.h"

#include <memory>
#include <string>

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>

#include "third_party/crashpad/minidump/minidump_file_writer.h"
#include "third_party/crashpad/util/file/file_writer.h"

namespace fuchsia {
namespace crash {

namespace {

// The crash server expects a specific filename for the attached stack trace in
// Dart crash reports.
const char kAttachmentDartStackTraceFilename[] = "DartError";

bool WriteVMO(crashpad::FileWriter* writer, const fuchsia::mem::Buffer& vmo) {
  // TODO(frousseau): make crashpad::FileWriter VMO-aware.
  auto data = std::make_unique<uint8_t[]>(vmo.size);
  if (vmo.vmo.read(data.get(), 0u, vmo.size) != ZX_OK) {
    return false;
  }
  return writer->Write(data.get(), vmo.size);
}

bool AddAttachment(crashpad::CrashReportDatabase::NewReport* report,
                   const std::string& attachment_filename,
                   const fuchsia::mem::Buffer& attachment_content) {
  crashpad::FileWriter* writer = report->AddAttachment(attachment_filename);
  if (!writer) {
    return false;
  }
  if (!WriteVMO(writer, std::move(attachment_content))) {
    FX_LOGS(ERROR) << "error writing " << attachment_filename << " to file";
    return false;
  }
  return true;
}

void AddFeedbackAttachments(crashpad::CrashReportDatabase::NewReport* report,
                            const fuchsia::feedback::Data& feedback_data) {
  if (!feedback_data.has_attachments()) {
    return;
  }
  for (const auto& attachment : feedback_data.attachments()) {
    AddAttachment(report, attachment.key, attachment.value);
  }
}

}  // namespace

void AddManagedRuntimeExceptionAttachments(
    crashpad::CrashReportDatabase::NewReport* report,
    const fuchsia::feedback::Data& feedback_data,
    ManagedRuntimeException* exception) {
  AddFeedbackAttachments(report, feedback_data);

  // Language-specific attachments.
  switch (exception->Which()) {
    case ManagedRuntimeException::Tag::Invalid:
      FX_LOGS(ERROR) << "invalid ManagedRuntimeException";
      break;
    case ManagedRuntimeException::Tag::kUnknown_:
      AddAttachment(report, "data", exception->unknown_().data);
      break;
    case ManagedRuntimeException::Tag::kDart:
      AddAttachment(report, kAttachmentDartStackTraceFilename,
                    exception->dart().stack_trace);
      break;
  }
}

void AddKernelPanicAttachments(crashpad::CrashReportDatabase::NewReport* report,
                               const fuchsia::feedback::Data& feedback_data,
                               fuchsia::mem::Buffer crash_log) {
  AddFeedbackAttachments(report, feedback_data);
  AddAttachment(report, "kernel_panic_crash_log", std::move(crash_log));
}

}  // namespace crash
}  // namespace fuchsia

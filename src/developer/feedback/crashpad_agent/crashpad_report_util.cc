// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/crashpad_report_util.h"

#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>

#include <string>

#include "src/developer/feedback/crashpad_agent/crash_report_util.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/trim.h"

namespace feedback {

bool WriteVMO(const fuchsia::mem::Buffer& vmo, crashpad::FileWriter* writer) {
  // TODO(frousseau): make crashpad::FileWriter VMO-aware.
  auto data = std::make_unique<uint8_t[]>(vmo.size);
  if (zx_status_t status = vmo.vmo.read(data.get(), 0u, vmo.size); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "failed to read VMO";
    return false;
  }
  return writer->Write(data.get(), vmo.size);
}

bool AddAttachment(const std::string& attachment_filename,
                   const fuchsia::mem::Buffer& attachment_content,
                   crashpad::CrashReportDatabase::NewReport* crashpad_report) {
  crashpad::FileWriter* writer = crashpad_report->AddAttachment(attachment_filename);
  if (!writer) {
    return false;
  }
  if (!WriteVMO(attachment_content, writer)) {
    FX_LOGS(ERROR) << "error writing " << attachment_filename << " to file";
    return false;
  }
  return true;
}

namespace {

std::string ReadStringFromFile(const std::string& filepath) {
  std::string content;
  if (!files::ReadFileToString(filepath, &content)) {
    FX_LOGS(ERROR) << "Failed to read content from '" << filepath << "'.";
    return "unknown";
  }
  return fxl::TrimString(content, "\r\n").ToString();
}

void AddCrashServerAnnotations(const fuchsia::feedback::CrashReport& report,
                               const bool has_minidump,
                               std::map<std::string, std::string>* annotations) {
  (*annotations)["product"] = "Fuchsia";
  (*annotations)["version"] = ReadStringFromFile("/config/build-info/version");
  // We use ptype to benefit from Chrome's "Process type" handling in the crash server UI.
  (*annotations)["ptype"] = report.program_name();
  (*annotations)["osName"] = "Fuchsia";
  (*annotations)["osVersion"] = "0.0.0";
  // Only the minidump file needs to be processed by the crash server. Reports without a
  // minidump should not have their file attachments processed.
  (*annotations)["should_process"] = has_minidump ? "true" : "false";
}

void AddFeedbackAnnotations(const fuchsia::feedback::Data& feedback_data,
                            std::map<std::string, std::string>* annotations) {
  if (!feedback_data.has_annotations()) {
    return;
  }
  for (const auto& annotation : feedback_data.annotations()) {
    (*annotations)[annotation.key] = annotation.value;
  }
}

void AddFeedbackAttachments(const fuchsia::feedback::Data& feedback_data,
                            crashpad::CrashReportDatabase::NewReport* report) {
  if (!feedback_data.has_attachment_bundle()) {
    return;
  }
  AddAttachment(feedback_data.attachment_bundle().key, feedback_data.attachment_bundle().value,
                report);
}

}  // namespace

void BuildAnnotationsAndAttachments(const fuchsia::feedback::CrashReport& report,
                                    const fuchsia::feedback::Data& feedback_data,
                                    std::map<std::string, std::string>* annotations,
                                    crashpad::CrashReportDatabase::NewReport* crashpad_report,
                                    bool* has_minidump) {
  // Optional annotations and attachments filled by the client.
  ExtractAnnotationsAndAttachments(report, annotations, crashpad_report, has_minidump);

  // Crash server annotations common to all crash reports.
  AddCrashServerAnnotations(report, *has_minidump, annotations);

  // Feedback annotations common to all crash reports.
  AddFeedbackAnnotations(feedback_data, annotations);

  // Feedback attachments common to all crash reports.
  AddFeedbackAttachments(feedback_data, crashpad_report);
}

}  // namespace feedback

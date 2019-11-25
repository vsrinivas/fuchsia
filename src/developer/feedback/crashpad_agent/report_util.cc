// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/report_util.h"

#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <string>

#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/trim.h"
#include "src/lib/syslog/cpp/logger.h"

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

bool AddAttachment(const std::string& filename, const fuchsia::mem::Buffer& content,
                   crashpad::CrashReportDatabase::NewReport* crashpad_report) {
  crashpad::FileWriter* writer = crashpad_report->AddAttachment(filename);
  if (!writer) {
    return false;
  }
  if (!WriteVMO(content, writer)) {
    FX_LOGS(ERROR) << "error attaching " << filename << " to Crashpad report";
    return false;
  }
  return true;
}

namespace {

// The crash server expects a specific key for client-provided program uptimes.
const char kProgramUptimeMillisKey[] = "ptime";

// The crash server expects a specific key for client-provided event keys.
const char kEventIdKey[] = "comments";

// The crash server expects a specific key for client-provided crash signatures.
const char kCrashSignatureKey[] = "signature";

// The crash server expects specific key and values for some annotations and attachments for Dart.
const char kDartTypeKey[] = "type";
const char kDartTypeValue[] = "DartError";
const char kDartExceptionMessageKey[] = "error_message";
const char kDartExceptionRuntimeTypeKey[] = "error_runtime_type";
const char kDartExceptionStackTraceKey[] = "DartError";

void ExtractAnnotationsAndAttachments(fuchsia::feedback::CrashReport report,
                                      std::map<std::string, std::string>* annotations,
                                      std::map<std::string, fuchsia::mem::Buffer>* attachments,
                                      std::optional<fuchsia::mem::Buffer>* minidump,
                                      bool* should_process) {
  // Default annotations common to all crash reports.
  if (report.has_annotations()) {
    for (const auto& annotation : report.annotations()) {
      (*annotations)[annotation.key] = annotation.value;
    }
  }

  if (report.has_program_uptime()) {
    (*annotations)[kProgramUptimeMillisKey] =
        std::to_string(zx::duration(report.program_uptime()).to_msecs());
  }

  if (report.has_event_id()) {
    (*annotations)[kEventIdKey] = report.event_id();
  }

  // Generic-specific annotations.
  if (report.has_specific_report() && report.specific_report().is_generic()) {
    const auto& generic_report = report.specific_report().generic();
    if (generic_report.has_crash_signature()) {
      (*annotations)[kCrashSignatureKey] = generic_report.crash_signature();
    }
  }

  // Dart-specific annotations.
  if (report.has_specific_report() && report.specific_report().is_dart()) {
    (*annotations)[kDartTypeKey] = kDartTypeValue;

    const auto& dart_report = report.specific_report().dart();
    if (dart_report.has_exception_type()) {
      (*annotations)[kDartExceptionRuntimeTypeKey] = dart_report.exception_type();
    } else {
      FX_LOGS(WARNING) << "no Dart exception type to attach to Crashpad report";
    }

    if (dart_report.has_exception_message()) {
      (*annotations)[kDartExceptionMessageKey] = dart_report.exception_message();
    } else {
      FX_LOGS(WARNING) << "no Dart exception message to attach to Crashpad report";
    }
  }

  // Native-specific annotations.
  // TODO(DX-1785): add module annotations from minidump.

  // Default attachments common to all crash reports.
  if (report.has_attachments()) {
    for (auto& attachment : *(report.mutable_attachments())) {
      (*attachments)[attachment.key] = std::move(attachment.value);
    }
  }

  // Native-specific attachment (minidump).
  if (report.has_specific_report() && report.specific_report().is_native()) {
    auto& native_report = report.mutable_specific_report()->native();
    if (native_report.has_minidump()) {
      *minidump = std::move(*native_report.mutable_minidump());
      *should_process = true;
    } else {
      FX_LOGS(WARNING) << "no minidump to attach to Crashpad report";
      (*annotations)[kCrashSignatureKey] = "fuchsia-no-minidump";
    }
  }

  // Dart-specific attachment (text stack trace).
  if (report.has_specific_report() && report.specific_report().is_dart()) {
    auto& dart_report = report.mutable_specific_report()->dart();
    if (dart_report.has_exception_stack_trace()) {
      (*attachments)[kDartExceptionStackTraceKey] =
          std::move(*dart_report.mutable_exception_stack_trace());
      *should_process = true;
    } else {
      FX_LOGS(WARNING) << "no Dart exception stack trace to attach to Crashpad report";
      (*annotations)[kCrashSignatureKey] = "fuchsia-no-dart-stack-trace";
    }
  }
}

std::string ReadStringFromFile(const std::string& filepath) {
  std::string content;
  if (!files::ReadFileToString(filepath, &content)) {
    FX_LOGS(ERROR) << "Failed to read content from '" << filepath << "'.";
    return "unknown";
  }
  return fxl::TrimString(content, "\r\n").ToString();
}

void AddCrashServerAnnotations(const std::string& program_name, const bool should_process,
                               std::map<std::string, std::string>* annotations) {
  (*annotations)["product"] = "Fuchsia";
  (*annotations)["version"] = ReadStringFromFile("/config/build-info/version");
  // We use ptype to benefit from Chrome's "Process type" handling in the crash server UI.
  (*annotations)["ptype"] = program_name;
  (*annotations)["osName"] = "Fuchsia";
  (*annotations)["osVersion"] = "0.0.0";
  // Not all reports need to be processed by the crash server.
  // Typically only reports with a minidump or a Dart stack trace file need to be processed.
  (*annotations)["should_process"] = should_process ? "true" : "false";
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

void AddFeedbackAttachments(fuchsia::feedback::Data feedback_data,
                            std::map<std::string, fuchsia::mem::Buffer>* attachments) {
  if (!feedback_data.has_attachment_bundle()) {
    return;
  }
  auto* attachment_bundle = feedback_data.mutable_attachment_bundle();
  (*attachments)[attachment_bundle->key] = std::move(attachment_bundle->value);
}

}  // namespace

void BuildAnnotationsAndAttachments(fuchsia::feedback::CrashReport report,
                                    fuchsia::feedback::Data feedback_data,
                                    std::map<std::string, std::string>* annotations,
                                    std::map<std::string, fuchsia::mem::Buffer>* attachments,
                                    std::optional<fuchsia::mem::Buffer>* minidump) {
  const std::string program_name = report.program_name();

  bool should_process = false;

  // Optional annotations and attachments filled by the client.
  ExtractAnnotationsAndAttachments(std::move(report), annotations, attachments, minidump,
                                   &should_process);

  // Crash server annotations common to all crash reports.
  AddCrashServerAnnotations(program_name, should_process, annotations);

  // Feedback annotations common to all crash reports.
  AddFeedbackAnnotations(feedback_data, annotations);

  // Feedback attachments common to all crash reports.
  AddFeedbackAttachments(std::move(feedback_data), attachments);
}

}  // namespace feedback

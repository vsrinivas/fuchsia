// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/crash_report_util.h"

#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>

#include "src/developer/feedback/crashpad_agent/crashpad_report_util.h"
#include "third_party/crashpad/util/file/file_writer.h"

namespace fuchsia {
namespace crash {
namespace {

using fuchsia::feedback::Annotation;
using fuchsia::feedback::Attachment;
using fuchsia::feedback::CrashReport;

// The crash server expects specific key and values for some annotations and attachments for Dart.
const char kDartTypeKey[] = "type";
const char kDartTypeValue[] = "DartError";
const char kDartExceptionMessageKey[] = "error_message";
const char kDartExceptionRuntimeTypeKey[] = "error_runtime_type";
const char kDartExceptionStackTraceKey[] = "DartError";

}  // namespace

bool IsValid(const fuchsia::feedback::CrashReport& report) {
  return report.Which() != CrashReport::Tag::kUnknown;
}

std::string ExtractProgramName(const fuchsia::feedback::CrashReport& report) {
  switch (report.Which()) {
    case CrashReport::Tag::kUnknown:
      FX_LOGS(ERROR) << "unknown fuchsia.feedback.CrashReport type";
      return "<unknown>";
    case CrashReport::Tag::kGeneric:
      return report.generic().program_name();
    case CrashReport::Tag::kNative:
      return report.native().base_report().program_name();
    case CrashReport::Tag::kDart:
      return report.dart().base_report().program_name();
  }
}

namespace {

std::optional<std::vector<Annotation>> ExtractRawAnnotations(
    const fuchsia::feedback::GenericCrashReport& report) {
  if (!report.has_annotations()) {
    return std::nullopt;
  }
  return report.annotations();
}

std::optional<std::vector<Annotation>> ExtractRawAnnotations(
    const fuchsia::feedback::CrashReport& report) {
  switch (report.Which()) {
    case CrashReport::Tag::kUnknown:
      FX_LOGS(ERROR) << "unknown fuchsia.feedback.CrashReport type";
      return std::nullopt;
    case CrashReport::Tag::kGeneric:
      return ExtractRawAnnotations(report.generic());
    case CrashReport::Tag::kNative:
      return ExtractRawAnnotations(report.native().base_report());
    case CrashReport::Tag::kDart:
      return ExtractRawAnnotations(report.dart().base_report());
  }
}

}  // namespace

void ExtractAnnotations(const fuchsia::feedback::CrashReport& report,
                        std::map<std::string, std::string>* annotations) {
  // Default annotations common to all crash reports.
  std::optional<std::vector<Annotation>> raw_annotations = ExtractRawAnnotations(report);
  if (raw_annotations.has_value()) {
    for (const auto& annotation : raw_annotations.value()) {
      (*annotations)[annotation.key] = annotation.value;
    }
  }

  // Dart-specific annotations.
  if (report.is_dart()) {
    (*annotations)[kDartTypeKey] = kDartTypeValue;

    if (report.dart().has_type()) {
      (*annotations)[kDartExceptionRuntimeTypeKey] = report.dart().type();
    } else {
      FX_LOGS(WARNING) << "no Dart exception type to attach to Crashpad report";
    }

    if (report.dart().has_message()) {
      (*annotations)[kDartExceptionMessageKey] = report.dart().message();
    } else {
      FX_LOGS(WARNING) << "no Dart exception message to attach to Crashpad report";
    }
  }

  // Native-specific annotations.
  // TODO(DX-1785): add process annotations from minidump.
}

namespace {

void ExtractRawAttachments(const fuchsia::feedback::GenericCrashReport& report,
                           crashpad::CrashReportDatabase::NewReport* crashpad_report) {
  if (!report.has_attachments()) {
    return;
  }

  for (const auto& attachment : report.attachments()) {
    if (!AddAttachment(attachment.key, attachment.value, crashpad_report)) {
      FX_LOGS(WARNING) << "error attaching " << attachment.key << " to Crashpad report";
    }
  }
}

void ExtractRawAttachments(const fuchsia::feedback::CrashReport& report,
                           crashpad::CrashReportDatabase::NewReport* crashpad_report) {
  switch (report.Which()) {
    case CrashReport::Tag::kUnknown:
      FX_LOGS(ERROR) << "unknown fuchsia.feedback.CrashReport type";
      break;
    case CrashReport::Tag::kGeneric:
      ExtractRawAttachments(report.generic(), crashpad_report);
      break;
    case CrashReport::Tag::kNative:
      ExtractRawAttachments(report.native().base_report(), crashpad_report);
      break;
    case CrashReport::Tag::kDart:
      ExtractRawAttachments(report.dart().base_report(), crashpad_report);
      break;
  }
}

}  // namespace

void ExtractAttachments(const fuchsia::feedback::CrashReport& report,
                        crashpad::CrashReportDatabase::NewReport* crashpad_report) {
  // Default attachments common to all crash reports.
  ExtractRawAttachments(report, crashpad_report);

  // Native-specific attachment (minidump).
  if (report.is_native()) {
    if (report.native().has_minidump()) {
      if (!WriteVMO(report.native().minidump(), crashpad_report->Writer())) {
        FX_LOGS(WARNING) << "error attaching minidump to Crashpad report";
      }
    } else {
      FX_LOGS(WARNING) << "no minidump to attach to Crashpad report";
    }
  }

  // Dart-specific attachment (text stack trace).
  if (report.is_dart()) {
    if (report.dart().has_stack_trace()) {
      if (!AddAttachment(kDartExceptionStackTraceKey, report.dart().stack_trace(),
                         crashpad_report)) {
        FX_LOGS(WARNING) << "error attaching Dart exception stack trace to Crashpad report";
      }
    } else {
      FX_LOGS(WARNING) << "no Dart exception stack trace to attach to Crashpad report";
    }
  }
}

}  // namespace crash
}  // namespace fuchsia

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

// The crash server expects a specific key for client-provided crash signatures.
const char kCrashSignatureKey[] = "signature";

// The crash server expects specific key and values for some annotations and attachments for Dart.
const char kDartTypeKey[] = "type";
const char kDartTypeValue[] = "DartError";
const char kDartExceptionMessageKey[] = "error_message";
const char kDartExceptionRuntimeTypeKey[] = "error_runtime_type";
const char kDartExceptionStackTraceKey[] = "DartError";

}  // namespace

void ExtractAnnotations(const fuchsia::feedback::CrashReport& report,
                        std::map<std::string, std::string>* annotations) {
  // Default annotations common to all crash reports.
  if (report.has_annotations()) {
    for (const auto& annotation : report.annotations()) {
      (*annotations)[annotation.key] = annotation.value;
    }
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
  // TODO(DX-1785): add process annotations from minidump.
}

void ExtractAttachments(const fuchsia::feedback::CrashReport& report,
                        crashpad::CrashReportDatabase::NewReport* crashpad_report) {
  // Default attachments common to all crash reports.
  if (report.has_attachments()) {
    for (const auto& attachment : report.attachments()) {
      if (!AddAttachment(attachment.key, attachment.value, crashpad_report)) {
        FX_LOGS(WARNING) << "error attaching " << attachment.key << " to Crashpad report";
      }
    }
  }

  // Native-specific attachment (minidump).
  if (report.has_specific_report() && report.specific_report().is_native()) {
    const auto& native_report = report.specific_report().native();
    if (native_report.has_minidump()) {
      if (!WriteVMO(native_report.minidump(), crashpad_report->Writer())) {
        FX_LOGS(WARNING) << "error attaching minidump to Crashpad report";
      }
    } else {
      FX_LOGS(WARNING) << "no minidump to attach to Crashpad report";
    }
  }

  // Dart-specific attachment (text stack trace).
  if (report.has_specific_report() && report.specific_report().is_dart()) {
    const auto& dart_report = report.specific_report().dart();
    if (dart_report.has_exception_stack_trace()) {
      if (!AddAttachment(kDartExceptionStackTraceKey, dart_report.exception_stack_trace(),
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

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/report_util.h"

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <string>

#include "src/developer/forensics/crash_reports/constants.h"
#include "src/developer/forensics/crash_reports/errors.h"
#include "src/developer/forensics/crash_reports/report.h"

namespace forensics {
namespace crash_reports {

std::string Shorten(std::string program_name) {
  // Remove leading whitespace
  const size_t first_non_whitespace = program_name.find_first_not_of(" ");
  if (first_non_whitespace == std::string::npos) {
    return "";
  }
  program_name = program_name.substr(first_non_whitespace);

  // Remove the "fuchsia-pkg://" prefix if present.
  const std::string fuchsia_pkg_prefix("fuchsia-pkg://");
  if (program_name.find(fuchsia_pkg_prefix) == 0) {
    program_name.erase(/*pos=*/0u, /*len=*/fuchsia_pkg_prefix.size());
  }
  std::replace(program_name.begin(), program_name.end(), '/', ':');

  // Remove all repeating ':'.
  for (size_t idx = program_name.find("::"); idx != std::string::npos;
       idx = program_name.find("::")) {
    program_name.erase(idx, 1);
  }

  // Remove trailing white space
  const size_t last_non_whitespace = program_name.find_last_not_of(" ");
  return (last_non_whitespace == std::string::npos)
             ? ""
             : program_name.substr(0, last_non_whitespace + 1);
}

std::string Logname(std::string name) {
  // Normalize |name|.
  name = Shorten(name);

  // Find the last colon in |name|.
  const size_t last_colon = name.find_last_of(":");
  if (last_colon == std::string::npos) {
    return name;
  }

  // Determine if there's a ".cmx" suffix in |name|.
  const size_t cmx_suffix = name.find_last_of(".cmx");
  if (cmx_suffix == std::string::npos || cmx_suffix <= last_colon) {
    return name;
  }

  // Extract the string between the last colon and the ".cmx" suffix.
  return name.substr(last_colon + 1, cmx_suffix - sizeof(".cmx") - last_colon + 1);
}

namespace {

// The crash server expects certain keys from the client for certain fields.
const char kProgramUptimeMillisKey[] = "ptime";
const char kEventIdKey[] = "comments";
const char kCrashSignatureKey[] = "signature";
const char kDartTypeKey[] = "type";
const char kDartTypeValue[] = "DartError";
const char kDartExceptionMessageKey[] = "error_message";
const char kDartExceptionRuntimeTypeKey[] = "error_runtime_type";
const char kDartExceptionStackTraceKey[] = "DartError";
const char kReportTimeMillis[] = "reportTimeMillis";
const char kIsFatalKey[] = "isFatal";
const char kProcessNameKey[] = "crash.process.name";
const char kThreadNameKey[] = "crash.thread.name";

// Extra keys that the crash server does *not* have a dependency on.
const char kProcessKoidKey[] = "crash.process.koid";
const char kThreadKoidKey[] = "crash.thread.koid";

void ExtractAnnotationsAndAttachments(fuchsia::feedback::CrashReport report,
                                      AnnotationMap* annotations,
                                      std::map<std::string, fuchsia::mem::Buffer>* attachments,
                                      std::optional<fuchsia::mem::Buffer>* minidump,
                                      bool* should_process) {
  // Default annotations common to all crash reports.
  if (report.has_annotations()) {
    annotations->Set(report.annotations());
  }

  if (report.has_program_uptime()) {
    annotations->Set(kProgramUptimeMillisKey, zx::duration(report.program_uptime()).to_msecs());
  }

  if (report.has_event_id()) {
    annotations->Set(kEventIdKey, report.event_id());
  }

  if (report.has_crash_signature()) {
    annotations->Set(kCrashSignatureKey, report.crash_signature());
  }

  if (report.has_is_fatal()) {
    annotations->Set(kIsFatalKey, report.is_fatal());
  }

  // Generic-specific annotations.
  if (report.has_specific_report() && report.specific_report().is_generic()) {
    const auto& generic_report = report.specific_report().generic();
    if (generic_report.has_crash_signature()) {
      annotations->Set(kCrashSignatureKey, generic_report.crash_signature());
    }
  }

  // Dart-specific annotations.
  if (report.has_specific_report() && report.specific_report().is_dart()) {
    annotations->Set(kDartTypeKey, kDartTypeValue);

    const auto& dart_report = report.specific_report().dart();
    if (dart_report.has_exception_type()) {
      annotations->Set(kDartExceptionRuntimeTypeKey, dart_report.exception_type());
    } else {
      FX_LOGS(WARNING) << "no Dart exception type to attach to Crashpad report";
    }

    if (dart_report.has_exception_message()) {
      annotations->Set(kDartExceptionMessageKey, dart_report.exception_message());
    } else {
      FX_LOGS(WARNING) << "no Dart exception message to attach to Crashpad report";
    }
  }

  // Native-specific annotations.
  if (report.has_specific_report() && report.specific_report().is_native()) {
    const auto& native_report = report.specific_report().native();
    if (native_report.has_process_name()) {
      annotations->Set(kProcessNameKey, native_report.process_name());
    }
    if (native_report.has_process_koid()) {
      annotations->Set(kProcessKoidKey, native_report.process_koid());
    }
    if (native_report.has_thread_name()) {
      annotations->Set(kThreadNameKey, native_report.thread_name());
    }
    if (native_report.has_thread_koid()) {
      annotations->Set(kThreadKoidKey, native_report.thread_koid());
    }

    // TODO(fxbug.dev/6564): add module annotations from minidump.
  }

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
      annotations->Set(kCrashSignatureKey, "fuchsia-no-minidump");
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
      annotations->Set(kCrashSignatureKey, "fuchsia-no-dart-stack-trace");
    }
  }
}

void AddSnapshotAnnotations(const SnapshotUuid& snapshot_uuid, const Snapshot& snapshot,
                            AnnotationMap* annotations) {
  if (const auto snapshot_annotations = snapshot.LockAnnotations(); snapshot_annotations) {
    annotations->Set(*snapshot_annotations);
  }
}

void AddCrashServerAnnotations(const std::string& program_name,
                               const std::optional<zx::time_utc>& current_time,
                               const ::fit::result<std::string, Error>& device_id,
                               const Product& product, const bool should_process,
                               AnnotationMap* annotations) {
  // Product.
  annotations->Set("product", product.name)
      .Set("version", product.version)
      .Set("channel", product.channel);

  // Program.
  // TODO(fxbug.dev/57502): for historical reasons, we used ptype to benefit from Chrome's "Process
  // type" handling in the crash server UI. Remove once the UI can fallback on "Program".
  annotations->Set("ptype", program_name);
  annotations->Set("program", program_name);

  // We set the report time only if we were able to get an accurate one.
  if (current_time.has_value()) {
    annotations->Set(kReportTimeMillis, current_time.value().get() / zx::msec(1).get());
  } else {
    annotations->Set("debug.report-time.set", false);
  }

  // We set the device's global unique identifier only if the device has one.
  if (device_id.is_ok()) {
    annotations->Set("guid", device_id.value());
  } else {
    annotations->Set("debug.guid.set", false).Set("debug.device-id.error", device_id.error());
  }

  // Not all reports need to be processed by the crash server.
  // Typically only reports with a minidump or a Dart stack trace file need to be processed.
  annotations->Set("should_process", should_process);
}

}  // namespace

std::optional<Report> MakeReport(fuchsia::feedback::CrashReport report, const ReportId report_id,
                                 const SnapshotUuid& snapshot_uuid, const Snapshot& snapshot,
                                 const std::optional<zx::time_utc>& current_time,
                                 const ::fit::result<std::string, Error>& device_id,
                                 const AnnotationMap& default_annotations, const Product& product,
                                 const bool is_hourly_report) {
  const std::string program_name = report.program_name();
  const std::string shortname = Shorten(program_name);

  AnnotationMap annotations = default_annotations;
  std::map<std::string, fuchsia::mem::Buffer> attachments;
  std::optional<fuchsia::mem::Buffer> minidump;
  bool should_process = false;

  // Optional annotations and attachments filled by the client.
  ExtractAnnotationsAndAttachments(std::move(report), &annotations, &attachments, &minidump,
                                   &should_process);

  // Snapshot annotations specific to this crash report.
  AddSnapshotAnnotations(snapshot_uuid, snapshot, &annotations);

  // Crash server annotations common to all crash reports.
  AddCrashServerAnnotations(program_name, current_time, device_id, product, should_process,
                            &annotations);

  return Report::MakeReport(report_id, shortname, annotations, std::move(attachments),
                            snapshot_uuid, std::move(minidump), is_hourly_report);
}

}  // namespace crash_reports
}  // namespace forensics

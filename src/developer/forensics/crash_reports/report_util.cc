// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/report_util.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <string>

#include "src/developer/forensics/crash_reports/errors.h"

namespace forensics {
namespace crash_reports {

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

// The crash server expects a specific key for client-provided report time.
constexpr char kReportTimeMillis[] = "reportTimeMillis";

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

void AddCrashServerAnnotations(const std::string& program_name,
                               const std::optional<zx::time_utc>& current_time,
                               const ::fit::result<std::string, Error>& device_id,
                               const ErrorOr<std::string>& os_version, const Product& product,
                               const bool should_process,
                               std::map<std::string, std::string>* annotations) {
  // Product.
  (*annotations)["product"] = product.name;
  if (product.version.HasValue()) {
    (*annotations)["version"] = product.version.Value();
  } else {
    (*annotations)["version"] = "<unknown>";
    (*annotations)["debug.version.error"] = ToReason(product.version.Error());
  }
  if (product.channel.HasValue()) {
    (*annotations)["channel"] = product.channel.Value();
  } else {
    // "channel" is a required field on the crash server that defaults to the empty string. But on
    // Fuchsia, the system update channel can be the empty string in the case of a fresh pave
    // typically. So in case the channel is unavailable, we set the value to "<unknown>" to
    // distinguish it from the default empty string value it would get on the crash server.
    (*annotations)["channel"] = "<unknown>";
    (*annotations)["debug.channel.error"] = ToReason(product.channel.Error());
  }

  // Program.
  // We use ptype to benefit from Chrome's "Process type" handling in the crash server UI.
  (*annotations)["ptype"] = program_name;

  // OS.
  (*annotations)["osName"] = "Fuchsia";
  if (os_version.HasValue()) {
    (*annotations)["osVersion"] = os_version.Value();
  } else {
    (*annotations)["osVersion"] = "<unknown>";
    (*annotations)["debug.os.version.error"] = ToReason(os_version.Error());
  }

  // We set the report time only if we were able to get an accurate one.
  if (current_time.has_value()) {
    (*annotations)[kReportTimeMillis] =
        std::to_string(current_time.value().get() / zx::msec(1).get());
  } else {
    (*annotations)["debug.report-time.set"] = "false";
  }

  // We set the device's global unique identifier only if the device has one.
  if (device_id.is_ok()) {
    (*annotations)["guid"] = device_id.value();
  } else {
    (*annotations)["debug.guid.set"] = "false";
    (*annotations)["debug.device-id.error"] = ToReason(device_id.error());
  }

  // Not all reports need to be processed by the crash server.
  // Typically only reports with a minidump or a Dart stack trace file need to be processed.
  (*annotations)["should_process"] = should_process ? "true" : "false";
}

void AddBugreportAnnotations(const fuchsia::feedback::Bugreport& bugreport,
                             std::map<std::string, std::string>* annotations) {
  if (!bugreport.has_annotations()) {
    return;
  }
  for (const auto& annotation : bugreport.annotations()) {
    (*annotations)[annotation.key] = annotation.value;
  }
}

void AddBugreportAsAttachment(fuchsia::feedback::Bugreport bugreport,
                              std::map<std::string, fuchsia::mem::Buffer>* attachments) {
  if (!bugreport.has_bugreport()) {
    return;
  }
  auto* bugreport_attachment = bugreport.mutable_bugreport();
  (*attachments)[bugreport_attachment->key] = std::move(bugreport_attachment->value);
}

void AddBugreport(::fit::result<fuchsia::feedback::Bugreport, Error> bugreport,
                  std::map<std::string, std::string>* annotations,
                  std::map<std::string, fuchsia::mem::Buffer>* attachments) {
  if (bugreport.is_error()) {
    (*annotations)["debug.bugreport.error"] = ToReason(bugreport.error());
    return;
  }

  if (bugreport.value().IsEmpty()) {
    (*annotations)["debug.bugreport.empty"] = "true";
    return;
  }

  AddBugreportAnnotations(bugreport.value(), annotations);

  AddBugreportAsAttachment(bugreport.take_value(), attachments);
}

}  // namespace

void BuildAnnotationsAndAttachments(fuchsia::feedback::CrashReport report,
                                    ::fit::result<fuchsia::feedback::Bugreport, Error> bugreport,
                                    const std::optional<zx::time_utc>& current_time,
                                    const ::fit::result<std::string, Error>& device_id,
                                    const ErrorOr<std::string>& os_version, const Product& product,
                                    std::map<std::string, std::string>* annotations,
                                    std::map<std::string, fuchsia::mem::Buffer>* attachments,
                                    std::optional<fuchsia::mem::Buffer>* minidump) {
  const std::string program_name = report.program_name();

  bool should_process = false;

  // Optional annotations and attachments filled by the client.
  ExtractAnnotationsAndAttachments(std::move(report), annotations, attachments, minidump,
                                   &should_process);

  // Crash server annotations common to all crash reports.
  AddCrashServerAnnotations(program_name, current_time, device_id, os_version, product,
                            should_process, annotations);

  // Bugreport annotations and attachment common to all crash reports.
  AddBugreport(std::move(bugreport), annotations, attachments);
}

}  // namespace crash_reports
}  // namespace forensics

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/exceptions/handler/report_builder.h"

#include <lib/syslog/cpp/macros.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/fxl/strings/join_strings.h"

namespace forensics {
namespace exceptions {
namespace handler {

CrashReportBuilder& CrashReportBuilder::SetProcess(const zx::process& process) {
  process_name_ = (process.is_valid()) ? fsl::GetObjectName(process.get()) : "unknown_process";
  if (process.is_valid()) {
    process_koid_ = fsl::GetKoid(process.get());
  }

  return *this;
}

CrashReportBuilder& CrashReportBuilder::SetThread(const zx::thread& thread) {
  thread_name_ = (thread.is_valid()) ? fsl::GetObjectName(thread.get()) : "unknown_thread";
  if (thread.is_valid()) {
    thread_koid_ = fsl::GetKoid(thread.get());
  }

  return *this;
}

CrashReportBuilder& CrashReportBuilder::SetMinidump(zx::vmo minidump) {
  FX_CHECK(minidump.is_valid());
  minidump_ = std::move(minidump);
  return *this;
}

CrashReportBuilder& CrashReportBuilder::SetComponentInfo(
    const fuchsia::sys::internal::SourceIdentity& component_info) {
  if (!component_info.has_component_url()) {
    FX_LOGS(ERROR) << "Did not receive a component url";
  } else {
    component_url_ = component_info.component_url();
  }

  if (!component_info.has_realm_path()) {
    FX_LOGS(ERROR) << "Did not receive a realm path";
  } else {
    realm_path_ = "/" + fxl::JoinStrings(component_info.realm_path(), "/");
  }

  return *this;
}

CrashReportBuilder& CrashReportBuilder::SetExceptionExpired() {
  exception_expired_ = true;
  return *this;
}

CrashReportBuilder& CrashReportBuilder::SetProcessTerminated() {
  process_already_terminated_ = true;
  return *this;
}

fuchsia::feedback::CrashReport CrashReportBuilder::Consume() {
  FX_CHECK(process_name_.has_value()) << "Need a process name";
  FX_CHECK(thread_name_.has_value()) << "Need a thread name";
  FX_CHECK(is_valid_) << "Consume can only be called once";
  is_valid_ = false;

  using namespace fuchsia::feedback;
  CrashReport crash_report;

  const std::string program_name =
      (component_url_.has_value()) ? component_url_.value() : process_name_.value();
  crash_report.set_program_name(program_name.substr(0, fuchsia::feedback::MAX_PROGRAM_NAME_LENGTH));

  auto AddAnnotation = [&crash_report](const std::string& key, const std::string& value) {
    crash_report.mutable_annotations()->push_back(Annotation{.key = key, .value = value});
  };

  AddAnnotation("crash.process.name", process_name_.value());
  AddAnnotation("crash.thread.name", thread_name_.value());
  if (process_koid_.has_value()) {
    AddAnnotation("crash.process.koid", std::to_string(process_koid_.value()));
  }
  if (thread_koid_.has_value()) {
    AddAnnotation("crash.thread.koid", std::to_string(thread_koid_.value()));
  }
  if (!component_url_.has_value()) {
    AddAnnotation("debug.crash.component.url.set", "false");
  }
  if (realm_path_.has_value()) {
    AddAnnotation("crash.realm-path", realm_path_.value());
  }

  FX_CHECK(minidump_.has_value() || exception_expired_ || process_already_terminated_);

  if (minidump_.has_value()) {
    NativeCrashReport native_crash_report;

    fuchsia::mem::Buffer mem_buffer;
    minidump_.value().get_size(&mem_buffer.size);
    mem_buffer.vmo = std::move(minidump_.value());
    minidump_ = std::nullopt;

    native_crash_report.set_minidump(std::move(mem_buffer));
    crash_report.set_specific_report(
        SpecificCrashReport::WithNative(std::move(native_crash_report)));
  } else {
    GenericCrashReport generic_crash_report;
    if (exception_expired_) {
      generic_crash_report.set_crash_signature("fuchsia-no-minidump-exception-expired");
    } else if (process_already_terminated_) {
      generic_crash_report.set_crash_signature("fuchsia-no-minidump-process-terminated");
    }

    crash_report.set_specific_report(
        SpecificCrashReport::WithGeneric(std::move(generic_crash_report)));
  }
  return crash_report;
}

}  // namespace handler
}  // namespace exceptions
}  // namespace forensics

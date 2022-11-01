// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/exceptions/handler/report_builder.h"

#include <lib/syslog/cpp/macros.h>

#include <optional>

#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace exceptions {
namespace handler {
namespace {

std::string Sanitize(std::string process_name) {
  // Determine if ".cm" is present in |process_name| and use the substring that precedes it. This
  // works for components v1 and v2 because their processes will end with ".cmx" and ".cm"
  // respectively.
  const size_t cm_pos = process_name.find(".cm");
  if (cm_pos != std::string::npos) {
    process_name = process_name.substr(0, cm_pos);
  }

  return process_name;
}

}  // namespace

CrashReportBuilder& CrashReportBuilder::SetProcess(const zx::process& process) {
  if (!process.is_valid()) {
    process_name_ = "unknown_process";
    return *this;
  }

  process_name_ = fsl::GetObjectName(process.get());
  process_koid_ = fsl::GetKoid(process.get());

  // Get the crashed process uptime
  zx_info_process_t process_info;
  if (const zx_status_t status =
          process.get_info(ZX_INFO_PROCESS, &process_info, sizeof(process_info), nullptr, nullptr);
      status == ZX_OK) {
    if (!(process_info.flags & ZX_INFO_PROCESS_FLAG_STARTED)) {
      FX_LOGS(WARNING) << "Cannot get the start time from crashed process "
                       << process_name_.value();
      return *this;
    }

    const zx_time_t crashed_process_uptime = zx_clock_get_monotonic() - process_info.start_time;
    if (crashed_process_uptime >= 0) {
      process_uptime_ = crashed_process_uptime;
    } else {
      FX_LOGS(WARNING) << "Invalid uptime = " << crashed_process_uptime << ", for crashed process "
                       << process_name_.value();
    }
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

CrashReportBuilder& CrashReportBuilder::SetExceptionReason(
    const std::optional<ExceptionReason>& exception_reason) {
  exception_reason_ = exception_reason;
  return *this;
}

CrashReportBuilder& CrashReportBuilder::SetComponentInfo(const ComponentInfo& component_info) {
  if (!component_info.url.empty()) {
    component_url_ = component_info.url;
  }

  if (!component_info.realm_path.empty()) {
    realm_path_ = component_info.realm_path;
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

CrashReportBuilder& CrashReportBuilder::SetGwpAsanExceptionType(std::string exception_type) {
  gwp_asan_exception_type_ = std::move(exception_type);
  return *this;
}

const std::optional<std::string>& CrashReportBuilder::ProcessName() const { return process_name_; }

fuchsia::feedback::CrashReport CrashReportBuilder::Consume() {
  FX_CHECK(process_name_.has_value()) << "Need a process name";
  FX_CHECK(thread_name_.has_value()) << "Need a thread name";
  FX_CHECK(is_valid_) << "Consume can only be called once";
  is_valid_ = false;

  using namespace fuchsia::feedback;
  CrashReport crash_report;

  // Always fatal crash.
  crash_report.set_is_fatal(true);

  // Program name.
  const std::string program_name =
      (component_url_.has_value()) ? component_url_.value() : process_name_.value();
  crash_report.set_program_name(program_name.substr(0, fuchsia::feedback::MAX_PROGRAM_NAME_LENGTH));

  // Program uptime.
  if (process_uptime_.has_value()) {
    crash_report.set_program_uptime(process_uptime_.value());
  }

  // Extra annotations.
  auto AddAnnotation = [&crash_report](const std::string& key, const std::string& value) {
    crash_report.mutable_annotations()->push_back(Annotation{.key = key, .value = value});
  };
  if (!component_url_.has_value()) {
    AddAnnotation("debug.crash.component.url.set", "false");
  }
  if (realm_path_.has_value()) {
    AddAnnotation("crash.realm-path", realm_path_.value());
  }
  if (gwp_asan_exception_type_.has_value()) {
    AddAnnotation("crash.gwp_asan.exception-type", gwp_asan_exception_type_.value());
  }

  // Crash signature overwrite on channel/port overflow.
  if (exception_reason_.has_value()) {
    switch (exception_reason_.value()) {
      case ExceptionReason::kChannelOverflow:
        crash_report.set_crash_signature(fxl::StringPrintf(
            "fuchsia-%s-channel-overflow", Sanitize(process_name_.value()).c_str()));
        break;
      case ExceptionReason::kPortObserverOverflow:
        crash_report.set_crash_signature(fxl::StringPrintf(
            "fuchsia-%s-port-observer-overflow", Sanitize(process_name_.value()).c_str()));
        break;
      case ExceptionReason::kPortPacketOverflow:
        crash_report.set_crash_signature(fxl::StringPrintf(
            "fuchsia-%s-port-packet-overflow", Sanitize(process_name_.value()).c_str()));
        break;
      case ExceptionReason::kPageFaultIo:
        crash_report.set_crash_signature("fuchsia-page_fault-io");
        break;
      case ExceptionReason::kPageFaultIoDataIntegrity:
        crash_report.set_crash_signature("fuchsia-page_fault-io_data_integrity");
        break;
      case ExceptionReason::kPageFaultBadState:
        crash_report.set_crash_signature("fuchsia-page_fault-bad_state");
        break;
      case ExceptionReason::kPageFaultNoMemory:
        crash_report.set_crash_signature("fuchsia-page_fault-no_memory");
        break;
    }
  }

  // Process and thread names/koids.
  NativeCrashReport native_crash_report;
  native_crash_report.set_process_name(process_name_.value());
  if (process_koid_.has_value()) {
    native_crash_report.set_process_koid(process_koid_.value());
  }
  native_crash_report.set_thread_name(thread_name_.value());
  if (thread_koid_.has_value()) {
    native_crash_report.set_thread_koid(thread_koid_.value());
  }

  // Minidump.
  FX_CHECK(minidump_.has_value() || exception_expired_ || process_already_terminated_);
  if (minidump_.has_value()) {
    fuchsia::mem::Buffer mem_buffer;
    minidump_.value().get_size(&mem_buffer.size);
    mem_buffer.vmo = std::move(minidump_.value());
    minidump_ = std::nullopt;
    native_crash_report.set_minidump(std::move(mem_buffer));
  } else if (exception_expired_) {
    crash_report.set_crash_signature("fuchsia-no-minidump-exception-expired");
  } else if (process_already_terminated_) {
    crash_report.set_crash_signature("fuchsia-no-minidump-process-terminated");
  }

  crash_report.set_specific_report(SpecificCrashReport::WithNative(std::move(native_crash_report)));

  return crash_report;
}

}  // namespace handler
}  // namespace exceptions
}  // namespace forensics

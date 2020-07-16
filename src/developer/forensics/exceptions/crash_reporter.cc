// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/crash_reporter.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/forensics/exceptions/crash_report_generation.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/fxl/strings/join_strings.h"

namespace forensics {
namespace exceptions {

CrashReporter::CrashReporter(std::shared_ptr<sys::ServiceDirectory> services)
    : services_(services), weak_factory_(this) {}

void CrashReporter::FileCrashReport(zx::exception exception, fit::closure callback) {
  callback_ = std::move(callback);

  std::string process_name;
  zx::vmo minidump_vmo = GenerateMinidumpVMO(exception, &process_name);

  builder_.SetProcessName(process_name);
  if (minidump_vmo.is_valid()) {
    builder_.SetMinidump(std::move(minidump_vmo));
  }

  introspect_connection_ = services_->Connect<fuchsia::sys::internal::Introspect>();
  introspect_connection_.set_error_handler([crash_reporter = GetWeakPtr()](zx_status_t status) {
    if (!crash_reporter)
      return;

    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.sys.internal.Introspect";
    crash_reporter->FileCrashReport();
  });

  using fuchsia::sys::internal::Introspect_FindComponentByProcessKoid_Result;

  zx::process process;
  if (const zx_status_t status = exception.get_process(&process); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to get process";
    FileCrashReport();
    return;
  }

  const zx_koid_t process_koid = fsl::GetKoid(process.get());
  if (process_koid == 0) {
    FX_LOGS(ERROR) << "Failed to get process koid";
    FileCrashReport();
    return;
  }

  introspect_connection_->FindComponentByProcessKoid(
      process_koid,
      // |exception| is moved into the call back to keep it alive until after the component
      // information of the crashed process has been collected or has failed to be collected,
      // otherwise the kernel would terminate the process.
      [crash_reporter = GetWeakPtr(),
       exception = std::move(exception)](Introspect_FindComponentByProcessKoid_Result result) {
        if (!crash_reporter)
          return;

        if (result.is_response()) {
          if (!result.response().component_info.has_component_url()) {
            FX_LOGS(ERROR) << "Did not receive a component url";
          } else {
            crash_reporter->builder_.SetComponentUrl(
                result.response().component_info.component_url());
          }

          if (!result.response().component_info.has_realm_path()) {
            FX_LOGS(ERROR) << "Did not receive a realm path";
          } else {
            crash_reporter->builder_.SetRealmPath(
                "/" + fxl::JoinStrings(result.response().component_info.realm_path(), "/"));
          }
        } else if (result.err() != ZX_ERR_NOT_FOUND) {
          FX_PLOGS(ERROR, result.err()) << "Failed FindComponentByProcessKoid";
        }

        crash_reporter->FileCrashReport();
      });
}

void CrashReporter::FileCrashReport() {
  crash_reporter_connection_ = services_->Connect<fuchsia::feedback::CrashReporter>();

  crash_reporter_connection_.set_error_handler([crash_reporter = GetWeakPtr()](zx_status_t status) {
    if (!crash_reporter)
      return;

    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.feedback.CrashReporter";
    crash_reporter->callback_();
  });

  auto report = builder_.Consume();
  crash_reporter_connection_->File(
      std::move(report), [crash_reporter = GetWeakPtr(), program_name = report.program_name()](
                             fuchsia::feedback::CrashReporter_File_Result result) {
        if (!crash_reporter)
          return;

        if (result.is_err())
          FX_PLOGS(ERROR, result.err()) << "Error filing crash report for " << program_name;

        crash_reporter->callback_();
      });
}

fxl::WeakPtr<CrashReporter> CrashReporter::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

}  // namespace exceptions
}  // namespace forensics

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/exception_handler/handler.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/forensics/exceptions/exception_handler/minidump.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/fxl/strings/join_strings.h"

namespace forensics {
namespace exceptions {

Handler::Handler(std::shared_ptr<sys::ServiceDirectory> services)
    : services_(services), weak_factory_(this) {}

void Handler::Handle(zx::exception exception, fit::closure callback) {
  exception_ = std::move(exception);
  callback_ = [this, callback = std::move(callback)] {
    // |exception| is reset when |callback_| executes because when sysmgr.cmx crashes, it will take
    // down with it the Forensics components once the process is killed. So exceptions.cmx should
    // hold on to the exception (preventing a single-threaded process to be killed) until it gets a
    // signal from crash_reports.cmx that the report is either uploaded or stored (so that if
    // crash_reports.cmx gets restarted, the report is not lost).
    exception_.reset();
    callback();
  };

  std::string process_name;
  zx::vmo minidump_vmo = GenerateMinidumpVMO(exception_, &process_name);

  builder_.SetProcessName(process_name);
  if (minidump_vmo.is_valid()) {
    builder_.SetMinidump(std::move(minidump_vmo));
  }

  introspect_connection_ = services_->Connect<fuchsia::sys::internal::Introspect>();
  introspect_connection_.set_error_handler([handler = GetWeakPtr()](zx_status_t status) {
    if (!handler)
      return;

    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.sys.internal.Introspect";
    handler->FileCrashReport();
  });

  using fuchsia::sys::internal::Introspect_FindComponentByProcessKoid_Result;

  zx::process process;
  if (const zx_status_t status = exception_.get_process(&process); status != ZX_OK) {
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
      process_koid, [handler = GetWeakPtr()](Introspect_FindComponentByProcessKoid_Result result) {
        if (!handler)
          return;

        handler->introspect_connection_.Unbind();

        if (result.is_response()) {
          if (!result.response().component_info.has_component_url()) {
            FX_LOGS(ERROR) << "Did not receive a component url";
          } else {
            handler->builder_.SetComponentUrl(result.response().component_info.component_url());
          }

          if (!result.response().component_info.has_realm_path()) {
            FX_LOGS(ERROR) << "Did not receive a realm path";
          } else {
            handler->builder_.SetRealmPath(
                "/" + fxl::JoinStrings(result.response().component_info.realm_path(), "/"));
          }
        } else if (result.err() != ZX_ERR_NOT_FOUND) {
          FX_PLOGS(ERROR, result.err()) << "Failed FindComponentByProcessKoid";
        }

        handler->FileCrashReport();
      });
}

void Handler::FileCrashReport() {
  crash_reporter_connection_ = services_->Connect<fuchsia::feedback::CrashReporter>();

  crash_reporter_connection_.set_error_handler([handler = GetWeakPtr()](zx_status_t status) {
    if (!handler)
      return;

    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.feedback.CrashReporter";
    handler->callback_();
  });

  auto report = builder_.Consume();
  crash_reporter_connection_->File(
      std::move(report), [handler = GetWeakPtr(), program_name = report.program_name()](
                             fuchsia::feedback::CrashReporter_File_Result result) {
        if (!handler)
          return;

        handler->crash_reporter_connection_.Unbind();

        if (result.is_err())
          FX_PLOGS(ERROR, result.err()) << "Error filing crash report for " << program_name;

        handler->callback_();
      });
}

fxl::WeakPtr<Handler> Handler::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

}  // namespace exceptions
}  // namespace forensics

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/exception_handler/handler.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/forensics/exceptions/exception_handler/component_lookup.h"
#include "src/developer/forensics/exceptions/exception_handler/crash_reporter.h"
#include "src/developer/forensics/exceptions/exception_handler/minidump.h"
#include "src/developer/forensics/exceptions/exception_handler/report_builder.h"
#include "src/developer/forensics/utils/fit/timeout.h"
#include "src/lib/fsl/handles/object_info.h"

namespace forensics {
namespace exceptions {

using FileCrashReportResult = fuchsia::feedback::CrashReporter_File_Result;
using fuchsia::feedback::CrashReport;
using fuchsia::sys::internal::SourceIdentity;

::fit::promise<> Handle(zx::exception exception, async_dispatcher_t* dispatcher,
                        std::shared_ptr<sys::ServiceDirectory> services,
                        const zx::duration component_lookup_timeout,
                        const zx::duration crash_reporter_timeout) {
  zx::process process;
  if (const zx_status_t status = exception.get_process(&process); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to get process";
  }

  const std::string process_name = fsl::GetObjectName(process.get());
  const zx_koid_t process_koid = fsl::GetKoid(process.get());
  if (process_koid == 0) {
    FX_LOGS(ERROR) << "Failed to get process koid";
  }

  // Put the exception on the heap so it can be used by the promises.
  auto exception_ptr = std::make_shared<zx::exception>(std::move(exception));

  return ::fit::join_promises(
             GetComponentSourceIdentity(dispatcher, services,
                                        fit::Timeout(component_lookup_timeout), process_koid),
             GenerateMinidumpVMO(*exception_ptr))
      .and_then([process_name, exception_ptr](
                    std::tuple<::fit::result<SourceIdentity>, ::fit::result<zx::vmo>>& results) {
        // Reset the exception after having generated the minidump.
        exception_ptr->reset();

        SourceIdentity component_info;
        if (std::get<0>(results).is_ok()) {
          component_info = std::get<0>(results).take_value();
        }

        zx::vmo minidump_vmo;
        if (std::get<1>(results).is_ok()) {
          minidump_vmo = std::get<1>(results).take_value();
        }

        CrashReportBuilder builder;

        builder.SetProcessName(process_name).SetComponentInfo(component_info);
        if (minidump_vmo.is_valid()) {
          builder.SetMinidump(std::move(minidump_vmo));
        }

        return ::fit::ok(builder.Consume());
      })
      .then(
          [dispatcher, services, crash_reporter_timeout](::fit::result<CrashReport>& crash_report) {
            return FileCrashReport(dispatcher, services, fit::Timeout(crash_reporter_timeout),
                                   crash_report.take_value());
          });
}

// Handles asynchronously filing a crash report for a given program.
::fit::promise<> Handle(const std::string& process_name, zx_koid_t process_koid,
                        async_dispatcher_t* dispatcher,
                        std::shared_ptr<sys::ServiceDirectory> services,
                        zx::duration component_lookup_timeout,
                        zx::duration crash_reporter_timeout) {
  return GetComponentSourceIdentity(dispatcher, services, fit::Timeout(component_lookup_timeout),
                                    process_koid)
      .then([process_name](::fit::result<SourceIdentity>& result) {
        SourceIdentity component_info;
        if (result.is_ok()) {
          component_info = result.take_value();
        }

        CrashReportBuilder builder;

        builder.SetProcessName(process_name).SetComponentInfo(component_info);
        builder.SetExceptionExpired();

        return ::fit::ok(builder.Consume());
      })
      .then(
          [dispatcher, services, crash_reporter_timeout](::fit::result<CrashReport>& crash_report) {
            return FileCrashReport(dispatcher, services, fit::Timeout(crash_reporter_timeout),
                                   crash_report.take_value());
          });
}

}  // namespace exceptions
}  // namespace forensics

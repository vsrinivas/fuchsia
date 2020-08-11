// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/handler/handler.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/forensics/exceptions/handler/component_lookup.h"
#include "src/developer/forensics/exceptions/handler/crash_reporter.h"
#include "src/developer/forensics/exceptions/handler/minidump.h"
#include "src/developer/forensics/exceptions/handler/report_builder.h"
#include "src/developer/forensics/utils/fit/timeout.h"
#include "src/lib/fsl/handles/object_info.h"

namespace forensics {
namespace exceptions {
namespace handler {
namespace {

using FileCrashReportResult = fuchsia::feedback::CrashReporter_File_Result;
using fuchsia::feedback::CrashReport;
using fuchsia::sys::internal::SourceIdentity;

::fit::promise<> Handle(CrashReportBuilder builder, const zx_koid_t process_koid,
                        async_dispatcher_t* dispatcher,
                        std::shared_ptr<sys::ServiceDirectory> services,
                        const zx::duration component_lookup_timeout,
                        const zx::duration crash_reporter_timeout) {
  return GetComponentSourceIdentity(dispatcher, services, fit::Timeout(component_lookup_timeout),
                                    process_koid)
      .then([builder = std::move(builder)](::fit::result<SourceIdentity>& result) mutable {
        SourceIdentity component_info;
        if (result.is_ok()) {
          component_info = result.take_value();
        }

        builder.SetComponentInfo(component_info);

        return ::fit::ok(builder.Consume());
      })
      .then(
          [dispatcher, services, crash_reporter_timeout](::fit::result<CrashReport>& crash_report) {
            return FileCrashReport(dispatcher, services, fit::Timeout(crash_reporter_timeout),
                                   crash_report.take_value());
          });
}

}  // namespace

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

  CrashReportBuilder builder;
  builder.SetProcessName(process_name);

  // We only need the exception to generate the minidump – after that we can release it.
  zx::vmo minidump = GenerateMinidump(std::move(exception));
  if (minidump.is_valid()) {
    builder.SetMinidump(std::move(minidump));
  }

  return Handle(std::move(builder), process_koid, dispatcher, services, component_lookup_timeout,
                crash_reporter_timeout);
}

// Handles asynchronously filing a crash report for a given program.
::fit::promise<> Handle(const std::string& process_name, const zx_koid_t process_koid,
                        async_dispatcher_t* dispatcher,
                        std::shared_ptr<sys::ServiceDirectory> services,
                        zx::duration component_lookup_timeout,
                        zx::duration crash_reporter_timeout) {
  CrashReportBuilder builder;
  builder.SetProcessName(process_name);
  builder.SetExceptionExpired();
  return Handle(std::move(builder), process_koid, dispatcher, services, component_lookup_timeout,
                crash_reporter_timeout);
}

}  // namespace handler
}  // namespace exceptions
}  // namespace forensics

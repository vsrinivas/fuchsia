// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/handler/crash_reporter.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/forensics/exceptions/handler/component_lookup.h"
#include "src/developer/forensics/exceptions/handler/minidump.h"
#include "src/developer/forensics/exceptions/handler/report_builder.h"
#include "src/developer/forensics/utils/fit/timeout.h"
#include "src/lib/fsl/handles/object_info.h"

namespace forensics {
namespace exceptions {
namespace handler {

using fuchsia::feedback::CrashReport;
using fuchsia::sys::internal::SourceIdentity;

CrashReporter::CrashReporter(async_dispatcher_t* dispatcher,
                             std::shared_ptr<sys::ServiceDirectory> services,
                             zx::duration component_lookup_timeout)
    : dispatcher_(dispatcher),
      executor_(dispatcher_),
      services_(services),
      component_lookup_timeout_(component_lookup_timeout) {}

void CrashReporter::Send(const std::string crashed_process_name,
                         const zx_koid_t crashed_process_koid, zx::exception exception,
                         SendCallback callback) {
  CrashReportBuilder builder;
  builder.SetProcessName(crashed_process_name);

  if (exception.is_valid()) {
    // We only need the exception to generate the minidump – after that we can release it.
    zx::vmo minidump = GenerateMinidump(std::move(exception));
    if (minidump.is_valid()) {
      builder.SetMinidump(std::move(minidump));
    }
  } else {
    builder.SetExceptionExpired();
  }

  auto file_crash_report =
      GetComponentSourceIdentity(dispatcher_, services_, fit::Timeout(component_lookup_timeout_),
                                 crashed_process_koid)
          .then([services = services_, builder = std::move(builder),
                 callback = std::move(callback)](::fit::result<SourceIdentity>& result) mutable {
            SourceIdentity component_lookup;
            if (result.is_ok()) {
              component_lookup = result.take_value();
            }

            builder.SetComponentInfo(component_lookup);

            // We make a fire-and-forget request as we won't do anything with the result.
            auto crash_reporter = services->Connect<fuchsia::feedback::CrashReporter>();
            crash_reporter->File(builder.Consume(),
                                 [](fuchsia::feedback::CrashReporter_File_Result result) {});

            callback();

            return ::fit::ok();
          });

  executor_.schedule_task(std::move(file_crash_report));
}

}  // namespace handler
}  // namespace exceptions
}  // namespace forensics

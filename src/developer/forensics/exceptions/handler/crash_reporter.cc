// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/handler/crash_reporter.h"

#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/exception.h>

#include "src/developer/forensics/exceptions/handler/component_lookup.h"
#include "src/developer/forensics/exceptions/handler/minidump.h"
#include "src/developer/forensics/exceptions/handler/report_builder.h"
#include "src/developer/forensics/utils/fit/timeout.h"
#include "src/lib/fsl/handles/object_info.h"

namespace forensics {
namespace exceptions {
namespace handler {
namespace {

// Either resets the exception immediately if the process only has one thread or with a 5s delay
// otherwise.
void ResetException(async_dispatcher_t* dispatcher, zx::exception exception,
                    const zx::process& process) {
  if (!process.is_valid()) {
    FX_LOGS(ERROR) << "Process for exception is invalid";
    exception.reset();
    return;
  }

  size_t num_threads{0};
  if (const zx_status_t status = zx_object_get_info(process.get(), ZX_INFO_PROCESS_THREADS, nullptr,
                                                    0u, nullptr, &num_threads);
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to get thread info from process " << process.get();
    exception.reset();
    return;
  }

  if (num_threads > 1) {
    // If the process has multiple threads, delay resetting |exception| for 5 seconds. If one of the
    // other threads is in an exception, releasing |exception| immediately may result in the process
    // being terminated by the kernel before the minidump for the other thread is generated.
    async::PostDelayedTask(
        dispatcher, [exception = std::move(exception)]() mutable { exception.reset(); },
        zx::sec(5));
  } else {
    exception.reset();
  }
}

}  // namespace

using fuchsia::feedback::CrashReport;
using fuchsia::sys::internal::SourceIdentity;

CrashReporter::CrashReporter(async_dispatcher_t* dispatcher,
                             std::shared_ptr<sys::ServiceDirectory> services,
                             zx::duration component_lookup_timeout)
    : dispatcher_(dispatcher),
      executor_(dispatcher_),
      services_(services),
      component_lookup_timeout_(component_lookup_timeout) {}

void CrashReporter::Send(zx::exception exception, zx::process crashed_process,
                         zx::thread crashed_thread, SendCallback callback) {
  CrashReportBuilder builder;
  builder.SetProcess(crashed_process).SetThread(crashed_thread);

  if (exception.is_valid()) {
    zx::vmo minidump = GenerateMinidump(exception);
    ResetException(dispatcher_, std::move(exception), crashed_process);

    if (minidump.is_valid()) {
      builder.SetMinidump(std::move(minidump));
    } else {
      builder.SetProcessTerminated();
    }
  } else {
    builder.SetExceptionExpired();
  }

  auto file_crash_report =
      GetComponentSourceIdentity(dispatcher_, services_, fit::Timeout(component_lookup_timeout_),
                                 fsl::GetKoid(crashed_thread.get()))
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

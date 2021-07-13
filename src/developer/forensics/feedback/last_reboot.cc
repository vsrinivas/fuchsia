// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/last_reboot.h"

namespace forensics::feedback {

LastReboot::LastReboot(async_dispatcher_t* dispatcher,
                       std::shared_ptr<sys::ServiceDirectory> services, cobalt::Logger* cobalt,
                       const Options options)
    : dispatcher_(dispatcher),
      reboot_watcher_(services, options.graceful_reboot_reason_write_path, cobalt),
      reporter_(dispatcher, services, cobalt),
      last_reboot_info_provider_(options.reboot_log) {
  reboot_watcher_.Connect();
  if (options.is_first_instance) {
    const zx::duration delay = (options.reboot_log.RebootReason() == RebootReason::kOOM)
                                   ? options.oom_crash_reporting_delay
                                   : zx::sec(0);
    reporter_.ReportOn(options.reboot_log, delay);
  }
}

void LastReboot::LastReboot::Handle(
    ::fidl::InterfaceRequest<fuchsia::feedback::LastRebootInfoProvider> request,
    ::fit::function<void(zx_status_t)> error_handler) {
  last_reboot_info_provider_connections_.AddBinding(&last_reboot_info_provider_, std::move(request),
                                                    dispatcher_, std::move(error_handler));
}

}  // namespace forensics::feedback

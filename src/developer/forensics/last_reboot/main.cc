// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <string>

#include "src/developer/forensics/last_reboot/main_service.h"
#include "src/developer/forensics/utils/component/component.h"
#include "src/developer/forensics/utils/previous_boot_file.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"

namespace forensics {
namespace last_reboot {
namespace {

constexpr char kGracefulRebootReasonFile[] = "graceful_reboot_reason.txt";
constexpr char kNotAFdr[] = "/data/not_a_fdr.txt";

void SetNotAFdr() {
  if (files::IsFile(kNotAFdr)) {
    return;
  }

  if (!files::WriteFile(kNotAFdr, "", 0u)) {
    FX_LOGS(ERROR) << "Failed to create " << kNotAFdr;
  }
}

}  // namespace

int main() {
  syslog::SetTags({"forensics", "reboot"});

  component::Component component;
  PreviousBootFile reboot_reason_file =
      PreviousBootFile::FromCache(component.IsFirstInstance(), kGracefulRebootReasonFile);

  MainService main_service(MainService::Config{
      .dispatcher = component.Dispatcher(),
      .services = component.Services(),
      .root_node = component.InspectRoot(),
      .reboot_log = RebootLog::ParseRebootLog("/boot/log/last-panic.txt",
                                              reboot_reason_file.PreviousBootPath(), kNotAFdr),
      .graceful_reboot_reason_write_path = reboot_reason_file.CurrentBootPath(),
  });

  // The "no-FDR" marker needs to be written after parsing the reboot log as its absence may
  // indicate a reboot due to FDR.
  SetNotAFdr();

  // fuchsia.feedback.LastRebootInfoProvider
  component.AddPublicService(
      ::fidl::InterfaceRequestHandler<fuchsia::feedback::LastRebootInfoProvider>(
          [&main_service](
              ::fidl::InterfaceRequest<fuchsia::feedback::LastRebootInfoProvider> request) {
            main_service.HandleLastRebootInfoProviderRequest(std::move(request));
          }));

  main_service.WatchForImminentGracefulReboot();

  if (component.IsFirstInstance()) {
    // We file the crash report with a 90s delay to increase the likelihood that Inspect data (at
    // all and specifically the data from memory_monitor) is included in the snapshot.zip generated
    // by the Feedback service. The memory_monitor Inspect data is critical to debug OOM crash
    // reports.
    // TODO(fxbug.dev/46216, fxbug.dev/48485): remove delay.
    main_service.Report(/*crash_reporting_delay=*/zx::sec(90));
  }

  component.RunLoop();

  return EXIT_SUCCESS;
}

}  // namespace last_reboot
}  // namespace forensics

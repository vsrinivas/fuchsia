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

constexpr char kPreviousGracefulRebootReasonFile[] = "/tmp/graceful_reboot_reason.txt";
constexpr char kCurrentGracefulRebootReasonFile[] = "/cache/graceful_reboot_reason.txt";
constexpr char kNotAFdr[] = "/data/not_a_fdr.txt";

// Return whether |kNotAFdr| existed in the file system and create it otherwise.
bool TestAndSetNotAFdr() {
  if (files::IsFile(kNotAFdr)) {
    return true;
  }

  if (!files::WriteFile(kNotAFdr, "", 0u)) {
    FX_LOGS(ERROR) << "Failed to create " << kNotAFdr;
  }

  return false;
}

void MovePreviousRebootReason() {
  // Bail if the file doesn't exist.
  if (!files::IsFile(kCurrentGracefulRebootReasonFile)) {
    return;
  }

  // Bail if the file can't be read.
  std::string content;
  if (!files::ReadFileToString(kCurrentGracefulRebootReasonFile, &content)) {
    FX_LOGS(ERROR) << "Failed to read file " << kCurrentGracefulRebootReasonFile;
    return;
  }

  // Copy the file content – we cannot move as the two files are under different namespaces.
  if (!files::WriteFile(kPreviousGracefulRebootReasonFile, content)) {
    FX_LOGS(ERROR) << "Failed to write file " << kPreviousGracefulRebootReasonFile;
    return;
  }

  // Delete the original file.
  if (!files::DeletePath(kCurrentGracefulRebootReasonFile, /*recursive=*/true)) {
    FX_LOGS(ERROR) << "Failed to delete " << kCurrentGracefulRebootReasonFile;
  }
}

}  // namespace

int main() {
  syslog::SetTags({"forensics", "reboot"});

  component::Component component;

  if (component.IsFirstInstance()) {
    MovePreviousRebootReason();
  }

  MainService main_service(MainService::Config{
      .dispatcher = component.Dispatcher(),
      .services = component.Services(),
      .clock = component.Clock(),
      .root_node = component.InspectRoot(),
      .reboot_log = feedback::RebootLog::ParseRebootLog(
          "/boot/log/last-panic.txt", kPreviousGracefulRebootReasonFile, TestAndSetNotAFdr()),
      .graceful_reboot_reason_write_path = kCurrentGracefulRebootReasonFile,
  });

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
    main_service.Report(/*oom_crash_reporting_delay=*/zx::sec(90));
  }

  component.RunLoop();

  return EXIT_SUCCESS;
}

}  // namespace last_reboot
}  // namespace forensics

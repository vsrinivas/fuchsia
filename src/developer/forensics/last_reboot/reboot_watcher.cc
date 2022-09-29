// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/last_reboot/reboot_watcher.h"

#include <fcntl.h>
#include <lib/fit/defer.h>

#include <fbl/unique_fd.h>

#include "src/developer/forensics/feedback/reboot_log/graceful_reboot_reason.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"
#include "src/lib/files/file.h"
#include "src/lib/files/file_descriptor.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace last_reboot {

ImminentGracefulRebootWatcher::ImminentGracefulRebootWatcher(
    std::shared_ptr<sys::ServiceDirectory> services, const std::string& path,
    cobalt::Logger* cobalt)
    : services_(services), path_(path), cobalt_(cobalt), connection_(this) {
  // TODO(fxbug.dev/52187): Reconnect if the error handler runs.
  connection_.set_error_handler([](zx_status_t status) {
    FX_PLOGS(WARNING, status) << "Lost connection to client of "
                                 "fuchsia.hardware.power.statecontrol.RebootMethodsWatcher";
  });
}

void ImminentGracefulRebootWatcher::Connect() {
  // We register ourselves with RebootMethodsWatcher using a fire-and-forget request that gives an
  // endpoint to a long-lived connection we maintain.
  auto reboot_watcher_register =
      services_->Connect<fuchsia::hardware::power::statecontrol::RebootMethodsWatcherRegister>();

  // |connection_| is bound with a fire-and-forget request that ignores the ack because failures
  // aren't expected unless the system is in a dire state.
  reboot_watcher_register->RegisterWithAck(connection_.NewBinding(), [] {});
}

void ImminentGracefulRebootWatcher::OnReboot(
    fuchsia::hardware::power::statecontrol::RebootReason reason, OnRebootCallback callback) {
  auto on_done = fit::defer([this, callback = std::move(callback)] {
    callback();
    connection_.Unbind();
  });

  const std::string content = feedback::ToFileContent(feedback::ToGracefulRebootReason(reason));
  FX_LOGS(INFO) << "Received reboot reason  '" << content << "' ";

  const size_t timer_id = cobalt_->StartTimer();

  fbl::unique_fd fd(open(path_.c_str(), O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR));
  if (!fd.is_valid()) {
    FX_LOGS(INFO) << "Failed to open reboot reason file: " << path_;
    return;
  }

  if (fxl::WriteFileDescriptor(fd.get(), content.data(), content.size())) {
    cobalt_->LogElapsedTime(cobalt::RebootReasonWriteResult::kSuccess, timer_id);
  } else {
    cobalt_->LogElapsedTime(cobalt::RebootReasonWriteResult::kFailure, timer_id);
    FX_LOGS(ERROR) << "Failed to write reboot reason '" << content << "' to " << path_;
  }

  // Force the flush as we want to persist the content asap and we don't have more content to
  // write.
  fsync(fd.get());
}

}  // namespace last_reboot
}  // namespace forensics

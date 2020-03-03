// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/feedback_agent.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/default.h>
#include <lib/fdio/spawn.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/processargs.h>
#include <zircon/types.h>

#include <cinttypes>

#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/developer/feedback/utils/rotating_file_set.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace feedback {

std::unique_ptr<FeedbackAgent> FeedbackAgent::TryCreate(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    inspect::Node* root_node) {
  std::unique_ptr<feedback::DataProvider> data_provider =
      feedback::DataProvider::TryCreate(dispatcher, services);
  if (!data_provider) {
    FX_LOGS(FATAL) << "Failed to set up feedback agent";
    return nullptr;
  }

  return std::make_unique<FeedbackAgent>(dispatcher, root_node, std::move(data_provider));
}

namespace {

void MovePreviousLogs() {
  RotatingFileSetReader log_reader(kCurrentLogsFilePaths);

  if (log_reader.Concatenate(kPreviousLogsFilePath)) {
    FX_LOGS(INFO) << "Found logs from previous boot, available at " << kPreviousLogsFilePath;
  } else {
    FX_LOGS(ERROR) << "No logs found from previous boot";
  }

  // Clean up all of the previous log files now that they have been concatenated into a single
  // in-memory file.
  for (const auto& file : kCurrentLogsFilePaths) {
    files::DeletePath(file, /*recursive=*/false);
  }
}

}  // namespace

FeedbackAgent::FeedbackAgent(async_dispatcher_t* dispatcher, inspect::Node* root_node,
                             std::unique_ptr<DataProvider> data_provider)
    : dispatcher_(dispatcher),
      inspect_manager_(root_node),
      data_provider_(std::move(data_provider)) {
  // We need to move the logs from the previous boot before spawning the system log recorder process
  // so that the new process doesn't overwrite the old logs. Additionally, to guarantee the data
  // providers see the complete previous logs, this needs to be done before spawning any data
  // providers to avoid parallel attempts to read and write the previous logs file.
  MovePreviousLogs();
}

void FeedbackAgent::SpawnSystemLogRecorder() {
  zx_handle_t process;
  const char* argv[] = {/*process_name=*/"system_log_recorder", nullptr};
  if (const zx_status_t status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                                            "/pkg/bin/system_log_recorder", argv, &process);
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to spawn system log recorder, logs will not be persisted";
  }
}

void FeedbackAgent::HandleDataProviderRequest(
    fidl::InterfaceRequest<fuchsia::feedback::DataProvider> request) {
  const std::string connection_identifier =
      fxl::StringPrintf("(connection %03" PRIu64 ")", next_data_provider_connection_id_++);
  FX_LOGS(INFO) << "Client opened a new connection to fuchsia.feedback.DataProvider "
                << connection_identifier;
  data_provider_connections_.AddBinding(
      data_provider_.get(), std::move(request), dispatcher_,
      [this, connection_identifier](const zx_status_t status) {
        if (status == ZX_ERR_PEER_CLOSED) {
          FX_LOGS(INFO) << "Client closed their connection to fuchsia.feedback.DataProvider "
                        << connection_identifier;
        }
        inspect_manager_.DecrementCurrentNumDataProviderConnections();
      });
  inspect_manager_.IncrementNumDataProviderConnections();
}
}  // namespace feedback

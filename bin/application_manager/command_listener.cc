// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/application_manager/command_listener.h"

#include <utility>

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/split_string.h"

namespace modular {

CommandListener::CommandListener(ApplicationLauncher* launcher,
                                 mx::channel command_channel)
    : launcher_(launcher),
      message_loop_(mtl::MessageLoop::GetCurrent()),
      command_channel_(std::move(command_channel)) {
  FTL_DCHECK(launcher_);
  FTL_DCHECK(command_channel_);

  handler_key_ = message_loop_->AddHandler(
      this, command_channel_.get(), MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
      ftl::TimeDelta::Max());
}

CommandListener::~CommandListener() {
  Close();
};

void CommandListener::OnHandleReady(mx_handle_t handle, mx_signals_t pending) {
  if (pending & MX_SIGNAL_READABLE) {
    uint32_t num_bytes = 0;
    uint32_t num_handles = 0;
    mx_status_t status = command_channel_.read(0, nullptr, 0, &num_bytes,
                                               nullptr, 0, &num_handles);
    if (status == ERR_BUFFER_TOO_SMALL && num_handles == 0) {
      std::vector<char> bytes(num_bytes);
      status = command_channel_.read(0, bytes.data(), num_bytes, &num_bytes,
                                     nullptr, 0, &num_handles);
      FTL_CHECK(status == NO_ERROR);
      ExecuteCommand(std::string(bytes.data(), bytes.size()));
      return;
    }
    FTL_LOG(ERROR) << "Closing command channel due to read error.";
  } else {
    FTL_DCHECK(pending & MX_SIGNAL_PEER_CLOSED);
  }
  Close();
}

void CommandListener::ExecuteCommand(std::string command) {
  // TODO(jeffbrown): We should probably leave tokenization up to the shell
  // which is invoking the command listener.
  std::vector<std::string> args = ftl::SplitStringCopy(
      command, " ", ftl::kTrimWhitespace, ftl::kSplitWantNonEmpty);
  if (args.size() < 2 || args[0] != "@")
    return;

  auto launch_info = ApplicationLaunchInfo::New();
  launch_info->url = args[1];
  for (size_t i = 2; i < args.size(); ++i)
    launch_info->arguments.push_back(args[i]);
  launcher_->CreateApplication(std::move(launch_info), nullptr);
}

void CommandListener::Close() {
  if (command_channel_) {
    message_loop_->RemoveHandler(handler_key_);
    command_channel_.reset();
  }
}

}  // namespace modular

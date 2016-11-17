// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/application_manager/command_listener.h"

#include <sstream>
#include <utility>

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/split_string.h"
#include "lib/ftl/strings/string_view.h"

namespace modular {

CommandListener::CommandListener(ApplicationEnvironmentImpl* root_environment,
                                 mx::channel command_channel)
    : root_environment_(root_environment),
      message_loop_(mtl::MessageLoop::GetCurrent()),
      command_channel_(std::move(command_channel)) {
  FTL_DCHECK(root_environment_);
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
  // TODO(jeffbrown): It would be better to implement this as a little
  // shell program so that we can write output directly to the console.
  // Unfortunately we can only reach application manager through a wormhole
  // in devmgr right now.  Later we should make an IDL for debug inspection.

  // Parse commands of the form "@<scope> <uri> <args>" or "@<scope>?"
  std::vector<std::string> args = ftl::SplitStringCopy(
      command, " ", ftl::kTrimWhitespace, ftl::kSplitWantNonEmpty);
  if (args.empty() || args[0][0] != '@') {
    Usage();
    return;
  }
  bool is_query = (args[0][args[0].size() - 1]) == '?';
  ftl::StringView scope(args[0].c_str() + 1,
                        args[0].size() - (is_query ? 2 : 1));

  // Find environment by scope.
  ApplicationEnvironmentImpl* environment = FindEnvironment(scope);
  if (!environment) {
    FTL_LOG(ERROR) << "Could not find environment: " << scope;
    return;
  }

  // Perform query.
  if (is_query) {
    std::stringstream s;
    environment->Describe(s);
    FTL_LOG(INFO) << "Information about '" << scope << "':" << std::endl
                  << s.str();
    return;
  }

  // Execute command.
  if (args.size() < 2) {
    Usage();
    return;
  }
  auto launch_info = ApplicationLaunchInfo::New();
  launch_info->url = args[1];
  for (size_t i = 2; i < args.size(); ++i)
    launch_info->arguments.push_back(args[i]);
  environment->CreateApplication(std::move(launch_info), nullptr);
}

ApplicationEnvironmentImpl* CommandListener::FindEnvironment(
    ftl::StringView scope) {
  // TODO(jeffbrown): It would be nice to support scoping by environment path
  // in case of ambiguity among labels.
  if (scope.empty())
    return root_environment_;
  return root_environment_->FindByLabel(scope);
}

void CommandListener::Usage() {
  FTL_LOG(INFO)
      << "Usage:" << std::endl
      << "  @ <uri> <args> : run app in root environment" << std::endl
      << "  @?             : get info about root environment" << std::endl
      << "  @<scope> <uri> <args> : run app in environment <scope>" << std::endl
      << "  @<scope>?             : get info about environment <scope>"
      << std::endl;
}

void CommandListener::Close() {
  if (command_channel_) {
    message_loop_->RemoveHandler(handler_key_);
    command_channel_.reset();
  }
}

}  // namespace modular

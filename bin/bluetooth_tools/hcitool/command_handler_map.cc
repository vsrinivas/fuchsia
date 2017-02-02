// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_handler_map.h"

#include <iostream>

namespace hcitool {

CommandHandlerMap::CommandHandlerMap(
    bluetooth::hci::CommandChannel* cmd_channel,
    ftl::RefPtr<ftl::TaskRunner> task_runner)
    : cmd_channel_(cmd_channel), task_runner_(task_runner) {
  FTL_DCHECK(cmd_channel_);
  FTL_DCHECK(task_runner_.get());
}

void CommandHandlerMap::RegisterHandler(
    const std::string& name,
    std::unique_ptr<CommandHandler> handler) {
  FTL_DCHECK(!name.empty());
  handler_map_[name] = std::move(handler);
}

bool CommandHandlerMap::ExecuteCommand(const std::vector<std::string>& argv,
                                       const ftl::Closure& complete_cb,
                                       bool* out_cmd_found) {
  FTL_DCHECK(out_cmd_found);

  *out_cmd_found = false;

  if (argv.empty())
    return false;

  const auto& iter = handler_map_.find(argv[0]);
  if (iter == handler_map_.end())
    return false;

  *out_cmd_found = true;

  return iter->second->Run(argv, complete_cb);
}

void CommandHandlerMap::DescribeAllCommands() {
  for (const auto& iter : handler_map_) {
    std::cout << "    " << iter.second->GetHelpMessage() << std::endl;
  }
}

}  // namespace hcitool

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_dispatcher.h"

#include <stdio.h>

namespace hcitool {

CommandDispatcher::CommandDispatcher(
    bluetooth::hci::CommandChannel* cmd_channel,
    ftl::RefPtr<ftl::TaskRunner> task_runner)
    : cmd_channel_(cmd_channel), task_runner_(task_runner) {
  FTL_DCHECK(cmd_channel_);
  FTL_DCHECK(task_runner_.get());
}

bool CommandDispatcher::ExecuteCommand(const std::vector<std::string>& argv,
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

  auto cl = ftl::CommandLineFromIterators(argv.begin(), argv.end());
  return iter->second.second(*this, cl, complete_cb);
}

void CommandDispatcher::DescribeAllCommands() {
  for (const auto& iter : handler_map_) {
    printf("  %-30s %s\n", iter.first.c_str(), iter.second.first.c_str());
  }
}

void CommandDispatcher::RegisterHandler(const std::string& name,
                                        const std::string& description,
                                        const CommandHandler& handler) {
  FTL_DCHECK(!name.empty());
  FTL_DCHECK(!description.empty());
  FTL_DCHECK(handler_map_.find(name) == handler_map_.end());

  handler_map_[name] = std::make_pair<>(description, handler);
}

}  // namespace hcitool

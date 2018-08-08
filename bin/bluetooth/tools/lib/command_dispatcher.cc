// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_dispatcher.h"

#include <algorithm>
#include <cstdio>

#include "lib/fxl/logging.h"

namespace bluetooth_tools {

CommandDispatcher::CommandHandlerData::CommandHandlerData(
    const std::string& description, CommandHandler handler)
    : description(description), handler(std::move(handler)) {}

bool CommandDispatcher::ExecuteCommand(const std::vector<std::string>& argv,
                                       fit::closure complete_cb,
                                       bool* out_cmd_found) {
  FXL_DCHECK(out_cmd_found);

  *out_cmd_found = false;

  if (argv.empty()) {
    return false;
  }

  const auto& iter = handler_map_.find(argv[0]);
  if (iter == handler_map_.end()) {
    return false;
  }

  *out_cmd_found = true;

  auto cl = fxl::CommandLineFromIterators(argv.begin(), argv.end());
  return iter->second.handler(cl, std::move(complete_cb));
}

void CommandDispatcher::DescribeAllCommands() const {
  for (const auto& iter : handler_map_) {
    std::printf("  %-20s %s\n", iter.first.c_str(),
                iter.second.description.c_str());
  }
}

void CommandDispatcher::RegisterHandler(const std::string& name,
                                        const std::string& description,
                                        CommandHandler handler) {
  FXL_DCHECK(!name.empty());
  FXL_DCHECK(!description.empty());
  FXL_DCHECK(handler_map_.find(name) == handler_map_.end());

  handler_map_[name] = CommandHandlerData(description, std::move(handler));
}

std::vector<std::string> CommandDispatcher::GetCommandsThatMatch(
    const std::string& prefix) const {
  std::vector<std::string> result;
  for (auto& iter : handler_map_) {
    auto& cmd_name = iter.first;
    if (prefix.length() > cmd_name.length()) {
      continue;
    }
    if (cmd_name.compare(0, prefix.length(), prefix) == 0) {
      result.push_back(cmd_name);
    }
  }
  return result;
}

}  // namespace bluetooth_tools

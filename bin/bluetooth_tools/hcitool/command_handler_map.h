// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <string>
#include <vector>

#include "lib/ftl/macros.h"

#include "command_handler.h"

namespace hcitool {

class CommandHandlerMap final {
 public:
  CommandHandlerMap(bluetooth::hci::CommandChannel* cmd_channel,
                    ftl::RefPtr<ftl::TaskRunner> task_runner);
  void RegisterHandler(const std::string& name,
                       std::unique_ptr<CommandHandler> handler);
  bool ExecuteCommand(const std::vector<std::string>& argv,
                      const ftl::Closure& complete_cb,
                      bool* out_cmd_found);
  void DescribeAllCommands();

  bluetooth::hci::CommandChannel* cmd_channel() const { return cmd_channel_; }
  ftl::RefPtr<ftl::TaskRunner> task_runner() const { return task_runner_; }

 private:
  std::map<std::string, std::unique_ptr<CommandHandler>> handler_map_;
  bluetooth::hci::CommandChannel* cmd_channel_;
  ftl::RefPtr<ftl::TaskRunner> task_runner_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CommandHandlerMap);
};

}  // namespace hcitool

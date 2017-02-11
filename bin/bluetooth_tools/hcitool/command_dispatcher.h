// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "lib/ftl/command_line.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/tasks/task_runner.h"

namespace bluetooth {
namespace hci {

class CommandChannel;

}  // namespace hci
}  // namespace bluetooth

namespace hcitool {

class CommandDispatcher final {
 public:
  CommandDispatcher(bluetooth::hci::CommandChannel* cmd_channel,
                    ftl::RefPtr<ftl::TaskRunner> task_runner);
  bool ExecuteCommand(const std::vector<std::string>& argv,
                      const ftl::Closure& complete_cb,
                      bool* out_cmd_found);
  void DescribeAllCommands();

  // A command handler implementation.
  // |owner|: A const reference to this map. Provides const getters for the
  //          hci::CommandChannel and the TaskRunner belonging to the tool's
  //          main MessageLoop.
  // |cmd_line|: The command line parser for the argument vector of the command
  //             being handled.
  // |complete_cb|: The callback that must be invoked when the command
  //                transaction is complete.
  //
  // Must return true if the command was handled successfully. False otherwise.
  using CommandHandler = std::function<bool(const CommandDispatcher& owner,
                                            const ftl::CommandLine& cmd_line,
                                            const ftl::Closure& complete_cb)>;

  // Registers a command handler for the given command name.
  void RegisterHandler(const std::string& command_name,
                       const std::string& description,
                       const CommandHandler& handler);

  bluetooth::hci::CommandChannel* cmd_channel() const { return cmd_channel_; }
  ftl::RefPtr<ftl::TaskRunner> task_runner() const { return task_runner_; }

 private:
  // The first field in the pair stores the command description. The second
  // field is the handler function.
  using CommandHandlerData = std::pair<std::string, CommandHandler>;
  std::map<std::string, CommandHandlerData> handler_map_;

  bluetooth::hci::CommandChannel* cmd_channel_;
  ftl::RefPtr<ftl::TaskRunner> task_runner_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CommandDispatcher);
};

}  // namespace hcitool

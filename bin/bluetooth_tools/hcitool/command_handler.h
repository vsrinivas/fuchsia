// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "apps/bluetooth/hci/command_channel.h"
#include "apps/bluetooth/hci/hci_constants.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"

namespace hcitool {

// TODO(armansito): The logic in this base class could be squashed into
// CommandHandlerMap and each handler could just be a std::function rather than
// a functor class which would obviate the ugly HCITOOL_DEFINE_HANDLER macro
// below. Revisit this later.

// Base class for handlers of hcitool commands.
class CommandHandler {
 public:
  // Each CommandHandler is initialized with a |cmd_channel|, which is used for
  // sending and receiving HCI commands and events, and a |task_runner| on which
  // the command event callbacks should be posted.
  CommandHandler(bluetooth::hci::CommandChannel* cmd_channel,
                 ftl::RefPtr<ftl::TaskRunner> task_runner);
  virtual ~CommandHandler() = default;

  // Called when a command is about to be executed. |argv| contains the command
  // arguments, e.g. if the user supplied "write-local-name foo", argv would
  // be equal to {"write-local-name", "foo"}.
  //
  // |complete_cb| is the callback that must be executed by a command handler
  // when the associated command has completed.
  bool Run(const std::vector<std::string>& argv,
           const ftl::Closure& complete_cb);

  // Implementations must return an informative help message that describes the
  // command, e.g. "write-local-name <name> - Sends HCI_Write_Local_Name".
  virtual std::string GetHelpMessage() const = 0;

 protected:
  using OptionMap =
      std::function<bool(const std::string& name, std::string* out_value)>;

  // Must implement the body of a command handler.
  //
  // |positional_args|: positional args of the command, not including the
  //                    command name itself.
  // |option_count|: Number of command-line options.
  // |options|: Closure that returns the value for an option, if it exists.
  // |complete_cb|: The callback that must be invoked when the command
  //                transaction is complete.
  virtual bool HandleCommand(const std::vector<std::string>& positional_args,
                             size_t option_count,
                             const OptionMap& options,
                             const ftl::Closure& complete_cb) = 0;

  // Returns a default handler for HCI_CommandStatus events that can be passed
  // to CommandChannel::SendCommand.
  bluetooth::hci::CommandChannel::CommandStatusCallback DefaultStatusCallback(
      const ftl::Closure& complete_cb) const;

  bluetooth::hci::CommandChannel* cmd_channel() const { return cmd_channel_; }
  ftl::RefPtr<ftl::TaskRunner> task_runner() const { return task_runner_; }

 private:
  bluetooth::hci::CommandChannel* cmd_channel_;
  ftl::RefPtr<ftl::TaskRunner> task_runner_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CommandHandler);
};

// Macro for defining a CommandHandler subclass.
#define HCITOOL_DEFINE_HANDLER(handler, cmd_name)                       \
  class handler final : public CommandHandler {                         \
   public:                                                              \
    handler(bluetooth::hci::CommandChannel* cmd_channel,                \
            ftl::RefPtr<ftl::TaskRunner> task_runner)                   \
        : CommandHandler(cmd_channel, task_runner) {}                   \
    std::string GetHelpMessage() const override;                        \
    bool HandleCommand(const std::vector<std::string>& positional_args, \
                       size_t option_count,                             \
                       const OptionMap& options,                        \
                       const ftl::Closure& complete_cb) override;       \
    static inline std::string GetCommandName() { return cmd_name; }     \
                                                                        \
   private:                                                             \
    FTL_DISALLOW_COPY_AND_ASSIGN(handler);                              \
  };

}  // namespace hcitool

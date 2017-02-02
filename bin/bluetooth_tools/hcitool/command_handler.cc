// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_handler.h"

#include <iostream>

#include "lib/ftl/command_line.h"
#include "lib/ftl/strings/string_printf.h"

namespace hcitool {

namespace {

void StatusCallback(ftl::Closure complete_cb,
                    bluetooth::hci::CommandChannel::TransactionId id,
                    bluetooth::hci::Status status) {
  std::cout << "Command Status: " << ftl::StringPrintf("0x%02x", status)
            << " (id=" << id << ")" << std::endl;
  if (status != bluetooth::hci::Status::kSuccess)
    complete_cb();
}

}  // namespace

CommandHandler::CommandHandler(bluetooth::hci::CommandChannel* cmd_channel,
                               ftl::RefPtr<ftl::TaskRunner> task_runner)
    : cmd_channel_(cmd_channel), task_runner_(task_runner) {}

bool CommandHandler::Run(const std::vector<std::string>& argv,
                         const ftl::Closure& complete_cb) {
  FTL_DCHECK(!argv.empty());

  ftl::CommandLine cl = ftl::CommandLineFromIterators(argv.begin(), argv.end());
  auto option_map = [cl](const std::string& name,
                         std::string* out_value) -> bool {
    return cl.GetOptionValue(name, out_value);
  };

  return HandleCommand(cl.positional_args(), cl.options().size(), option_map,
                       complete_cb);
}

bluetooth::hci::CommandChannel::CommandStatusCallback
CommandHandler::DefaultStatusCallback(const ftl::Closure& complete_cb) const {
  return std::bind(&StatusCallback, complete_cb, std::placeholders::_1,
                   std::placeholders::_2);
}

}  // namespace hcitool

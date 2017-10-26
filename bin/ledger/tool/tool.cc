// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/tool/tool.h"

#include <iostream>
#include <unordered_set>

#include "lib/app/cpp/connect.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/strings/concatenate.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/network/fidl/network_service.fidl.h"
#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/tool/convert.h"
#include "peridot/bin/ledger/tool/inspect_command.h"

namespace tool {

ToolApp::ToolApp(fxl::CommandLine command_line)
    : command_line_(std::move(command_line)),
      context_(app::ApplicationContext::CreateFromStartupInfo()) {
  if (Initialize()) {
    Start();
  } else {
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
  }
}

void ToolApp::PrintUsage() {
  std::cout << "Usage: ledger_tool <COMMAND>" << std::endl;
  std::cout << "Commands:" << std::endl;
  std::cout << " - `inspect` - inspects the state of a ledger" << std::endl;
}

std::unique_ptr<Command> ToolApp::CommandFromArgs(
    const std::vector<std::string>& args) {
  if (args.empty() || args[0] != "inspect") {
    std::cerr << "only the `inspect` command is currently supported"
              << std::endl;
    return nullptr;
  }

  return std::make_unique<InspectCommand>(args);
}

bool ToolApp::Initialize() {
  if (command_line_.argv0() == "file://cloud_sync") {
    std::cout << "The 'cloud_sync' command is deprecated. "
              << "Please use 'ledger_tool' instead." << std::endl;
  }

  std::unordered_set<std::string> valid_commands = {"inspect"};
  const std::vector<std::string>& args = command_line_.positional_args();
  if (!args.empty() && valid_commands.count(args[0]) == 0) {
    std::cerr << "Unknown command: " << args[0] << std::endl;
    PrintUsage();
    return false;
  }

  std::string repository_path;

  command_ = CommandFromArgs(args);
  if (command_ == nullptr) {
    std::cerr << "Failed to initialize the selected command." << std::endl;
    PrintUsage();
    return false;
  }
  return true;
}

void ToolApp::Start() {
  FXL_DCHECK(command_);
  command_->Start([] { fsl::MessageLoop::GetCurrent()->PostQuitTask(); });
}

}  // namespace tool

int main(int argc, const char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  fsl::MessageLoop loop;

  tool::ToolApp app(std::move(command_line));

  loop.Run();
  return 0;
}

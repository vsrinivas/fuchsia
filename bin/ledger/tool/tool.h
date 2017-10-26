// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TOOL_TOOL_H_
#define PERIDOT_BIN_LEDGER_TOOL_TOOL_H_

#include <memory>
#include <string>
#include <vector>

#include "lib/app/cpp/application_context.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/cloud_sync/public/user_config.h"
#include "peridot/bin/ledger/network/network_service_impl.h"
#include "peridot/bin/ledger/tool/command.h"

namespace tool {

class ToolApp {
 public:
  explicit ToolApp(fxl::CommandLine command_line);

 private:
  std::unique_ptr<Command> CommandFromArgs(
      const std::vector<std::string>& args);

  void PrintUsage();

  bool ReadConfig();

  bool Initialize();

  void Start();

  fxl::CommandLine command_line_;
  std::unique_ptr<app::ApplicationContext> context_;
  std::unique_ptr<Command> command_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ToolApp);
};

}  // namespace tool

#endif  // PERIDOT_BIN_LEDGER_TOOL_TOOL_H_

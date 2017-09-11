// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_TOOL_TOOL_H_
#define APPS_LEDGER_SRC_TOOL_TOOL_H_

#include <memory>
#include <string>
#include <vector>

#include "lib/app/cpp/application_context.h"
#include "apps/ledger/src/cloud_sync/public/user_config.h"
#include "apps/ledger/src/firebase/firebase_impl.h"
#include "apps/ledger/src/network/network_service_impl.h"
#include "apps/ledger/src/tool/command.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/macros.h"

namespace tool {

class ToolApp {
 public:
  explicit ToolApp(ftl::CommandLine command_line);

 private:
  std::unique_ptr<Command> CommandFromArgs(
      const std::vector<std::string>& args);

  void PrintUsage();

  bool ReadConfig();

  bool Initialize();

  void Start();

  ftl::CommandLine command_line_;
  std::unique_ptr<app::ApplicationContext> context_;
  std::unique_ptr<Command> command_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ToolApp);
};

}  // namespace tool

#endif  // APPS_LEDGER_SRC_TOOL_TOOL_H_

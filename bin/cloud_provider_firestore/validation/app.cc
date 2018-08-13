// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/cloud_provider/validation/launcher/validation_tests_launcher.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/concatenate.h>
#include <lib/fxl/strings/string_view.h>

#include "peridot/bin/cloud_provider_firestore/include/types.h"
#include "peridot/bin/cloud_provider_firestore/testing/cloud_provider_factory.h"
#include "peridot/bin/ledger/testing/sync_params.h"

namespace cloud_provider_firestore {
void PrintUsage(const char* executable_name) {
  std::cerr << "Usage: "
            << executable_name
            // Comment to make clang format not break formatting.
            << ledger::GetSyncParamsUsage();
}

}  // namespace cloud_provider_firestore

int main(int argc, char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  ledger::SyncParams sync_params;
  if (!ledger::ParseSyncParamsFromCommandLine(&command_line, &sync_params)) {
    cloud_provider_firestore::PrintUsage(argv[0]);
    return -1;
  }

  const std::set<std::string> known_options = ledger::GetSyncParamFlags();
  std::vector<std::string> arguments;
  for (auto& option : command_line.options()) {
    if (known_options.count(option.name) == 0u) {
      arguments.push_back(
          fxl::Concatenate({"--", option.name, "=", option.value}));
    }
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  std::unique_ptr<component::StartupContext> startup_context =
      component::StartupContext::CreateFromStartupInfo();
  cloud_provider_firestore::CloudProviderFactory factory(
      startup_context.get(), sync_params.server_id, sync_params.api_key,
      sync_params.credentials);

  cloud_provider::ValidationTestsLauncher launcher(
      startup_context.get(), [&factory](auto request) {
        factory.MakeCloudProvider(std::move(request));
      });

  int32_t return_code = -1;
  async::PostTask(loop.dispatcher(), [&factory, &launcher, &return_code, &loop,
                                      arguments = std::move(arguments)] {
    factory.Init();

    launcher.Run(arguments, [&return_code, &loop](int32_t result) {
      return_code = result;
      loop.Quit();
    });
  });
  loop.Run();
  return return_code;
}

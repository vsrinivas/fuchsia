// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/concatenate.h>
#include <src/lib/fxl/strings/string_view.h>

#include "peridot/lib/rng/system_random.h"
#include "src/ledger/bin/testing/sync_params.h"
#include "src/ledger/bin/tests/cloud_provider/launcher/validation_tests_launcher.h"
#include "src/ledger/cloud_provider_firestore/bin/include/types.h"
#include "src/ledger/cloud_provider_firestore/bin/testing/cloud_provider_factory.h"
#include "src/lib/files/file.h"

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
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  std::unique_ptr<component::StartupContext> startup_context =
      component::StartupContext::CreateFromStartupInfo();

  ledger::SyncParams sync_params;
  if (!ledger::ParseSyncParamsFromCommandLine(
          command_line, startup_context.get(), &sync_params)) {
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

  rng::SystemRandom random;

  cloud_provider_firestore::CloudProviderFactory factory(
      startup_context.get(), &random, sync_params.api_key,
      sync_params.credentials->Clone());

  cloud_provider::ValidationTestsLauncher launcher(
      startup_context.get(), [&factory](auto request) {
        factory.MakeCloudProvider(
            cloud_provider_firestore::CloudProviderFactory::UserId::New(),
            std::move(request));
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

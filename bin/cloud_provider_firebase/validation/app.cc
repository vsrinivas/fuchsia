// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/cloud_provider/validation/launcher/validation_tests_launcher.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/string_view.h>

#include "peridot/bin/ledger/testing/cloud_provider_firebase_factory.h"

namespace cloud_provider_firebase {
namespace {
constexpr fxl::StringView kServerIdFlag = "server-id";
}  // namespace

void PrintUsage(const char* executable_name) {
  std::cout << "Usage: " << executable_name << "--" << kServerIdFlag
            << "=<string>" << std::endl;
}

}  // namespace cloud_provider_firebase

int main(int argc, char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  std::string server_id;
  if (!command_line.GetOptionValue(
          cloud_provider_firebase::kServerIdFlag.ToString(), &server_id)) {
    cloud_provider_firebase::PrintUsage(argv[0]);
    return -1;
  }

  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  std::unique_ptr<fuchsia::sys::StartupContext> startup_context =
      fuchsia::sys::StartupContext::CreateFromStartupInfo();
  test::CloudProviderFirebaseFactory factory(startup_context.get());

  cloud_provider::ValidationTestsLauncher launcher(
      startup_context.get(), [&factory, server_id](auto request) {
        factory.MakeCloudProvider(server_id, "", std::move(request));
      });

  int32_t return_code = -1;
  async::PostTask(loop.dispatcher(), [&factory, &launcher, &return_code, &loop] {
    factory.Init();
    launcher.Run({}, [&return_code, &loop](int32_t result) {
      return_code = result;
      loop.Quit();
    });
  });
  loop.Run();
  return return_code;
}

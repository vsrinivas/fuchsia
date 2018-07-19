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

namespace cloud_provider_firestore {
namespace {
constexpr fxl::StringView kServerIdFlag = "server-id";
constexpr fxl::StringView kApiKeyFlag = "api-key";
constexpr fxl::StringView kCredentialsFlag = "credentials";
}  // namespace

void PrintUsage(const char* executable_name) {
  std::cout << "Usage: "
            << executable_name
            // Comment to make clang format not break formatting.
            << "--" << kServerIdFlag << "=<string> "
            << "--" << kApiKeyFlag << "=<string> "
            << "--" << kCredentialsFlag << "=<file path>" << std::endl;
}

}  // namespace cloud_provider_firestore

int main(int argc, char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  std::string server_id;
  std::string api_key;
  std::string credentials_path;
  if (!command_line.GetOptionValue(
          cloud_provider_firestore::kServerIdFlag.ToString(), &server_id) ||
      !command_line.GetOptionValue(
          cloud_provider_firestore::kApiKeyFlag.ToString(), &api_key) ||
      !command_line.GetOptionValue(
          cloud_provider_firestore::kCredentialsFlag.ToString(),
          &credentials_path)) {
    cloud_provider_firestore::PrintUsage(argv[0]);
    return -1;
  }

  if (!files::IsFile(credentials_path)) {
    std::cerr << "Cannot access " << credentials_path << std::endl;
    cloud_provider_firestore::PrintUsage(argv[0]);
    return -1;
  }

  std::string credentials;
  if (!files::ReadFileToString(credentials_path, &credentials)) {
    std::cerr << "Cannot read " << credentials_path << std::endl;
    cloud_provider_firestore::PrintUsage(argv[0]);
    return -1;
  }

  const std::set<std::string> known_options(
      {cloud_provider_firestore::kServerIdFlag.ToString(),
       cloud_provider_firestore::kApiKeyFlag.ToString(),
       cloud_provider_firestore::kCredentialsFlag.ToString()});
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
  cloud_provider_firestore::CloudProviderFactory factory(startup_context.get(),
                                                         credentials);

  cloud_provider::ValidationTestsLauncher launcher(
      startup_context.get(), [&factory, api_key, server_id](auto request) {
        factory.MakeCloudProvider(server_id, api_key, std::move(request));
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

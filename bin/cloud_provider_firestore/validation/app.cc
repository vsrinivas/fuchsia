// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_view.h"
#include "peridot/bin/cloud_provider_firestore/testing/cloud_provider_factory.h"
#include "peridot/public/lib/cloud_provider/validation/launcher/validation_tests_launcher.h"

namespace cloud_provider_firestore {
namespace {
constexpr fxl::StringView kServerIdFlag = "server-id";
constexpr fxl::StringView kApiKey = "api-key";
constexpr fxl::StringView kCredentialsFlag = "credentials";
}  // namespace

void PrintUsage(const char* executable_name) {
  std::cout << "Usage: "
            << executable_name
            // Comment to make clang format not break formatting.
            << "--" << kServerIdFlag << "=<string> "
            << "--" << kApiKey << "=<string> "
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
      !command_line.GetOptionValue(cloud_provider_firestore::kApiKey.ToString(),
                                   &api_key) ||
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

  fsl::MessageLoop message_loop;
  std::unique_ptr<app::ApplicationContext> application_context =
      app::ApplicationContext::CreateFromStartupInfo();
  cloud_provider_firestore::CloudProviderFactory factory(
      application_context.get(), credentials_path);

  cloud_provider::ValidationTestsLauncher launcher(
      application_context.get(), [&factory, api_key, server_id](auto request) {
        factory.MakeCloudProvider(server_id, api_key, std::move(request));
      });

  int32_t return_code = -1;
  message_loop.task_runner()->PostTask(
      [&factory, &launcher, &return_code, &message_loop] {
        factory.Init();

        launcher.Run([&return_code, &message_loop](int32_t result) {
          return_code = result;
          message_loop.PostQuitTask();
        });
      });
  message_loop.Run();
  return return_code;
}

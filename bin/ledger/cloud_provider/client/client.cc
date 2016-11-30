// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_provider/client/client.h"

#include <iostream>
#include <unordered_set>

#include "apps/ledger/src/cloud_provider/impl/cloud_provider_impl.h"
#include "apps/ledger/src/cloud_provider/public/types.h"
#include "apps/ledger/src/configuration/configuration.h"
#include "apps/ledger/src/configuration/configuration_encoder.h"
#include "apps/ledger/src/firebase/encoding.h"
#include "apps/ledger/src/firebase/firebase_impl.h"
#include "apps/ledger/src/glue/crypto/rand.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/network/services/network_service.fidl.h"
#include "lib/ftl/strings/concatenate.h"
#include "lib/ftl/strings/string_view.h"
#include "lib/mtl/tasks/message_loop.h"

namespace cloud_provider {

namespace {

std::string RandomString() {
  return std::to_string(glue::RandUint64());
}

std::string GetFirebasePrefix(ftl::StringView user_prefix,
                              ftl::StringView page_id) {
  return ftl::Concatenate({firebase::EncodeKey(user_prefix), "/",
                           firebase::EncodeKey("debug_cloud_sync"), "/",
                           firebase::EncodeKey(page_id)});
}

}  // namespace

ClientApp::ClientApp(ftl::CommandLine command_line)
    : command_line_(std::move(command_line)),
      context_(modular::ApplicationContext::CreateFromStartupInfo()) {
  if (Initialize()) {
    Start();
  }
}

void ClientApp::PrintUsage() {
  std::cout << "Usage: cloud_sync <COMMAND>" << std::endl;
  std::cout << "Commands:" << std::endl;
  std::cout << " - `doctor` - checks up the cloud sync configuration (default)"
            << std::endl;
}

std::unique_ptr<Command> ClientApp::CommandFromArgs(
    const std::vector<std::string>& args) {
  if (args.empty()) {
    return nullptr;
  }

  // `doctor` is the default command.
  if (args.empty() || (args[0] == "doctor" && args.size() == 1)) {
    return std::make_unique<DoctorCommand>(cloud_provider_.get());
  }

  return nullptr;
}

bool ClientApp::Initialize() {
  std::unordered_set<std::string> valid_commands = {"doctor"};
  const std::vector<std::string>& args = command_line_.positional_args();
  if (args.size() && valid_commands.count(args[0]) == 0) {
    PrintUsage();
    return false;
  }

  configuration::Configuration configuration;
  if (!configuration::ConfigurationEncoder::Decode(
          configuration::kDefaultConfigurationFile.ToString(),
          &configuration)) {
    std::cout << "Error: unable to read Ledger configuration at: "
              << configuration::kDefaultConfigurationFile << std::endl;
    std::cout << "Hint: run `configure_ledger --help` to learn about "
              << "configuration options." << std::endl;
    return false;
  }

  if (!configuration.use_sync) {
    std::cout << "Error: Cloud sync is disabled in the Ledger configuration."
              << std::endl;
    std::cout << "Hint: pass --firebase_id and --firebase_prefix to "
              << "`configure_ledger`" << std::endl;
    return false;
  }

  std::cout << "Cloud Sync Settings:" << std::endl;
  std::cout << " - firebase id: " << configuration.sync_params.firebase_id
            << std::endl;
  std::cout << " - firebase prefix: "
            << configuration.sync_params.firebase_prefix << std::endl;
  std::cout << std::endl;

  network_service_ = std::make_unique<ledger::NetworkServiceImpl>([this] {
    return context_->ConnectToEnvironmentService<network::NetworkService>();
  });

  firebase_ = std::make_unique<firebase::FirebaseImpl>(
      network_service_.get(), configuration.sync_params.firebase_id,
      GetFirebasePrefix(configuration.sync_params.firebase_prefix,
                        RandomString()));
  cloud_provider_ = std::make_unique<CloudProviderImpl>(firebase_.get());

  command_ = CommandFromArgs(args);
  return true;
}

void ClientApp::Start() {
  FTL_DCHECK(command_);
  command_->Start([] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });
}

}  // namespace cloud_provider

int main(int argc, const char** argv) {
  ftl::CommandLine command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  mtl::MessageLoop loop;

  cloud_provider::ClientApp app(std::move(command_line));

  loop.Run();
  return 0;
}

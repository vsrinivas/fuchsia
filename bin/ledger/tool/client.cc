// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/tool/client.h"

#include <iostream>
#include <unordered_set>

#include "application/lib/app/connect.h"
#include "apps/ledger/src/cloud_provider/public/types.h"
#include "apps/ledger/src/configuration/configuration.h"
#include "apps/ledger/src/configuration/configuration_encoder.h"
#include "apps/ledger/src/configuration/load_configuration.h"
#include "apps/ledger/src/firebase/encoding.h"
#include "apps/ledger/src/tool/clean_command.h"
#include "apps/ledger/src/tool/doctor_command.h"
#include "apps/network/services/network_service.fidl.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/strings/concatenate.h"
#include "lib/ftl/strings/string_view.h"
#include "lib/mtl/tasks/message_loop.h"

namespace tool {

namespace {

constexpr ftl::StringView kUserIdFlag = "user-id";
constexpr ftl::StringView kForceFlag = "force";

// Inverse of the transformation currently used by DeviceRunner to translate
// human-readable username to user ID.
bool FromHexString(const std::string& hex_string, std::string* result) {
  if (hex_string.size() % 2 != 0) {
    return false;
  }

  std::string bytes;
  bytes.reserve(hex_string.size() / 2);
  for (size_t i = 0; i < hex_string.size(); i += 2) {
    bytes.push_back(strtol(hex_string.substr(i, 2).c_str(), nullptr, 16));
  }
  result->swap(bytes);
  return true;
}

// Transformation currently used by DeviceRunner to translate human-readable
// username to user ID.
std::string ToHexString(ftl::StringView data) {
  constexpr char kHexadecimalCharacters[] = "0123456789abcdef";
  std::string ret;
  ret.reserve(data.size() * 2);
  for (size_t i = 0; i < data.size(); ++i) {
    ret.push_back(kHexadecimalCharacters[static_cast<uint8_t>(data[i]) >> 4]);
    ret.push_back(kHexadecimalCharacters[static_cast<uint8_t>(data[i]) & 0xf]);
  }
  return ret;
}

}  // namespace

ClientApp::ClientApp(ftl::CommandLine command_line)
    : command_line_(std::move(command_line)),
      context_(app::ApplicationContext::CreateFromStartupInfo()) {
  if (Initialize()) {
    Start();
  } else {
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  }
}

void ClientApp::PrintUsage() {
  std::cout << "Usage: ledger_tool [options] <COMMAND>" << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << " --user-id=<string> overrides the user ID to use" << std::endl;
  std::cout << " --force skips confirmation dialogs" << std::endl;
  std::cout << "Commands:" << std::endl;
  std::cout << " - `doctor` - checks up the Ledger configuration (default)"
            << std::endl;
  std::cout
      << " - `clean` - wipes remote and local data of the most recent user "
      << std::endl;
}

std::unique_ptr<Command> ClientApp::CommandFromArgs(
    const std::vector<std::string>& args) {
  // `doctor` is the default command.
  if (args.empty() || args[0] == "doctor") {
    if (args.size() > 1) {
      FTL_LOG(ERROR) << "Too many arguments for the " << args[0] << " command";
      return nullptr;
    }
    return std::make_unique<DoctorCommand>(user_config_,
                                           network_service_.get());
  }

  if (args[0] == "clean") {
    if (args.size() > 1) {
      FTL_LOG(ERROR) << "Too many arguments for the " << args[0] << " command";
      return nullptr;
    }
    return std::make_unique<CleanCommand>(
        user_config_, user_repository_path_, network_service_.get(),
        command_line_.HasOption(kForceFlag.ToString()));
  }

  return nullptr;
}

bool ClientApp::Initialize() {
  if (command_line_.argv0() == "file://cloud_sync") {
    std::cout << "The 'cloud_sync' command is deprecated. "
              << "Please use 'ledger_tool' instead." << std::endl;
  }

  const std::unordered_set<std::string> known_options = {
      kForceFlag.ToString(), kUserIdFlag.ToString()};

  for (auto& option : command_line_.options()) {
    if (known_options.count(option.name) == 0) {
      FTL_LOG(ERROR) << "Unknown option: " << option.name << std::endl;
      PrintUsage();
      return false;
    }
  }

  std::unordered_set<std::string> valid_commands = {"doctor", "clean"};
  const std::vector<std::string>& args = command_line_.positional_args();
  if (args.size() && valid_commands.count(args[0]) == 0) {
    FTL_LOG(ERROR) << "Unknown command: " << args[0];
    PrintUsage();
    return false;
  }

  std::string repository_path;
  if (!ReadConfig()) {
    std::cout << "Error: no Ledger configuration found." << std::endl;
    std::cout
        << "Hint: refer to the User Guide at "
        << "https://fuchsia.googlesource.com/ledger/+/HEAD/docs/user_guide.md"
        << std::endl;
    return false;
  }

  if (!user_config_.use_sync) {
    std::cout << "Error: Cloud sync is disabled in the Ledger configuration."
              << std::endl;
    std::cout << "Hint: pass --firebase_id to `configure_ledger`" << std::endl;
    return false;
  }

  std::cout << "parameters: " << std::endl;
  // User ID.
  std::cout << " - user ID: " << user_config_.user_id;
  std::string readable_id;
  if (!user_config_.user_id.empty() &&
      FromHexString(user_config_.user_id, &readable_id)) {
    std::cout << " (" << readable_id << ")";
  }
  std::cout << std::endl;
  // Sync settings.
  std::cout << " - firebase ID: " << user_config_.server_id << std::endl;
  std::cout << std::endl;

  network_service_ = std::make_unique<ledger::NetworkServiceImpl>(
      mtl::MessageLoop::GetCurrent()->task_runner(), [this] {
        return context_->ConnectToEnvironmentService<network::NetworkService>();
      });

  command_ = CommandFromArgs(args);
  if (command_ == nullptr) {
    std::cout << "Failed to initialize the selected command." << std::endl;
    PrintUsage();
    return false;
  }
  return true;
}

bool ClientApp::ReadConfig() {
  configuration::Configuration global_config;
  if (!configuration::LoadConfiguration(&global_config)) {
    return false;
  }
  user_config_.use_sync = global_config.use_sync;
  user_config_.server_id = global_config.sync_params.firebase_id;

  std::string user_id_human_readable;
  if (command_line_.GetOptionValue(kUserIdFlag.ToString(),
                                   &user_id_human_readable)) {
    FTL_LOG(INFO) << "using the user id passed on the command line";
    user_config_.user_id = ToHexString(user_id_human_readable);
    user_repository_path_ =
        ftl::Concatenate({"/data/ledger/", user_config_.user_id});
    return true;
  }

  if (!files::IsFile(configuration::kLastUserIdPath.ToString()) ||
      !files::ReadFileToString(configuration::kLastUserIdPath.ToString(),
                               &user_config_.user_id) ||
      !files::IsFile(configuration::kLastUserRepositoryPath.ToString()) ||
      !files::ReadFileToString(
          configuration::kLastUserRepositoryPath.ToString(),
          &user_repository_path_)) {
    FTL_LOG(ERROR) << "Failed to identify the most recent user ID, "
                   << "pick the user in Device Shell UI or pass the user ID "
                   << "to use in the --" << kUserIdFlag << " flag";
    return false;
  }

  FTL_LOG(INFO) << "using the user id of the most recent Ledger run";
  return true;
}

void ClientApp::Start() {
  FTL_DCHECK(command_);
  command_->Start([] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });
}

}  // namespace tool

int main(int argc, const char** argv) {
  ftl::CommandLine command_line = ftl::CommandLineFromArgcArgv(argc, argv);

  mtl::MessageLoop loop;

  tool::ClientApp app(std::move(command_line));

  loop.Run();
  return 0;
}

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <magenta/device/devmgr.h>
#include <magenta/syscalls.h>
#include <unistd.h>
#include <iostream>
#include <memory>

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "application/services/application_launcher.fidl.h"
#include "application/services/service_provider.fidl.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/services/config/config.fidl.h"
#include "apps/modular/services/device/device_context.fidl.h"
#include "apps/modular/services/device/device_shell.fidl.h"
#include "apps/modular/services/device/user_provider.fidl.h"
#include "apps/modular/services/user/user_context.fidl.h"
#include "apps/modular/services/user/user_runner.fidl.h"
#include "apps/modular/src/device_runner/password_hash.h"
#include "apps/modular/src/device_runner/user_controller_impl.h"
#include "apps/modular/src/device_runner/users_generated.h"
#include "apps/mozart/services/presentation/presenter.fidl.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/fidl/cpp/bindings/struct_ptr.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_printf.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/flatbuffers/include/flatbuffers/flatbuffers.h"

namespace modular {
namespace {

// To generate this configuration file follow instructions at
// https://fuchsia.googlesource.com/modules/+/master/#Email
// TODO(alhaad) Find a different home for this config file.
constexpr char kAuthConfigurationFile[] = "/system/data/email/config.json";

// For polling minfs.
constexpr ftl::StringView kPersistentFileSystem = "/data";
constexpr ftl::StringView kMinFsName = "minfs";
constexpr ftl::TimeDelta kMaxPollingDelay = ftl::TimeDelta::FromSeconds(5);

constexpr char kLedgerAppUrl[] = "file:///system/apps/ledger";
constexpr char kLedgerDataBaseDir[] = "/data/ledger/";
constexpr char kUsersConfigurationFile[] = "/data/modular/device/users.db";

class Settings {
 public:
  explicit Settings(const ftl::CommandLine& command_line) {
    device_name =
        command_line.GetOptionValueWithDefault("device_name", "magenta");
    user_runner = command_line.GetOptionValueWithDefault(
        "user_runner", "file:///system/apps/user_runner");

    device_shell.url = command_line.GetOptionValueWithDefault(
        "device_shell", "file:///system/apps/userpicker_device_shell");
    user_shell.url = command_line.GetOptionValueWithDefault(
        "user_shell", "file:///system/apps/armadillo_user_shell");
    story_shell.url = command_line.GetOptionValueWithDefault(
        "story_shell", "file:///system/apps/dummy_story_shell");

    ledger_repository_for_testing =
        command_line.HasOption("ledger_repository_for_testing");
    user_auth_config_file = command_line.HasOption("user_auth_config_file");

    ParseShellArgs(
        command_line.GetOptionValueWithDefault("device_shell_args", ""),
        &device_shell.args);

    ParseShellArgs(
        command_line.GetOptionValueWithDefault("user_shell_args", ""),
        &user_shell.args);

    ParseShellArgs(
        command_line.GetOptionValueWithDefault("story_shell_args", ""),
        &story_shell.args);
  }

  static std::string GetUsage() {
    return R"USAGE(device_runner
      --device_name=DEVICE_NAME
      --device_shell=DEVICE_SHELL
      --device_shell_args=SHELL_ARGS
      --user_runner=USER_RUNNER
      --user_shell=USER_SHELL
      --user_shell_args=SHELL_ARGS
      --story_shell=STORY_SHELL
      --story_shell_args=SHELL_ARGS
      --ledger_repository_for_testing
      --user_auth_config_file
    DEVICE_NAME: Name which user shell uses to identify this device.
    DEVICE_SHELL: URL of the device shell to run.
                Defaults to "file:///system/apps/dummy_device_shell".
    USER_RUNNER: URL of the user runner implementation to run.
                Defaults to "file:///system/apps/user_runner"
    USER_SHELL: URL of the user shell to run.
                Defaults to "file:///system/apps/armadillo_user_shell".
                For integration testing use "dummy_user_shell".
    STORY_SHELL: URL of the story shell to run.
                Defaults to "file:///system/apps/dummy_story_shell".
    SHELL_ARGS: Comma separated list of arguments. Backslash escapes comma.)USAGE";
  }

  std::string device_name;
  AppConfig device_shell;

  std::string user_runner;
  AppConfig user_shell;

  AppConfig story_shell;

  bool ledger_repository_for_testing;
  bool user_auth_config_file;

 private:
  void ParseShellArgs(const std::string& value,
                      fidl::Array<fidl::String>* args) {
    bool escape = false;
    std::string arg;
    for (std::string::const_iterator i = value.begin(); i != value.end(); ++i) {
      if (escape) {
        arg.push_back(*i);
        escape = false;
        continue;
      }

      if (*i == '\\') {
        escape = true;
        continue;
      }

      if (*i == ',') {
        args->push_back(arg);
        arg.clear();
        continue;
      }

      arg.push_back(*i);
    }

    if (!arg.empty()) {
      args->push_back(arg);
    }

    for (auto& s : *args) {
      FTL_LOG(INFO) << "ARG " << s;
    }
  }

  FTL_DISALLOW_COPY_AND_ASSIGN(Settings);
};

// TODO(vardhan): A running user's state is starting to grow, so break it
// outside of DeviceRunnerApp.
class DeviceRunnerApp : UserProvider, DeviceContext {
 public:
  DeviceRunnerApp(const Settings& settings)
      : settings_(settings),
        app_context_(app::ApplicationContext::CreateFromStartupInfo()),
        device_context_binding_(this),
        user_provider_binding_(this) {
    // 1. Start the device shell. This also connects the root view of the device
    // to the device shell. This is done first so that we can show some UI until
    // other things come up.
    app::ServiceProviderPtr device_shell_services;
    auto device_shell_launch_info = app::ApplicationLaunchInfo::New();
    device_shell_launch_info->url = settings_.device_shell.url;
    device_shell_launch_info->arguments = settings_.device_shell.args.Clone();
    device_shell_launch_info->services = device_shell_services.NewRequest();

    app_context_->launcher()->CreateApplication(
        std::move(device_shell_launch_info),
        device_shell_controller_.NewRequest());

    mozart::ViewProviderPtr device_shell_view_provider;
    ConnectToService(device_shell_services.get(),
                     device_shell_view_provider.NewRequest());

    DeviceShellFactoryPtr device_shell_factory;
    ConnectToService(device_shell_services.get(),
                     device_shell_factory.NewRequest());

    fidl::InterfaceHandle<mozart::ViewOwner> root_view;
    device_shell_view_provider->CreateView(root_view.NewRequest(), nullptr);
    app_context_->ConnectToEnvironmentService<mozart::Presenter>()->Present(
        std::move(root_view));

    device_shell_factory->Create(device_context_binding_.NewBinding(),
                                 user_provider_binding_.NewBinding(),
                                 device_shell_.NewRequest());

    // 2. Get login data for users of the device.
    // There might not be a file of users persisted. If config file doesn't
    // exist, move forward with no previous users.
    WaitForMinfs();
    if (files::IsFile(kUsersConfigurationFile)) {
      std::string serialized_users;
      if (!files::ReadFileToString(kUsersConfigurationFile,
                                   &serialized_users)) {
        // Unable to read file. Bailing out.
        FTL_LOG(ERROR) << "Unable to read user configuration file at: "
                       << kUsersConfigurationFile;
        return;
      }

      if (!Parse(serialized_users)) {
        return;
      }
    }

    if (!app_context_->launcher()) {
      FTL_LOG(ERROR) << "Environment handle not set. Please use @boot.";
      exit(1);
    }
    if (!app_context_->environment_services()) {
      FTL_LOG(ERROR) << "Services handle not set. Please use @boot.";
      exit(1);
    }

    // 3. Start the ledger.
    app::ServiceProviderPtr ledger_services;
    auto ledger_launch_info = app::ApplicationLaunchInfo::New();
    ledger_launch_info->url = kLedgerAppUrl;
    ledger_launch_info->arguments = nullptr;
    ledger_launch_info->services = ledger_services.NewRequest();

    app_context_->launcher()->CreateApplication(
        std::move(ledger_launch_info), ledger_controller_.NewRequest());

    ConnectToService(ledger_services.get(),
                     ledger_repository_factory_.NewRequest());
  }

 private:
  // |DeviceContext|
  // TODO(vardhan): Signal the ledger application to tear down.
  void Shutdown() override {
    FTL_LOG(INFO) << "DeviceContext::Shutdown()";
    auto cont = [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); };
    if ((bool)user_controller_impl_) {
      // This will tear down the user runner altogether, and call us back to
      // delete |user_controller_impl_| when it is ready to be killed.
      user_controller_impl_->Logout(cont);
      // At this point, |user_controller_impl_| is destroyed.
    } else {
      cont();
    }
  }

  // |UserProvider|
  void Login(
      const fidl::String& username,
      const fidl::String& password,
      const fidl::String& auth_token,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<UserController> user_controller_request) override {
    // Check username and password before logging in.
    bool found_user = false;
    if (users_storage_) {
      for (const auto* user : *users_storage_->users()) {
        if (user->username()->str() == username &&
            CheckPassword(password, user->password_hash()->str())) {
          found_user = true;
          break;
        }
      }
    }
    if (!found_user) {
      FTL_LOG(INFO) << "Invalid username or password";
      return;
    }

    FTL_LOG(INFO) << "UserProvider::Login() user: " << username;

    // Get the LedgerRepository for the user.
    fidl::Array<uint8_t> user_id = to_array(username);
    std::string ledger_repository_path =
        kLedgerDataBaseDir + to_hex_string(user_id);

    if (settings_.ledger_repository_for_testing) {
      unsigned random_number;
      size_t random_size;
      mx_status_t status =
          mx_cprng_draw(&random_number, sizeof random_number, &random_size);
      FTL_CHECK(status == NO_ERROR);
      FTL_CHECK(sizeof random_number == random_size);
      ledger_repository_path +=
          ftl::StringPrintf("_for_testing_%X", random_number);
      FTL_LOG(INFO) << "Using testing ledger repository path: "
                    << ledger_repository_path;
    }

    fidl::InterfaceHandle<ledger::LedgerRepository> ledger_repository;
    ledger_repository_factory_->GetRepository(
        ledger_repository_path, ledger_repository.NewRequest(),
        [](ledger::Status status) {
          FTL_DCHECK(status == ledger::Status::OK)
              << "GetRepository failed: " << status;
        });

    std::string ret_auth_token;
    if (settings_.user_auth_config_file) {
      if (!files::IsFile(kAuthConfigurationFile)) {
        FTL_LOG(ERROR) << "Auth configuration file not found at: "
                       << kAuthConfigurationFile;
      } else {
        if (!files::ReadFileToString(kAuthConfigurationFile, &ret_auth_token)) {
          // Unable to read file. Bailing out.
          FTL_LOG(ERROR) << "Unable to read auth configuration file at: "
                         << kAuthConfigurationFile;
          ret_auth_token = auth_token;
        }
      }
    }

    user_controller_impl_.reset(new UserControllerImpl(
        app_context_, settings_.device_name, settings_.user_runner,
        settings_.user_shell, settings_.story_shell, ret_auth_token,
        std::move(user_id), std::move(ledger_repository),
        std::move(view_owner_request), std::move(user_controller_request),
        [this] { user_controller_impl_.reset(); }));
  }

  // |UserProvider|
  void PreviousUsers(const PreviousUsersCallback& callback) override {
    fidl::Array<fidl::String> users = fidl::Array<fidl::String>::New(0);
    if (users_storage_) {
      for (const auto* user : *users_storage_->users()) {
        users.push_back(user->username()->str());
      }
    }
    callback(std::move(users));
  }

  // |UserProvider|
  void AddUser(const fidl::String& username,
               const fidl::String& password,
               const fidl::String& servername) override {
    flatbuffers::FlatBufferBuilder builder;
    std::vector<flatbuffers::Offset<modular::UserStorage>> users;

    // Reserialize existing users. Stop if |username| already exists.
    if (users_storage_) {
      for (const auto* user : *(users_storage_->users())) {
        if (user->username()->str() == username) {
          FTL_LOG(INFO) << username << " already exists. Continuing without "
                        << "creating a new entry.";
          return;
        }
        users.push_back(modular::CreateUserStorage(
            builder, builder.CreateString(user->username()),
            builder.CreateString(user->password_hash()),
            builder.CreateString(user->server_name())));
      }
    }

    // Add the new user.
    std::string password_hash;
    if (!modular::HashPassword(password, &password_hash)) {
      return;
    }
    users.push_back(modular::CreateUserStorage(
        builder, builder.CreateString(std::move(username)),
        builder.CreateString(std::move(password_hash)),
        builder.CreateString(std::move(servername))));

    builder.Finish(modular::CreateUsersStorage(
        builder, builder.CreateVector(std::move(users))));
    std::string new_serialized_users = std::string(
        reinterpret_cast<const char*>(builder.GetCurrentBufferPointer()),
        builder.GetSize());
    if (!Parse(new_serialized_users)) {
      return;
    }

    // Save users to disk.
    if (!files::CreateDirectory(
            files::GetDirectoryName(kUsersConfigurationFile))) {
      return;
    }
    if (!files::WriteFile(kUsersConfigurationFile, new_serialized_users.data(),
                          new_serialized_users.size())) {
      return;
    }
  }

  bool Parse(const std::string& serialized_users) {
    flatbuffers::Verifier verifier(
        reinterpret_cast<const unsigned char*>(serialized_users.data()),
        serialized_users.size());
    if (!modular::VerifyUsersStorageBuffer(verifier)) {
      FTL_LOG(ERROR) << "Unable to verify storage buffer.";
      return false;
    }
    serialized_users_ = std::move(serialized_users);
    users_storage_ = modular::GetUsersStorage(serialized_users_.data());
    return true;
  }

  void WaitForMinfs() {
    auto delay = ftl::TimeDelta::FromMilliseconds(10);
    ftl::TimePoint now = ftl::TimePoint::Now();
    while (ftl::TimePoint::Now() - now < kMaxPollingDelay) {
      ftl::UniqueFD fd(open(kPersistentFileSystem.data(), O_RDWR));
      FTL_DCHECK(fd.is_valid());
      char out[128];
      ssize_t len = ioctl_devmgr_query_fs(fd.get(), out, sizeof(out));
      FTL_DCHECK(len >= 0);

      ftl::StringView fs_name(out, len);
      if (fs_name == kMinFsName) {
        return;
      }
      usleep(delay.ToMicroseconds());
      delay = delay * 2;
    }

    FTL_LOG(WARNING) << kPersistentFileSystem
                     << " is not persistent. Did you forget to configure it?";
  }

  const Settings& settings_;  // Not owned nor copied.
  std::string serialized_users_;
  const modular::UsersStorage* users_storage_ = nullptr;

  std::shared_ptr<app::ApplicationContext> app_context_;
  fidl::Binding<DeviceContext> device_context_binding_;
  fidl::Binding<UserProvider> user_provider_binding_;

  app::ApplicationControllerPtr device_shell_controller_;
  DeviceShellPtr device_shell_;

  ledger::LedgerRepositoryFactoryPtr ledger_repository_factory_;
  app::ApplicationControllerPtr ledger_controller_;

  std::unique_ptr<UserControllerImpl> user_controller_impl_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DeviceRunnerApp);
};

}  // namespace
}  // namespace modular

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (command_line.HasOption("help")) {
    std::cout << modular::Settings::GetUsage() << std::endl;
    return 0;
  }

  modular::Settings settings(command_line);

  mtl::MessageLoop loop;
  modular::DeviceRunnerApp app(settings);
  loop.Run();
  return 0;
}

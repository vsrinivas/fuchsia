// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "application/services/application_launcher.fidl.h"
#include "application/services/service_provider.fidl.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/util/filesystem.h"
#include "apps/modular/services/auth/account_provider.fidl.h"
#include "apps/modular/services/config/config.fidl.h"
#include "apps/modular/services/device/device_runner_monitor.fidl.h"
#include "apps/modular/services/device/device_shell.fidl.h"
#include "apps/modular/services/device/user_provider.fidl.h"
#include "apps/modular/services/user/user_context.fidl.h"
#include "apps/modular/services/user/user_runner.fidl.h"
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

constexpr char kLedgerAppUrl[] = "file:///system/apps/ledger";
constexpr char kLedgerDataBaseDir[] = "/data/ledger/";
constexpr char kUsersConfigurationFile[] = "/data/modular/device/users-v1.db";

class Settings {
 public:
  explicit Settings(const ftl::CommandLine& command_line) {
    user_runner = command_line.GetOptionValueWithDefault(
        "user_runner", "file:///system/apps/user_runner");

    device_shell.url = command_line.GetOptionValueWithDefault(
        "device_shell", "file:///system/apps/userpicker_device_shell");
    user_shell.url = command_line.GetOptionValueWithDefault(
        "user_shell", "file:///system/apps/armadillo_user_shell");
    story_shell.url = command_line.GetOptionValueWithDefault(
        "story_shell", "file:///system/apps/mondrian");

    ledger_repository_for_testing =
        command_line.HasOption("ledger_repository_for_testing");
    user_auth_config_file = command_line.HasOption("user_auth_config_file");
    ignore_monitor = command_line.HasOption("ignore_monitor");
    no_minfs = command_line.HasOption("no_minfs");

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
      --device_shell=DEVICE_SHELL
      --device_shell_args=SHELL_ARGS
      --user_runner=USER_RUNNER
      --user_shell=USER_SHELL
      --user_shell_args=SHELL_ARGS
      --story_shell=STORY_SHELL
      --story_shell_args=SHELL_ARGS
      --ledger_repository_for_testing
      --user_auth_config_file
      --ignore_monitor
      --no_minfs
    DEVICE_NAME: Name which user shell uses to identify this device.
    DEVICE_SHELL: URL of the device shell to run.
                Defaults to "file:///system/apps/userpicker_device_shell".
    USER_RUNNER: URL of the user runner implementation to run.
                Defaults to "file:///system/apps/user_runner"
    USER_SHELL: URL of the user shell to run.
                Defaults to "file:///system/apps/armadillo_user_shell".
                For integration testing use "dummy_user_shell".
    STORY_SHELL: URL of the story shell to run.
                Defaults to "file:///system/apps/dummy_story_shell".
    SHELL_ARGS: Comma separated list of arguments. Backslash escapes comma.)USAGE";
  }

  AppConfig device_shell;

  std::string user_runner;
  AppConfig user_shell;

  AppConfig story_shell;

  bool ledger_repository_for_testing;
  bool user_auth_config_file;
  bool ignore_monitor;
  bool no_minfs;

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
class DeviceRunnerApp : DeviceShellContext,
                        UserProvider,
                        auth::AccountProviderContext {
 public:
  DeviceRunnerApp(const Settings& settings)
      : settings_(settings),
        app_context_(
            app::ApplicationContext::CreateFromStartupInfoNotChecked()),
        device_shell_context_binding_(this),
        account_provider_context_(this) {
    // 0a. Check if environment handle / services have been initialized.
    if (!app_context_->launcher()) {
      FTL_LOG(ERROR) << "Environment handle not set. Please use @boot.";
      exit(1);
    }
    if (!app_context_->environment_services()) {
      FTL_LOG(ERROR) << "Services handle not set. Please use @boot.";
      exit(1);
    }

    // 0b. Connect to the device runner monitor and check this
    // instance is the only one running, unless the command line asks
    // to ignore the monitor check.
    if (settings.ignore_monitor) {
      Start();

    } else {
      app_context_->ConnectToEnvironmentService(monitor_.NewRequest());

      monitor_.set_connection_error_handler([] {
        FTL_LOG(ERROR) << "No device runner monitor found. "
                       << " Please use @boot.";
        exit(1);
      });

      monitor_->GetConnectionCount([this](uint32_t count) {
        if (count != 1) {
          FTL_LOG(ERROR) << "Another device runner is running."
                         << " Please use that one, or shut it down first.";
          exit(1);
        }

        Start();
      });
    }
  }

 private:
  void Start() {
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

    ConnectToService(device_shell_services.get(), device_shell_.NewRequest());

    fidl::InterfaceHandle<mozart::ViewOwner> root_view;
    device_shell_view_provider->CreateView(root_view.NewRequest(), nullptr);
    app_context_->ConnectToEnvironmentService<mozart::Presenter>()->Present(
        std::move(root_view));

    device_shell_->Initialize(device_shell_context_binding_.NewBinding());

    // 2. Get login data for users of the device.
    // There might not be a file of users persisted. If config file doesn't
    // exist, move forward with no previous users.
    if (!settings_.no_minfs) {
      WaitForMinfs();
    }
    // TODO(alhaad): Use JSON instead of flatbuffers for better inspectablity.
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

    // 3. Start OAuth Token Manager App.
    app::ServiceProviderPtr token_manager_services;
    auto token_manager_launch_info = app::ApplicationLaunchInfo::New();
    token_manager_launch_info->url = "file:///system/apps/oauth_token_manager";
    token_manager_launch_info->services = token_manager_services.NewRequest();
    app_context_->launcher()->CreateApplication(
        std::move(token_manager_launch_info),
        token_manager_controller_.NewRequest());

    ConnectToService(token_manager_services.get(),
                     account_provider_.NewRequest());
    account_provider_->Initialize(account_provider_context_.NewBinding());

    // 4. Start the ledger.
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

  // |DeviceShellContext|
  void GetUserProvider(fidl::InterfaceRequest<UserProvider> request) override {
    user_provider_bindings_.AddBinding(this, std::move(request));
  }

  // |DeviceShellContext|
  // TODO(vardhan): Signal the ledger application to tear down.
  void Shutdown() override {
    FTL_LOG(INFO) << "DeviceShellContext::Shutdown()";
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
      const fidl::String& account_id,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<UserController> user_controller_request) override {
    // If requested, run in incognito mode.
    // TODO(alhaad): Revisit clean-up of local ledger state for incognito mode.
    if (account_id.is_null() || account_id == "") {
      FTL_LOG(INFO) << "UserProvider::Login() Incognito mode";
      // When running in incogito mode, we generate a random number. This number
      // serves as user_id, device_name and the filename for ledger repository.
      uint32_t random_number;
      size_t random_size;
      mx_status_t status =
          mx_cprng_draw(&random_number, sizeof random_number, &random_size);
      FTL_CHECK(status == NO_ERROR);
      FTL_CHECK(sizeof random_number == random_size);

      auto user_id = std::to_string(random_number);
      auto ledger_repository_path = kLedgerDataBaseDir + user_id;
      LoginInternal(user_id, user_id, ledger_repository_path,
            std::move(view_owner_request), std::move(user_controller_request));
      return;
    }

    // If not running in incognito mode, a corresponding entry must be present
    // in the users database.
    const UserStorage* found_user = nullptr;
    if (users_storage_) {
      for (const auto* user : *users_storage_->users()) {
        if (user->id()->str() == account_id) {
          found_user = user;
          break;
        }
      }
    }

    // If an entry is not found, we drop the incoming requests on the floor.
    if (!found_user) {
      FTL_LOG(INFO)
          << "The requested user was not found in the users database"
          << "It needs to be added first via UserProvider::AddUser().";
      return;
    }

    // Get the LedgerRepository for the user.
    std::string user_id = account_id;
    std::string ledger_repository_path = kLedgerDataBaseDir + user_id;

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

    FTL_LOG(INFO) << "UserProvider::Login() user: " << user_id;
    LoginInternal(user_id, found_user->device_name()->str(), ledger_repository_path,
          std::move(view_owner_request), std::move(user_controller_request));
  }

  // |UserProvider|
  void PreviousUsers(const PreviousUsersCallback& callback) override {
    fidl::Array<auth::AccountPtr> accounts =
        fidl::Array<auth::AccountPtr>::New(0);
    if (users_storage_) {
      for (const auto* user : *users_storage_->users()) {
        auto account = auth::Account::New();
        account->id = user->id()->str();
        switch (user->identity_provider()) {
          case IdentityProvider_DEV:
            account->identity_provider = auth::IdentityProvider::DEV;
            break;
          case IdentityProvider_GOOGLE:
            account->identity_provider = auth::IdentityProvider::GOOGLE;
            break;
          default:
            FTL_DCHECK(false) << "Unrecognized IdentityProvider" << user->identity_provider();
        }
        account->display_name = user->display_name()->str();
        accounts.push_back(std::move(account));
      }
    }
    callback(std::move(accounts));
  }

  // |UserProvider|
  void AddUser(auth::IdentityProvider identity_provider,
               const fidl::String& displayname,
               const fidl::String& devicename,
               const fidl::String& servername,
               const AddUserCallback& callback) override {
    account_provider_->AddAccount(
        identity_provider, displayname,
        [this, identity_provider, displayname, devicename, servername, callback](
            auth::AccountPtr account, const fidl::String& error_code) {
          if (account.is_null()) {
            callback(nullptr, error_code);
            return;
          }

          flatbuffers::FlatBufferBuilder builder;
          std::vector<flatbuffers::Offset<modular::UserStorage>> users;

          // Reserialize existing users.
          if (users_storage_) {
            for (const auto* user : *(users_storage_->users())) {
              users.push_back(modular::CreateUserStorage(
                  builder, builder.CreateString(user->id()), user->identity_provider(),
                  builder.CreateString(user->device_name()),
                  builder.CreateString(user->server_name())));
            }
          }

          modular::IdentityProvider flatbuffer_identity_provider;
          switch (account->identity_provider) {
            case auth::IdentityProvider::DEV:
              flatbuffer_identity_provider = modular::IdentityProvider::IdentityProvider_DEV;
              break;
            case auth::IdentityProvider::GOOGLE:
              flatbuffer_identity_provider = modular::IdentityProvider::IdentityProvider_GOOGLE;
              break;
            default:
              FTL_DCHECK(false) << "Unrecongized IDP.";
          }
          users.push_back(modular::CreateUserStorage(
              builder, builder.CreateString(account->id), flatbuffer_identity_provider,
              builder.CreateString(account->display_name),
              builder.CreateString(std::move(devicename)),
              builder.CreateString(std::move(servername))));

          builder.Finish(modular::CreateUsersStorage(
              builder, builder.CreateVector(std::move(users))));
          std::string new_serialized_users = std::string(
              reinterpret_cast<const char*>(builder.GetCurrentBufferPointer()),
              builder.GetSize());
          if (!Parse(new_serialized_users)) {
            callback(nullptr, "The user database seems corrupted.");
            return;
          }

          // Save users to disk.
          if (!files::CreateDirectory(
                  files::GetDirectoryName(kUsersConfigurationFile))) {
            callback(nullptr, "Unable to create directory.");
            return;
          }
          if (!files::WriteFile(kUsersConfigurationFile,
                                new_serialized_users.data(),
                                new_serialized_users.size())) {
            callback(nullptr, "Unable to write file.");
            return;
          }

          callback(std::move(account), error_code);
        });
  }

  // |AccountProviderContext|
  void GetAuthenticationContext(const fidl::String& account_id,
           fidl::InterfaceRequest<AuthenticationContext> request) override {
    device_shell_->GetAuthenticationContext(account_id, std::move(request));
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

  void LoginInternal(const std::string& user_id,
             const std::string& device_name,
             const std::string& local_ledger_path,
             fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
             fidl::InterfaceRequest<UserController> user_controller_request) {
    fidl::InterfaceHandle<ledger::LedgerRepository> ledger_repository;
    // TODO: Take the cloud sync server name as Login() parameter from the
    // device shell and/or from the user db.
    ledger_repository_factory_->GetRepository(
        local_ledger_path, fidl::String(""), ledger_repository.NewRequest(),
        [](ledger::Status status) {
          FTL_DCHECK(status == ledger::Status::OK)
              << "GetRepository failed: " << status;
        });

    // Get token provider factory for this user.
    auth::TokenProviderFactoryPtr token_provider_factory;
    account_provider_->GetTokenProviderFactory(
        user_id, token_provider_factory.NewRequest());

    user_controller_impl_.reset(new UserControllerImpl(
        app_context_, device_name, settings_.user_runner, settings_.user_shell,
        settings_.story_shell, std::move(token_provider_factory), user_id,
        std::move(ledger_repository), std::move(view_owner_request),
        std::move(user_controller_request),
        [this] { user_controller_impl_.reset(); }));
  }

  const Settings& settings_;  // Not owned nor copied.
  std::string serialized_users_;
  const modular::UsersStorage* users_storage_ = nullptr;

  std::shared_ptr<app::ApplicationContext> app_context_;
  DeviceRunnerMonitorPtr monitor_;

  fidl::Binding<DeviceShellContext> device_shell_context_binding_;
  fidl::BindingSet<UserProvider> user_provider_bindings_;
  fidl::Binding<auth::AccountProviderContext> account_provider_context_;

  app::ApplicationControllerPtr token_manager_controller_;
  auth::AccountProviderPtr account_provider_;

  app::ApplicationControllerPtr device_shell_controller_;
  DeviceShellPtr device_shell_;

  ledger::LedgerRepositoryFactoryPtr ledger_repository_factory_;
  app::ApplicationControllerPtr ledger_controller_;

  // TODO(alhaad): The framework allows simultaneous logins, this should
  // be an array of all logged in users.
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

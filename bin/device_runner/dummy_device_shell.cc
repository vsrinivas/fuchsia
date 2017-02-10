// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the DeviceShell service that passes a dummy user
// name to its UserProvider.

#include <memory>

#include "apps/modular/lib/fidl/single_service_view_app.h"
#include "apps/modular/services/device/device_context.fidl.h"
#include "apps/modular/services/device/device_shell.fidl.h"
#include "apps/modular/services/device/user_provider.fidl.h"
#include "apps/modular/src/device_runner/password_hash.h"
#include "apps/modular/src/device_runner/users_generated.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/flatbuffers/include/flatbuffers/flatbuffers.h"

namespace {

constexpr char kDefaultUsernameCommandLineArgument[] = "user";
constexpr char kUsersConfigurationFile[] = "/data/modular/device/users.db";

class Settings {
 public:
  bool Init() {
    if (files::IsFile(kUsersConfigurationFile)) {
      std::string serialized_users;
      if (!files::ReadFileToString(kUsersConfigurationFile,
                                   &serialized_users)) {
        // Unable to read file. Bailing out.
        FTL_LOG(WARNING) << "Unable to read user configuration file at: "
                         << kUsersConfigurationFile;
        return false;
      }

      if (!Parse(serialized_users)) {
        FTL_LOG(WARNING) << "Unable to read settings.";
        return false;
      }
    }
    return true;
  }

  bool AddUser(std::string user_name,
               std::string password,
               std::string server_id) {
    flatbuffers::FlatBufferBuilder builder;
    std::vector<flatbuffers::Offset<modular::UserStorage>> users;

    // Reserialize existing users.
    if (users_storage_) {
      for (const auto* user : *(users_storage_->users())) {
        users.push_back(modular::CreateUserStorage(
            builder, builder.CreateString(user->username()),
            builder.CreateString(user->password_hash()),
            builder.CreateString(user->server_name())));
      }
    }

    // Add the new user.
    std::string password_hash;
    if (!modular::HashPassword(password, &password_hash)) {
      return false;
    }
    users.push_back(modular::CreateUserStorage(
        builder, builder.CreateString(std::move(user_name)),
        builder.CreateString(std::move(password_hash)),
        builder.CreateString(std::move(server_id))));

    builder.Finish(modular::CreateUsersStorage(
        builder, builder.CreateVector(std::move(users))));
    std::string new_serialized_users = std::string(
        reinterpret_cast<const char*>(builder.GetCurrentBufferPointer()),
        builder.GetSize());
    if (!Parse(new_serialized_users)) {
      return false;
    }

    // Save users to disk.
    if (!files::CreateDirectory(
            files::GetDirectoryName(kUsersConfigurationFile))) {
      return false;
    }
    if (!files::WriteFile(kUsersConfigurationFile, new_serialized_users.data(),
                          new_serialized_users.size())) {
      return false;
    }

    return true;
  }

  const flatbuffers::Vector<flatbuffers::Offset<modular::UserStorage>>*
  users() {
    if (!users_storage_) {
      return nullptr;
    }
    return users_storage_->users();
  }

 private:
  bool Parse(std::string serialized_users) {
    flatbuffers::Verifier verifier(
        reinterpret_cast<const unsigned char*>(serialized_users.data()),
        serialized_users.size());
    if (!modular::VerifyUsersStorageBuffer(verifier)) {
      return false;
    }
    serialized_users_ = std::move(serialized_users);
    users_storage_ = modular::GetUsersStorage(serialized_users_.data());
    return true;
  }

  std::string serialized_users_;
  const modular::UsersStorage* users_storage_ = nullptr;
};

class DummyDeviceShellApp
    : public modular::SingleServiceViewApp<modular::DeviceShellFactory>,
      public modular::DeviceShell,
      public modular::UserWatcher {
 public:
  DummyDeviceShellApp(std::unique_ptr<Settings> settings,
                      std::unique_ptr<std::string> default_user)
      : settings_(std::move(settings)),
        default_user_(std::move(default_user)),
        device_shell_binding_(this),
        user_watcher_binding_(this) {}
  ~DummyDeviceShellApp() override = default;

 private:
  // |SingleServiceViewApp|
  void CreateView(
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<modular::ServiceProvider> services) override {
    view_owner_request_ = std::move(view_owner_request);
    Connect();
  }

  // |DeviceShellFactory|
  void Create(fidl::InterfaceHandle<modular::DeviceContext> device_context,
              fidl::InterfaceHandle<modular::UserProvider> user_provider,
              fidl::InterfaceRequest<modular::DeviceShell> device_shell_request)
      override {
    user_provider_.Bind(std::move(user_provider));
    device_context_.Bind(std::move(device_context));

    FTL_DCHECK(!device_shell_binding_.is_bound());
    device_shell_binding_.Bind(std::move(device_shell_request));

    Connect();
  }

  // |DeviceShell|
  void Terminate(const TerminateCallback& done) override {
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
    done();
  }

  // |UserWatcher|
  void OnLogout() override {
    FTL_LOG(INFO) << "User logged out. Starting shutdown.";
    device_context_->Shutdown();
  }

  void Connect() {
    if (!user_provider_ || !view_owner_request_) {
      return;
    }

    if (default_user_) {
      Login(*default_user_);
      return;
    }

    if (settings_->users() && settings_->users()->size() > 0) {
      // TODO(alhaad) If there is more than 1 user, the system should show a
      // chooser.
      Login(settings_->users()->Get(0)->username()->str());
      return;
    }

    // TODO(alhaad) Show a new user screen. In the mean time login with default
    // user.
    FTL_LOG(WARNING)
        << "Running with default user until new user screen is ready.";
    Login("user1");
  }

  void Login(const std::string& user_name) {
    user_provider_->Login(user_name, nullptr, std::move(view_owner_request_),
                          user_controller_.NewRequest());
    user_controller_->Watch(user_watcher_binding_.NewBinding());
  }

  std::unique_ptr<Settings> settings_;
  std::unique_ptr<std::string> default_user_;
  fidl::Binding<modular::DeviceShell> device_shell_binding_;
  fidl::Binding<modular::UserWatcher> user_watcher_binding_;
  fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request_;
  modular::DeviceContextPtr device_context_;
  modular::UserControllerPtr user_controller_;
  modular::UserProviderPtr user_provider_;
  FTL_DISALLOW_COPY_AND_ASSIGN(DummyDeviceShellApp);
};

std::unique_ptr<std::string> GetDefaultUser(
    const ftl::CommandLine& command_line) {
  // First, look for override in the command line.
  std::string user_name;
  if (command_line.GetOptionValue(kDefaultUsernameCommandLineArgument,
                                  &user_name)) {
    return std::make_unique<std::string>(std::move(user_name));
  }

  return nullptr;
}

}  // namespace

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);

  auto settings = std::make_unique<Settings>();
  if (!settings->Init()) {
    FTL_LOG(WARNING) << "Unable to init settings.";
    return 1;
  }

  auto default_user = GetDefaultUser(command_line);

  mtl::MessageLoop loop;
  DummyDeviceShellApp app(std::move(settings), std::move(default_user));
  loop.Run();
  return 0;
}

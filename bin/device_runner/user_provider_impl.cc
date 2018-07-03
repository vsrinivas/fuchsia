// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/device_runner/user_provider_impl.h"

#include <utility>

#include <lib/fxl/files/directory.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/files/path.h>
#include <lib/fxl/functional/make_copyable.h>
#include <lib/fxl/strings/string_printf.h>

#include "peridot/bin/device_runner/users_generated.h"
#include "peridot/lib/common/xdr.h"
#include "peridot/lib/fidl/clone.h"
#include "peridot/lib/fidl/json_xdr.h"

namespace modular {

namespace {

constexpr char kUsersConfigurationFile[] = "/data/modular/users-v5.db";

fuchsia::modular::auth::AccountPtr Convert(
    const fuchsia::modular::UserStorage* const user) {
  FXL_DCHECK(user);
  auto account = fuchsia::modular::auth::Account::New();
  account->id = user->id()->str();
  switch (user->identity_provider()) {
    case fuchsia::modular::IdentityProvider_DEV:
      account->identity_provider =
          fuchsia::modular::auth::IdentityProvider::DEV;
      break;
    case fuchsia::modular::IdentityProvider_GOOGLE:
      account->identity_provider =
          fuchsia::modular::auth::IdentityProvider::GOOGLE;
      break;
    default:
      FXL_DCHECK(false) << "Unrecognized IdentityProvider"
                        << user->identity_provider();
  }
  account->display_name = user->display_name()->str();
  if (account->display_name.is_null()) {
    account->display_name = "";
  }
  account->url = user->profile_url()->str();
  account->image_url = user->image_url()->str();
  return account;
}

std::string GetRandomId() {
  uint32_t random_number = 0;
  zx_cprng_draw(&random_number, sizeof random_number);
  return std::to_string(random_number);
}

}  // namespace

UserProviderImpl::UserProviderImpl(
    std::shared_ptr<fuchsia::sys::StartupContext> context,
    const fuchsia::modular::AppConfig& user_runner,
    const fuchsia::modular::AppConfig& default_user_shell,
    const fuchsia::modular::AppConfig& story_shell,
    fuchsia::modular::auth::AccountProvider* const account_provider)
    : context_(std::move(context)),
      user_runner_(user_runner),
      default_user_shell_(default_user_shell),
      story_shell_(story_shell),
      account_provider_(account_provider) {
  // There might not be a file of users persisted. If config file doesn't
  // exist, move forward with no previous users.
  // TODO(alhaad): Use JSON instead of flatbuffers for better inspectablity.
  if (files::IsFile(kUsersConfigurationFile)) {
    std::string serialized_users;
    if (!files::ReadFileToString(kUsersConfigurationFile, &serialized_users)) {
      // Unable to read file. Bailing out.
      FXL_LOG(ERROR) << "Unable to read user configuration file at: "
                     << kUsersConfigurationFile;
      return;
    }

    if (!Parse(serialized_users)) {
      return;
    }
  }
}

void UserProviderImpl::Connect(
    fidl::InterfaceRequest<fuchsia::modular::UserProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}

void UserProviderImpl::Teardown(const std::function<void()>& callback) {
  if (user_controllers_.empty()) {
    callback();
    return;
  }

  for (auto& it : user_controllers_) {
    auto cont = [this, ptr = it.first, callback] {
      // This is okay because during teardown, |cont| is never invoked
      // asynchronously.
      user_controllers_.erase(ptr);

      if (!user_controllers_.empty()) {
        // Not the last callback.
        return;
      }

      callback();
    };

    it.second->Logout(cont);
  }
}

void UserProviderImpl::Login(fuchsia::modular::UserLoginParams params) {
  // If requested, run in incognito mode.
  if (params.account_id.is_null() || params.account_id == "") {
    FXL_LOG(INFO) << "fuchsia::modular::UserProvider::Login() Incognito mode";
    LoginInternal(nullptr /* account */, std::move(params));
    return;
  }

  // If not running in incognito mode, a corresponding entry must be present
  // in the users database.
  const fuchsia::modular::UserStorage* found_user = nullptr;
  if (users_storage_) {
    for (const auto* user : *users_storage_->users()) {
      if (user->id()->str() == params.account_id) {
        found_user = user;
        break;
      }
    }
  }

  // If an entry is not found, we drop the incoming requests on the floor.
  if (!found_user) {
    FXL_LOG(INFO) << "The requested user was not found in the users database"
                  << "It needs to be added first via "
                     "fuchsia::modular::UserProvider::AddUser().";
    return;
  }

  FXL_LOG(INFO) << "fuchsia::modular::UserProvider::Login() account: "
                << params.account_id;
  LoginInternal(Convert(found_user), std::move(params));
}

void UserProviderImpl::PreviousUsers(PreviousUsersCallback callback) {
  fidl::VectorPtr<fuchsia::modular::auth::Account> accounts;
  accounts.resize(0);
  if (users_storage_) {
    for (const auto* user : *users_storage_->users()) {
      accounts.push_back(*Convert(user));
    }
  }
  callback(std::move(accounts));
}

void UserProviderImpl::AddUser(
    fuchsia::modular::auth::IdentityProvider identity_provider,
    AddUserCallback callback) {
  account_provider_->AddAccount(
      identity_provider, [this, identity_provider, callback](
                             fuchsia::modular::auth::AccountPtr account,
                             fidl::StringPtr error_code) {
        if (!account) {
          callback(nullptr, error_code);
          return;
        }

        flatbuffers::FlatBufferBuilder builder;
        std::vector<flatbuffers::Offset<fuchsia::modular::UserStorage>> users;

        // Reserialize existing users.
        if (users_storage_) {
          for (const auto* user : *(users_storage_->users())) {
            users.push_back(fuchsia::modular::CreateUserStorage(
                builder, builder.CreateString(user->id()),
                user->identity_provider(),
                builder.CreateString(user->display_name()),
                builder.CreateString(user->profile_url()),
                builder.CreateString(user->image_url())));
          }
        }

        fuchsia::modular::IdentityProvider flatbuffer_identity_provider;
        switch (account->identity_provider) {
          case fuchsia::modular::auth::IdentityProvider::DEV:
            flatbuffer_identity_provider =
                fuchsia::modular::IdentityProvider::IdentityProvider_DEV;
            break;
          case fuchsia::modular::auth::IdentityProvider::GOOGLE:
            flatbuffer_identity_provider =
                fuchsia::modular::IdentityProvider::IdentityProvider_GOOGLE;
            break;
          default:
            FXL_DCHECK(false) << "Unrecongized IDP.";
        }
        users.push_back(fuchsia::modular::CreateUserStorage(
            builder, builder.CreateString(account->id),
            flatbuffer_identity_provider,
            builder.CreateString(account->display_name),
            builder.CreateString(account->url),
            builder.CreateString(account->image_url)));

        builder.Finish(fuchsia::modular::CreateUsersStorage(
            builder, builder.CreateVector(users)));
        std::string new_serialized_users = std::string(
            reinterpret_cast<const char*>(builder.GetCurrentBufferPointer()),
            builder.GetSize());

        std::string error;
        if (!WriteUsersDb(new_serialized_users, &error)) {
          callback(nullptr, error);
          return;
        }

        callback(std::move(account), error_code);
      });
}

void UserProviderImpl::RemoveUser(fidl::StringPtr account_id,
                                  RemoveUserCallback callback) {
  fuchsia::modular::auth::AccountPtr account;
  if (users_storage_) {
    for (const auto* user : *users_storage_->users()) {
      if (user->id()->str() == account_id) {
        account = Convert(user);
      }
    }
  }

  if (!account) {
    callback("User not found.");
    return;
  }

  FXL_DCHECK(account_provider_);
  account_provider_->RemoveAccount(
      std::move(*account), false /* disable single logout*/,
      [this, account_id = account_id,
       callback](fuchsia::modular::auth::AuthErr auth_err) {
        if (auth_err.status != fuchsia::modular::auth::Status::OK) {
          callback(auth_err.message);
          return;
        }

        // update user storage after deleting user credentials.
        flatbuffers::FlatBufferBuilder builder;
        std::vector<flatbuffers::Offset<fuchsia::modular::UserStorage>> users;
        for (const auto* user : *(users_storage_->users())) {
          if (user->id()->str() == account_id) {
            // TODO(alhaad): We need to delete the local ledger data for a user
            // who has been removed. Re-visit this when sandboxing the user
            // runner.
            continue;
          }

          users.push_back(fuchsia::modular::CreateUserStorage(
              builder, builder.CreateString(user->id()),
              user->identity_provider(),
              builder.CreateString(user->display_name()),
              builder.CreateString(user->profile_url()),
              builder.CreateString(user->image_url())));
        }

        builder.Finish(fuchsia::modular::CreateUsersStorage(
            builder, builder.CreateVector(users)));
        std::string new_serialized_users = std::string(
            reinterpret_cast<const char*>(builder.GetCurrentBufferPointer()),
            builder.GetSize());

        std::string error;
        if (!WriteUsersDb(new_serialized_users, &error)) {
          FXL_LOG(ERROR) << "Writing to user database failed with: " << error;
          callback(error);
          return;
        }

        callback("");  // success
        return;
      });
}

bool UserProviderImpl::WriteUsersDb(const std::string& serialized_users,
                                    std::string* const error) {
  if (!Parse(serialized_users)) {
    *error = "The user database seems corrupted.";
    return false;
  }

  // Save users to disk.
  if (!files::CreateDirectory(
          files::GetDirectoryName(kUsersConfigurationFile))) {
    *error = "Unable to create directory.";
    return false;
  }
  if (!files::WriteFile(kUsersConfigurationFile, serialized_users.data(),
                        serialized_users.size())) {
    *error = "Unable to write file.";
    return false;
  }
  return true;
}

bool UserProviderImpl::Parse(const std::string& serialized_users) {
  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(serialized_users.data()),
      serialized_users.size());
  if (!fuchsia::modular::VerifyUsersStorageBuffer(verifier)) {
    FXL_LOG(ERROR) << "Unable to verify storage buffer.";
    return false;
  }
  serialized_users_ = serialized_users;
  users_storage_ = fuchsia::modular::GetUsersStorage(serialized_users_.data());
  return true;
}

void UserProviderImpl::LoginInternal(fuchsia::modular::auth::AccountPtr account,
                                     fuchsia::modular::UserLoginParams params) {
  // Get token provider factory for this user.
  fuchsia::modular::auth::TokenProviderFactoryPtr token_provider_factory;
  account_provider_->GetTokenProviderFactory(
      account ? account->id.get() : GetRandomId(),
      token_provider_factory.NewRequest());

  auto user_shell = params.user_shell_config
                        ? std::move(*params.user_shell_config)
                        : CloneStruct(default_user_shell_);
  auto controller = std::make_unique<UserControllerImpl>(
      context_->launcher().get(), CloneStruct(user_runner_),
      std::move(user_shell), CloneStruct(story_shell_),
      std::move(token_provider_factory), std::move(account),
      std::move(params.view_owner), std::move(params.services),
      std::move(params.user_controller),
      [this](UserControllerImpl* c) { user_controllers_.erase(c); });
  user_controllers_[controller.get()] = std::move(controller);
}

}  // namespace modular

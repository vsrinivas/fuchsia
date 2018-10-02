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

// Url of the application launching token manager
constexpr char kUserProviderAppUrl[] = "user_provider_url";

// Dev auth provider configuration
constexpr char kDevAuthProviderType[] = "dev";
constexpr char kDevAuthProviderUrl[] = "dev_auth_provider";

// Google auth provider configuration
constexpr char kGoogleAuthProviderType[] = "google";
constexpr char kGoogleAuthProviderUrl[] = "google_auth_provider";

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
  if (flatbuffers::IsFieldPresent(
          user, fuchsia::modular::UserStorage::VT_PROFILE_ID)) {
    account->profile_id = user->profile_id()->str();
  } else {
    account->profile_id = "";
  }
  return account;
}

std::string GetRandomId() {
  uint32_t random_number = 0;
  zx_cprng_draw(&random_number, sizeof random_number);
  return std::to_string(random_number);
}

// Returns the corresponding |auth_provider_type| string that maps to
// |fuchsia::modular::auth::IdentityProvider| value.
// TODO(ukode): Convert enum |fuchsia::modular::auth::IdentityProvider| to
// fidl::String datatype to make it consistent in the future.
std::string MapIdentityProviderToAuthProviderType(
    const fuchsia::modular::auth::IdentityProvider idp) {
  switch (idp) {
    case fuchsia::modular::auth::IdentityProvider::DEV:
      return kDevAuthProviderType;
    case fuchsia::modular::auth::IdentityProvider::GOOGLE:
      return kGoogleAuthProviderType;
  }
  FXL_DCHECK(false) << "Unrecognized IDP.";
}

// Returns a list of supported auth provider configurations that includes the
// type, startup parameters and the url of the auth provider component.
// TODO(ukode): This list will be derived from a config package in the future.
fidl::VectorPtr<fuchsia::auth::AuthProviderConfig> GetAuthProviderConfigs() {
  fuchsia::auth::AuthProviderConfig dev_auth_provider_config;
  dev_auth_provider_config.auth_provider_type = kDevAuthProviderType;
  dev_auth_provider_config.url = kDevAuthProviderUrl;

  fuchsia::auth::AuthProviderConfig google_auth_provider_config;
  google_auth_provider_config.auth_provider_type = kGoogleAuthProviderType;
  google_auth_provider_config.url = kGoogleAuthProviderUrl;

  fidl::VectorPtr<fuchsia::auth::AuthProviderConfig> auth_provider_configs;
  auth_provider_configs.push_back(std::move(google_auth_provider_config));
  auth_provider_configs.push_back(std::move(dev_auth_provider_config));

  return auth_provider_configs;
}

}  // namespace

UserProviderImpl::UserProviderImpl(
    std::shared_ptr<component::StartupContext> context,
    const fuchsia::modular::AppConfig& user_runner,
    const fuchsia::modular::AppConfig& default_user_shell,
    const fuchsia::modular::AppConfig& story_shell,
    fuchsia::modular::auth::AccountProvider* const account_provider,
    fuchsia::auth::TokenManagerFactory* const token_manager_factory,
    bool use_token_manager_factory, Delegate* const delegate)
    : context_(std::move(context)),
      user_runner_(user_runner),
      default_user_shell_(default_user_shell),
      story_shell_(story_shell),
      account_provider_(account_provider),
      token_manager_factory_(token_manager_factory),
      use_token_manager_factory_(use_token_manager_factory),
      delegate_(delegate),
      auth_context_provider_binding_(this) {
  FXL_DCHECK(delegate);

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
  if (!use_token_manager_factory_) {
    AddUserV1(identity_provider, std::move(callback));
  } else {
    AddUserV2(identity_provider, std::move(callback));
  }
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

  if (!use_token_manager_factory_) {
    RemoveUserV1(std::move(account), std::move(callback));
  } else {
    RemoveUserV2(std::move(account), std::move(callback));
  }
}

void UserProviderImpl::AddUserV1(
    const fuchsia::modular::auth::IdentityProvider identity_provider,
    AddUserCallback callback) {
  FXL_DCHECK(account_provider_);

  account_provider_->AddAccount(
      identity_provider, [this, identity_provider, callback](
                             fuchsia::modular::auth::AccountPtr account,
                             fidl::StringPtr error_code) {
        if (!account) {
          callback(nullptr, error_code);
          return;
        }

        std::string error;
        if (!AddUserToAccountsDB(fidl::Clone(account), &error)) {
          callback(nullptr, error);
          return;
        }

        callback(std::move(account), error_code);
      });
}

void UserProviderImpl::AddUserV2(
    const fuchsia::modular::auth::IdentityProvider identity_provider,
    AddUserCallback callback) {
  FXL_DCHECK(token_manager_factory_);

  // Creating a new user, the initial bootstrapping will be done by
  // AccountManager in the future. For now, create an account_id that
  // uniquely maps to a token manager instance at runtime.
  const std::string& account_id = GetRandomId();

  fuchsia::auth::TokenManagerPtr token_manager;
  token_manager_factory_->GetTokenManager(
      account_id, kUserProviderAppUrl, GetAuthProviderConfigs(),
      auth_context_provider_binding_.NewBinding(), token_manager.NewRequest());
  token_manager.set_error_handler([this, account_id] {
    FXL_LOG(INFO) << "Token Manager for account:" << account_id
                  << " disconnected";
  });

  // TODO(ukode): Fuchsia mod configuration that is requesting OAuth tokens.
  // This includes OAuth client specific details such as client id, secret,
  // list of scopes etc. These could be supplied by a config package in the
  // future.
  fuchsia::auth::AppConfig fuchsia_app_config;
  fuchsia_app_config.auth_provider_type =
      MapIdentityProviderToAuthProviderType(identity_provider);
  auto scopes = fidl::VectorPtr<fidl::StringPtr>::New(0);
  token_manager->Authorize(
      std::move(fuchsia_app_config), std::move(scopes), "", "",
      [this, identity_provider, account_id, callback](
          fuchsia::auth::Status status,
          fuchsia::auth::UserProfileInfoPtr user_profile_info) {
        if (status != fuchsia::auth::Status::OK) {
          FXL_DLOG(INFO) << "Token Manager Authorize() call returned error";
          callback(nullptr, "Failed to authorize user");
          return;
        }

        if (!user_profile_info) {
          FXL_DLOG(INFO) << "Token Manager Authorize() call returned empty "
                            "user profile info";
          callback(nullptr,
                   "Empty user profile info returned by auth_provider");
          return;
        }

        auto account = fuchsia::modular::auth::Account::New();
        account->id = account_id;
        account->identity_provider = identity_provider;
        account->display_name = user_profile_info->display_name;
        account->url = user_profile_info->url;
        account->image_url = user_profile_info->image_url;
        account->profile_id = user_profile_info->id;

        std::string error;
        if (!AddUserToAccountsDB(fidl::Clone(account), &error)) {
          callback(nullptr, error);
          return;
        }

        callback(std::move(account), "");
      });
}

bool UserProviderImpl::AddUserToAccountsDB(
    fuchsia::modular::auth::AccountPtr account, std::string* error) {
  FXL_DCHECK(account);

  flatbuffers::FlatBufferBuilder builder;
  std::vector<flatbuffers::Offset<fuchsia::modular::UserStorage>> users;

  // Reserialize existing users.
  if (users_storage_) {
    for (const auto* user : *(users_storage_->users())) {
      users.push_back(fuchsia::modular::CreateUserStorage(
          builder, builder.CreateString(user->id()), user->identity_provider(),
          builder.CreateString(user->display_name()),
          builder.CreateString(user->profile_url()),
          builder.CreateString(user->image_url()),
          builder.CreateString(user->profile_id())));
    }
  }

  auto account_identity_provider = account->identity_provider;
  auto flatbuffer_identity_provider = [account_identity_provider]() {
    switch (account_identity_provider) {
      case fuchsia::modular::auth::IdentityProvider::DEV:
        return fuchsia::modular::IdentityProvider::IdentityProvider_DEV;
      case fuchsia::modular::auth::IdentityProvider::GOOGLE:
        return fuchsia::modular::IdentityProvider::IdentityProvider_GOOGLE;
    }
    FXL_DCHECK(false) << "Unrecognized IDP.";
    // TODO(ukode): Move |UserStorage::identity_provider| to string
    // datatype. Use DEV identity provider as default in the interim.
    return fuchsia::modular::IdentityProvider::IdentityProvider_DEV;
  }();

  // Add new user
  users.push_back(fuchsia::modular::CreateUserStorage(
      builder, builder.CreateString(account->id), flatbuffer_identity_provider,
      builder.CreateString(account->display_name),
      builder.CreateString(account->url),
      builder.CreateString(account->image_url),
      builder.CreateString(account->id)));

  // Write user info to disk
  builder.Finish(fuchsia::modular::CreateUsersStorage(
      builder, builder.CreateVector(users)));
  std::string new_serialized_users = std::string(
      reinterpret_cast<const char*>(builder.GetCurrentBufferPointer()),
      builder.GetSize());

  return WriteUsersDb(new_serialized_users, error);
}

void UserProviderImpl::RemoveUserV1(fuchsia::modular::auth::AccountPtr account,
                                    RemoveUserCallback callback) {
  FXL_DCHECK(account);
  FXL_DCHECK(account_provider_);

  FXL_LOG(INFO) << "Removing user account :" << account->id;

  auto account_id = account->id;
  account_provider_->RemoveAccount(
      std::move(*account), false /* disable single logout*/,
      [this, account_id, callback](fuchsia::modular::auth::AuthErr auth_err) {
        if (auth_err.status != fuchsia::modular::auth::Status::OK) {
          FXL_LOG(ERROR) << "Error from RemoveAccount(): " << auth_err.message;
          callback(auth_err.message);
          return;
        }

        std::string error;
        if (!RemoveUserFromAccountsDB(account_id, &error)) {
          FXL_LOG(ERROR) << "Error in updating user database: " << error;
          callback(error);
          return;
        }

        callback("");  // success
      });
}

void UserProviderImpl::RemoveUserV2(fuchsia::modular::auth::AccountPtr account,
                                    RemoveUserCallback callback) {
  FXL_DCHECK(token_manager_factory_);
  FXL_DCHECK(account);
  auto account_id = account->id;

  fuchsia::auth::TokenManagerPtr token_manager;
  token_manager_factory_->GetTokenManager(
      account_id, kUserProviderAppUrl, GetAuthProviderConfigs(),
      auth_context_provider_binding_.NewBinding(), token_manager.NewRequest());
  token_manager.set_error_handler([this, account_id] {
    FXL_LOG(INFO) << "Token Manager for account:" << account_id
                  << " disconnected";
  });

  // TODO(ukode): Delete tokens for all the supported auth provider configs just
  // not Google. This will be replaced by AccountManager::RemoveUser api in the
  // future.
  fuchsia::auth::AppConfig fuchsia_app_config;
  fuchsia_app_config.auth_provider_type = kGoogleAuthProviderType;
  token_manager->DeleteAllTokens(
      fuchsia_app_config, account->profile_id,
      [this, account_id, callback](fuchsia::auth::Status status) {
        if (status != fuchsia::auth::Status::OK) {
          FXL_DLOG(INFO) << "Token Manager Authorize() call returned error";
          callback("Unable to remove user");
          return;
        }

        std::string error;
        if (!RemoveUserFromAccountsDB(account_id, &error)) {
          FXL_LOG(ERROR) << "Error in updating user database: " << error;
          callback(error);
          return;
        }

        callback("");  // success
      });
}

// Update user storage after deleting user credentials.
bool UserProviderImpl::RemoveUserFromAccountsDB(fidl::StringPtr account_id,
                                                std::string* error) {
  FXL_DCHECK(account_id);
  FXL_DCHECK(error);

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
        builder, builder.CreateString(user->id()), user->identity_provider(),
        builder.CreateString(user->display_name()),
        builder.CreateString(user->profile_url()),
        builder.CreateString(user->image_url()),
        builder.CreateString(user->profile_id())));
  }

  builder.Finish(fuchsia::modular::CreateUsersStorage(
      builder, builder.CreateVector(users)));
  std::string new_serialized_users = std::string(
      reinterpret_cast<const char*>(builder.GetCurrentBufferPointer()),
      builder.GetSize());

  return WriteUsersDb(new_serialized_users, error);
}

void UserProviderImpl::GetAuthenticationUIContext(
    fidl::InterfaceRequest<fuchsia::auth::AuthenticationUIContext> request) {
  // TODO(ukode): Provide impl here.
  FXL_LOG(INFO) << "GetAuthenticationUIContext() is unimplemented.";
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
  FXL_DCHECK(token_manager_factory_);
  auto account_id = account ? account->id.get() : GetRandomId();

  // Get |fuchsia::modular::auth::TokenManagerFactory| for this user.
  fuchsia::modular::auth::TokenProviderFactoryPtr token_provider_factory;
  account_provider_->GetTokenProviderFactory(
      account_id, token_provider_factory.NewRequest());

  // Get a new |fuchsia::auth::TokenManager| handle for this user, that is
  // passed to user_runner.
  // TODO(ukode): Maybe its better to pass token_manager_factory_ptr to
  // user_runner.
  fuchsia::auth::TokenManagerPtr token_manager;
  token_manager_factory_->GetTokenManager(
      account_id, kUserProviderAppUrl, GetAuthProviderConfigs(),
      auth_context_provider_binding_.NewBinding(), token_manager.NewRequest());

  auto user_shell = params.user_shell_config
                        ? std::move(*params.user_shell_config)
                        : CloneStruct(default_user_shell_);

  auto view_owner =
      delegate_->GetUserShellViewOwner(std::move(params.view_owner));
  auto service_provider =
      delegate_->GetUserShellServiceProvider(std::move(params.services));

  auto controller = std::make_unique<UserControllerImpl>(
      context_->launcher().get(), CloneStruct(user_runner_),
      std::move(user_shell), CloneStruct(story_shell_),
      std::move(token_provider_factory), std::move(token_manager),
      std::move(account), std::move(view_owner), std::move(service_provider),
      std::move(params.user_controller), [this](UserControllerImpl* c) {
        user_controllers_.erase(c);
        delegate_->DidLogout();
      });
  auto controller_ptr = controller.get();
  user_controllers_[controller_ptr] = std::move(controller);

  delegate_->DidLogin();
}

FuturePtr<> UserProviderImpl::SwapUserShell(
    fuchsia::modular::AppConfig user_shell_config) {
  if (user_controllers_.size() == 0)
    return Future<>::CreateCompleted("SwapUserShell(Completed)");

  FXL_CHECK(user_controllers_.size() == 1)
      << user_controllers_.size()
      << " user controllers exist, which should be impossible.";

  auto user_controller = user_controllers_.begin()->first;
  return user_controller->SwapUserShell(std::move(user_shell_config));
}

}  // namespace modular

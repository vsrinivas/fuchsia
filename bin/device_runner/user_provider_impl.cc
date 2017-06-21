// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/device_runner/user_provider_impl.h"

#include "apps/modular/src/device_runner/users_generated.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/strings/string_printf.h"

namespace modular {

namespace {

// TODO(alhaad): This is also defined in device_runner.cc. Reconcile!
constexpr char kLedgerAppUrl[] = "file:///system/apps/ledger";
constexpr char kLedgerDataBaseDir[] = "/data/ledger/";
constexpr char kUsersConfigurationFile[] = "/data/modular/device/users-v4.db";

// TODO(alhaad): Once ledger starts using user's firebase id to namespace the
// user's cloud instance, use account id instead of display name for |user_id|.
std::string LedgerRepositoryPath(const std::string& user_id) {
  return kLedgerDataBaseDir + user_id;
}

}  // namespace

UserProviderImpl::UserProviderImpl(
    std::shared_ptr<app::ApplicationContext> app_context,
    const AppConfig& default_user_shell,
    const AppConfig& story_shell,
    ledger::LedgerRepositoryFactory* const ledger_repository_factory,
    const bool ledger_repository_for_testing,
    auth::AccountProvider* const account_provider)
    : app_context_(app_context),
      default_user_shell_(default_user_shell),
      story_shell_(story_shell),
      ledger_repository_factory_(ledger_repository_factory),
      ledger_repository_for_testing_(ledger_repository_for_testing),
      account_provider_(account_provider) {
  // There might not be a file of users persisted. If config file doesn't
  // exist, move forward with no previous users.
  // TODO(alhaad): Use JSON instead of flatbuffers for better inspectablity.
  if (files::IsFile(kUsersConfigurationFile)) {
    std::string serialized_users;
    if (!files::ReadFileToString(kUsersConfigurationFile, &serialized_users)) {
      // Unable to read file. Bailing out.
      FTL_LOG(ERROR) << "Unable to read user configuration file at: "
                     << kUsersConfigurationFile;
      return;
    }

    if (!Parse(serialized_users)) {
      return;
    }
  }
}

void UserProviderImpl::Connect(fidl::InterfaceRequest<UserProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}

void UserProviderImpl::Teardown(const std::function<void()>& callback) {
  if (user_controllers_.empty()) {
    callback();
    return;
  }

  for (auto& it : user_controllers_) {
    auto cont = [ this, ptr = it.first, callback ] {
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

void UserProviderImpl::Login(UserLoginParamsPtr params) {
  // If requested, run in incognito mode.
  // TODO(alhaad): Revisit clean-up of local ledger state for incognito mode.
  if (params->account_id.is_null() || params->account_id == "") {
    FTL_LOG(INFO) << "UserProvider::Login() Incognito mode";
    // When running in incogito mode, we generate a random number. This number
    // serves as account_id and the filename for ledger repository.
    uint32_t random_number;
    size_t random_size;
    mx_status_t status =
        mx_cprng_draw(&random_number, sizeof random_number, &random_size);
    FTL_CHECK(status == MX_OK);
    FTL_CHECK(sizeof random_number == random_size);

    auto random_id = std::to_string(random_number);
    LoginInternal(random_id, nullptr /* server_name */,
                  LedgerRepositoryPath(random_id), std::move(params));
    return;
  }

  // If not running in incognito mode, a corresponding entry must be present
  // in the users database.
  const UserStorage* found_user = nullptr;
  if (users_storage_) {
    for (const auto* user : *users_storage_->users()) {
      if (user->id()->str() == params->account_id) {
        found_user = user;
        break;
      }
    }
  }

  // If an entry is not found, we drop the incoming requests on the floor.
  if (!found_user) {
    FTL_LOG(INFO) << "The requested user was not found in the users database"
                  << "It needs to be added first via UserProvider::AddUser().";
    return;
  }

  // Get the LedgerRepository for the user.
  // |user_id| has to be something that is the same across devices. Currently,
  // we take it as input from the user. TODO(alhaad): Infer it from id token.
  std::string user_id = found_user->display_name()->str();
  std::string ledger_repository_path = LedgerRepositoryPath(user_id);

  if (ledger_repository_for_testing_) {
    unsigned random_number;
    size_t random_size;
    mx_status_t status =
        mx_cprng_draw(&random_number, sizeof random_number, &random_size);
    FTL_CHECK(status == MX_OK);
    FTL_CHECK(sizeof random_number == random_size);

    ledger_repository_path +=
        ftl::StringPrintf("_for_testing_%X", random_number);
    FTL_LOG(INFO) << "Using testing ledger repository path: "
                  << ledger_repository_path;
  }

  FTL_LOG(INFO) << "UserProvider::Login() user: " << user_id;
  LoginInternal(params->account_id, found_user->server_name()->str(),
                ledger_repository_path, std::move(params));
}

void UserProviderImpl::PreviousUsers(const PreviousUsersCallback& callback) {
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
          FTL_DCHECK(false)
              << "Unrecognized IdentityProvider" << user->identity_provider();
      }
      account->display_name = user->display_name()->str();
      account->url = user->profile_url()->str();
      account->image_url = user->image_url()->str();
      accounts.push_back(std::move(account));
    }
  }
  callback(std::move(accounts));
}

void UserProviderImpl::AddUser(auth::IdentityProvider identity_provider,
                               const fidl::String& displayname,
                               const fidl::String& devicename, // TODO(zbowling): deprecated field
                               const fidl::String& servername,
                               const AddUserCallback& callback) {
  account_provider_->AddAccount(
      identity_provider,
      [this, identity_provider, displayname, servername, callback](
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
                builder, builder.CreateString(user->id()),
                user->identity_provider(),
                builder.CreateString(user->display_name()),
                builder.CreateString(user->server_name()),
                builder.CreateString(user->profile_url()),
                builder.CreateString(user->image_url())));
          }
        }

        modular::IdentityProvider flatbuffer_identity_provider;
        switch (account->identity_provider) {
          case auth::IdentityProvider::DEV:
            flatbuffer_identity_provider =
                modular::IdentityProvider::IdentityProvider_DEV;
            break;
          case auth::IdentityProvider::GOOGLE:
            flatbuffer_identity_provider =
                modular::IdentityProvider::IdentityProvider_GOOGLE;
            break;
          default:
            FTL_DCHECK(false) << "Unrecongized IDP.";
        }
        users.push_back(modular::CreateUserStorage(
            builder, builder.CreateString(account->id),
            flatbuffer_identity_provider,
            builder.CreateString(account->display_name),
            builder.CreateString(std::move(servername)),
            builder.CreateString(account->url),
            builder.CreateString(account->image_url)));

        builder.Finish(modular::CreateUsersStorage(
            builder, builder.CreateVector(std::move(users))));
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

// TODO(alhaad, security): This does not remove tokens stored by the token
// manager. That should be done properly by invalidaing the tokens. Re-visit
// this!
void UserProviderImpl::RemoveUser(const fidl::String& account_id) {
  if (!users_storage_) {
    return;
  }

  flatbuffers::FlatBufferBuilder builder;
  std::vector<flatbuffers::Offset<modular::UserStorage>> users;
  for (const auto* user : *(users_storage_->users())) {
    if (user->id()->str() == account_id) {
      // Delete the local ledger repository for this user too.
      std::string user_id = user->display_name()->str();
      files::DeletePath(LedgerRepositoryPath(user_id), true);
      continue;
    }

    users.push_back(modular::CreateUserStorage(
        builder, builder.CreateString(user->id()), user->identity_provider(),
        builder.CreateString(user->display_name()),
        builder.CreateString(user->server_name()),
        builder.CreateString(user->profile_url()),
        builder.CreateString(user->image_url())));
  }

  builder.Finish(modular::CreateUsersStorage(
      builder, builder.CreateVector(std::move(users))));
  std::string new_serialized_users = std::string(
      reinterpret_cast<const char*>(builder.GetCurrentBufferPointer()),
      builder.GetSize());

  std::string error;
  if (!WriteUsersDb(new_serialized_users, &error)) {
    FTL_LOG(ERROR) << "Writing to user database failed with: " << error;
  }
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
  if (!modular::VerifyUsersStorageBuffer(verifier)) {
    FTL_LOG(ERROR) << "Unable to verify storage buffer.";
    return false;
  }
  serialized_users_ = std::move(serialized_users);
  users_storage_ = modular::GetUsersStorage(serialized_users_.data());
  return true;
}

void UserProviderImpl::LoginInternal(const std::string& account_id,
                                     const fidl::String& server_name,
                                     const std::string& local_ledger_path,
                                     UserLoginParamsPtr params) {
  // Get token provider factory for this user.
  auth::TokenProviderFactoryPtr token_provider_factory;
  account_provider_->GetTokenProviderFactory(
      account_id, token_provider_factory.NewRequest());

  // Get a token provider instance to pass to ledger.
  fidl::InterfaceHandle<auth::TokenProvider> ledger_token_provider;
  token_provider_factory->GetTokenProvider(kLedgerAppUrl,
                                           ledger_token_provider.NewRequest());

  fidl::InterfaceHandle<ledger::LedgerRepository> ledger_repository;
  ledger_repository_factory_->GetRepository(
      local_ledger_path, server_name, std::move(ledger_token_provider),
      ledger_repository.NewRequest(), [](ledger::Status status) {
        FTL_DCHECK(status == ledger::Status::OK)
            << "GetRepository failed: " << status;
      });

  fidl::InterfaceHandle<auth::TokenProvider> ledger_token_provider_for_erase;
  token_provider_factory->GetTokenProvider(kLedgerAppUrl,
                                           ledger_token_provider_for_erase.NewRequest());
  auto user_shell = params->user_shell_config.is_null()
                        ? default_user_shell_.Clone()
                        : std::move(params->user_shell_config);
  auto controller = std::make_unique<UserControllerImpl>(
      app_context_, std::move(user_shell), story_shell_,
      std::move(token_provider_factory), account_id,
      std::move(ledger_repository), std::move(params->view_owner),
      std::move(params->user_controller), ftl::MakeCopyable([
        this, local_ledger_path, server_name,
        ledger_token_provider_for_erase =
            std::move(ledger_token_provider_for_erase)
      ]() mutable {
        ledger_repository_factory_->EraseRepository(
            local_ledger_path, server_name,
            std::move(ledger_token_provider_for_erase),
            [](ledger::Status status) {
              if (status != ledger::Status::OK) {
                FTL_LOG(ERROR) << "EraseRepository failed: " << status;
              }
            });
      }),
      [this](UserControllerImpl* c) { user_controllers_.erase(c); });
  user_controllers_[controller.get()] = std::move(controller);
}

}  // namespace modular

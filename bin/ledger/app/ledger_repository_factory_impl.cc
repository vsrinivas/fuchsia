// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/ledger_repository_factory_impl.h"

#include "apps/ledger/src/app/auth_provider_impl.h"
#include "apps/ledger/src/app/constants.h"
#include "apps/ledger/src/backoff/exponential_backoff.h"
#include "apps/ledger/src/cloud_sync/impl/user_sync_impl.h"
#include "apps/tracing/lib/trace/event.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/strings/concatenate.h"
#include "lib/ftl/strings/string_view.h"

namespace ledger {

namespace {
ftl::StringView GetStorageDirectoryName(ftl::StringView repository_path) {
  size_t separator = repository_path.rfind('/');
  FTL_DCHECK(separator != std::string::npos);
  FTL_DCHECK(separator != repository_path.size() - 1);

  return repository_path.substr(separator + 1);
}

cloud_sync::UserConfig GetUserConfig(const fidl::String& server_id,
                                     ftl::StringView user_id,
                                     ftl::StringView user_directory,
                                     cloud_sync::AuthProvider* auth_provider) {
  if (!server_id || server_id.size() == 0) {
    cloud_sync::UserConfig user_config;
    user_config.use_sync = false;
    return user_config;
  }

  cloud_sync::UserConfig user_config;
  user_config.use_sync = true;
  user_config.server_id = server_id.get();
  user_config.user_id = user_id.ToString();
  user_config.user_directory = user_directory.ToString();
  user_config.auth_provider = auth_provider;
  return user_config;
}

bool SaveConfigForDebugging(ftl::StringView user_id,
                            ftl::StringView repository_path,
                            const std::string& temp_dir) {
  if (!files::WriteFileInTwoPhases(kLastUserIdPath.ToString(), user_id,
                                   temp_dir)) {
    return false;
  }
  if (!files::WriteFileInTwoPhases(kLastUserRepositoryPath.ToString(),
                                   repository_path, temp_dir)) {
    return false;
  }
  return true;
}

// Verifies that the current server id is not different from the server id used
// in a previous run.
//
// Ledger does not support cloud migrations - once the repository is synced with
// a cloud, we can't change the server.
bool CheckSyncConfig(const cloud_sync::UserConfig& user_config,
                     ftl::StringView repository_path,
                     const std::string& temp_dir) {
  if (!user_config.use_sync) {
    return true;
  }

  std::string server_id_path =
      ftl::Concatenate({repository_path, "/", kServerIdFilename});
  if (files::IsFile(server_id_path)) {
    std::string previous_server_id;
    if (!files::ReadFileToString(server_id_path, &previous_server_id)) {
      FTL_LOG(ERROR) << "Failed to read the previous server id for "
                     << "compatibility check";
      return false;
    }

    if (previous_server_id != user_config.server_id) {
      FTL_LOG(ERROR) << "Mismatch between the previous server id: "
                     << previous_server_id
                     << " and the current one: " << user_config.server_id;
      FTL_LOG(ERROR) << "Ledger does not support cloud migrations. If you need "
                     << "to change the server, reset Ledger using "
                     << "`ledger_tool clean`";
      return false;
    }

    return true;
  }

  std::string temp_dir_root = ftl::Concatenate({repository_path, "/tmp"});
  if (!files::WriteFileInTwoPhases(server_id_path, user_config.server_id,
                                   temp_dir)) {
    FTL_LOG(ERROR) << "Failed to write the current server_id for compatibility "
                   << "check.";
    return false;
  }

  return true;
}

}  // namespace

// Container for a LedgerRepositoryImpl that keeps tracks of the in-flight FIDL
// requests and callbacks and fires them when the repository is available.
// TODO(ppi): LE-224 extract this into a generic class shared with
// LedgerManager.
class LedgerRepositoryFactoryImpl::LedgerRepositoryContainer {
 public:
  LedgerRepositoryContainer(
      std::unique_ptr<cloud_sync::AuthProvider> auth_provider)
      : status_(Status::OK), auth_provider_(std::move(auth_provider)) {}
  ~LedgerRepositoryContainer() {
    for (const auto& request : requests_) {
      request.second(Status::INTERNAL_ERROR);
    }
  }

  void set_on_empty(const ftl::Closure& on_empty_callback) {
    on_empty_callback_ = on_empty_callback;
    if (ledger_repository_) {
      ledger_repository_->set_on_empty(on_empty_callback);
    }
  };

  // Keeps track of |request| and |callback|. Binds |request| and fires
  // |callback| when the repository is available or an error occurs.
  void BindRepository(fidl::InterfaceRequest<LedgerRepository> request,
                      std::function<void(Status)> callback) {
    if (status_ != Status::OK) {
      callback(status_);
      return;
    }
    if (ledger_repository_) {
      ledger_repository_->BindRepository(std::move(request));
      callback(status_);
      return;
    }
    requests_.push_back(
        std::make_pair(std::move(request), std::move(callback)));
  }

  // Sets the implementation or the error status for the container. This
  // notifies all awaiting callbacks and binds all pages in case of success.
  void SetRepository(Status status,
                     std::unique_ptr<LedgerRepositoryImpl> ledger_repository) {
    FTL_DCHECK(!ledger_repository_);
    FTL_DCHECK(status != Status::OK || ledger_repository);
    status_ = status;
    ledger_repository_ = std::move(ledger_repository);
    for (auto it = requests_.begin(); it != requests_.end(); ++it) {
      if (ledger_repository_) {
        ledger_repository_->BindRepository(std::move(it->first));
      }
      it->second(status_);
    }
    requests_.clear();
    if (on_empty_callback_) {
      if (ledger_repository_) {
        ledger_repository_->set_on_empty(on_empty_callback_);
      } else {
        on_empty_callback_();
      }
    }
  }

 private:
  std::unique_ptr<LedgerRepositoryImpl> ledger_repository_;
  Status status_;
  std::unique_ptr<cloud_sync::AuthProvider> auth_provider_;
  std::vector<std::pair<fidl::InterfaceRequest<LedgerRepository>,
                        std::function<void(Status)>>>
      requests_;
  ftl::Closure on_empty_callback_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryContainer);
};

LedgerRepositoryFactoryImpl::LedgerRepositoryFactoryImpl(
    Delegate* delegate,
    ledger::Environment* environment,
    ConfigPersistence config_persistence)
    : delegate_(delegate),
      environment_(environment),
      config_persistence_(config_persistence) {}

LedgerRepositoryFactoryImpl::~LedgerRepositoryFactoryImpl() {}

void LedgerRepositoryFactoryImpl::GetRepository(
    const fidl::String& repository_path,
    const fidl::String& server_id,
    fidl::InterfaceHandle<modular::auth::TokenProvider> token_provider,
    fidl::InterfaceRequest<LedgerRepository> repository_request,
    const GetRepositoryCallback& callback) {
  TRACE_DURATION("ledger", "repository_factory_get_repository");
  std::string sanitized_path =
      files::SimplifyPath(std::move(repository_path.get()));
  auto it = repositories_.find(sanitized_path);
  if (it != repositories_.end()) {
    it->second.BindRepository(std::move(repository_request),
                              std::move(callback));
    return;
  }

  auto token_provider_ptr =
      modular::auth::TokenProviderPtr::Create(std::move(token_provider));
  if (token_provider_ptr) {
    token_provider_ptr.set_connection_error_handler([this, sanitized_path] {
      FTL_LOG(ERROR) << "Lost connection to TokenProvider, "
                     << "shutting down the repository.";
      auto find_repository = repositories_.find(sanitized_path);
      FTL_DCHECK(find_repository != repositories_.end());
      repositories_.erase(find_repository);
    });
  }
  auto auth_provider = std::make_unique<AuthProviderImpl>(
      environment_->main_runner(), std::move(token_provider_ptr));
  cloud_sync::AuthProvider* auth_provider_ptr = auth_provider.get();

  auto ret = repositories_.emplace(
      std::piecewise_construct, std::forward_as_tuple(sanitized_path),
      std::forward_as_tuple(std::move(auth_provider)));
  LedgerRepositoryContainer* container = &ret.first->second;
  container->BindRepository(std::move(repository_request), std::move(callback));

  auth_provider_ptr->GetFirebaseUserId(ftl::MakeCopyable([
    this, sanitized_path = std::move(sanitized_path),
    server_id = server_id.get(), auth_provider_ptr, container
  ](std::string user_id) {
    if (user_id.empty()) {
      user_id = GetStorageDirectoryName(sanitized_path).ToString();
    }
    cloud_sync::UserConfig user_config =
        GetUserConfig(server_id, user_id, sanitized_path, auth_provider_ptr);
    CreateRepository(container, std::move(sanitized_path),
                     std::move(user_config));

  }));
}

void LedgerRepositoryFactoryImpl::EraseRepository(
    const fidl::String& repository_path,
    const fidl::String& server_id,
    fidl::InterfaceHandle<modular::auth::TokenProvider> token_provider,
    const EraseRepositoryCallback& callback) {
  std::string sanitized_path =
      files::SimplifyPath(std::move(repository_path.get()));
  auto find_repository = repositories_.find(sanitized_path);
  if (find_repository != repositories_.end()) {
    FTL_LOG(WARNING) << "The repository to be erased is running, "
                     << "shutting it down before erasing.";
    repositories_.erase(find_repository);
  }

  auto token_provider_ptr =
      modular::auth::TokenProviderPtr::Create(std::move(token_provider));

  delegate_->EraseRepository(
      EraseRepositoryOperation(environment_->main_runner(),
                               environment_->network_service(),
                               std::move(sanitized_path), server_id.get(),
                               std::move(token_provider_ptr)),
      [callback = std::move(callback)](bool succeeded) {
        if (succeeded) {
          callback(Status::OK);
        } else {
          callback(Status::INTERNAL_ERROR);
        }
      });
}

void LedgerRepositoryFactoryImpl::CreateRepository(
    LedgerRepositoryContainer* container,
    std::string repository_path,
    cloud_sync::UserConfig user_config) {
  if (!user_config.use_sync &&
      config_persistence_ == ConfigPersistence::PERSIST) {
    FTL_LOG(WARNING) << "No sync configuration set, "
                     << "Ledger will work locally but won't sync";
  }
  const std::string temp_dir = ftl::Concatenate({repository_path, "/tmp"});
  if (config_persistence_ == ConfigPersistence::PERSIST &&
      !CheckSyncConfig(user_config, repository_path, temp_dir)) {
    container->SetRepository(Status::CONFIGURATION_ERROR, nullptr);
    return;
  }
  // Save debugging data for `ledger_tool`.
  if (config_persistence_ == ConfigPersistence::PERSIST &&
      !SaveConfigForDebugging(user_config.user_id, repository_path, temp_dir)) {
    FTL_LOG(WARNING) << "Failed to save the current configuration.";
  }
  std::unique_ptr<SyncWatcherSet> watchers = std::make_unique<SyncWatcherSet>();
  auto user_sync = std::make_unique<cloud_sync::UserSyncImpl>(
      environment_, std::move(user_config),
      std::make_unique<backoff::ExponentialBackoff>(), watchers.get());
  user_sync->Start();
  auto repository = std::make_unique<LedgerRepositoryImpl>(
      repository_path, environment_, std::move(watchers), std::move(user_sync));
  container->SetRepository(Status::OK, std::move(repository));
}

}  // namespace ledger

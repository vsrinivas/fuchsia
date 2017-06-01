// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/ledger_repository_factory_impl.h"

#include "apps/ledger/src/app/auth_provider_impl.h"
#include "apps/ledger/src/app/constants.h"
#include "apps/ledger/src/backoff/exponential_backoff.h"
#include "apps/ledger/src/cloud_sync/impl/user_sync_impl.h"
#include "apps/ledger/src/cloud_sync/public/user_config.h"
#include "apps/tracing/lib/trace/event.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/files/scoped_temp_dir.h"
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

cloud_sync::UserConfig GetUserConfig(
    const fidl::String& server_id,
    ftl::StringView user_id,
    ftl::StringView user_directory,
    ftl::RefPtr<ftl::TaskRunner> task_runner,
    modular::auth::TokenProviderPtr token_provider) {
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
  user_config.auth_provider = std::make_unique<AuthProviderImpl>(
      task_runner, std::move(token_provider));
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

LedgerRepositoryFactoryImpl::LedgerRepositoryFactoryImpl(
    ledger::Environment* environment,
    ConfigPersistence config_persistence)
    : environment_(environment), config_persistence_(config_persistence) {}

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
  if (it == repositories_.end()) {
    ftl::StringView user_id = GetStorageDirectoryName(sanitized_path);
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
    cloud_sync::UserConfig user_config = GetUserConfig(
        server_id, user_id, sanitized_path, environment_->main_runner(),
        std::move(token_provider_ptr));
    if (!user_config.use_sync &&
        config_persistence_ == ConfigPersistence::PERSIST) {
      FTL_LOG(WARNING) << "No sync configuration set, "
                       << "Ledger will work locally but won't sync";
    }
    const std::string temp_dir =
        ftl::Concatenate({repository_path.get(), "/tmp"});
    if (config_persistence_ == ConfigPersistence::PERSIST &&
        !CheckSyncConfig(user_config, sanitized_path, temp_dir)) {
      callback(Status::CONFIGURATION_ERROR);
      return;
    }
    // Save debugging data for `ledger_tool`.
    if (config_persistence_ == ConfigPersistence::PERSIST &&
        !SaveConfigForDebugging(user_id, repository_path.get(), temp_dir)) {
      FTL_LOG(WARNING) << "Failed to save the current configuration.";
    }
    auto user_sync = std::make_unique<cloud_sync::UserSyncImpl>(
        environment_, std::move(user_config),
        std::make_unique<backoff::ExponentialBackoff>());
    user_sync->Start();
    auto result = repositories_.emplace(
        std::piecewise_construct, std::forward_as_tuple(sanitized_path),
        std::forward_as_tuple(sanitized_path, environment_,
                              std::move(user_sync)));
    FTL_DCHECK(result.second);
    it = result.first;
  }
  it->second.BindRepository(std::move(repository_request));
  callback(Status::OK);
}

}  // namespace ledger

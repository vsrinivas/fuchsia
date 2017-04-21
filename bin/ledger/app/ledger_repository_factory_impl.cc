// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/ledger_repository_factory_impl.h"

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
constexpr ftl::StringView kServerIdFilename = "server_id";

ftl::StringView GetStorageDirectoryName(ftl::StringView repository_path) {
  size_t separator = repository_path.rfind('/');
  FTL_DCHECK(separator != std::string::npos);
  FTL_DCHECK(separator != repository_path.size() - 1);

  return repository_path.substr(separator + 1);
}

cloud_sync::UserConfig GetUserConfig(
    const configuration::Configuration& global_config,
    const fidl::String& server_id,
    ftl::StringView user_id) {
  if (!server_id) {
    cloud_sync::UserConfig user_config;
    user_config.use_sync = false;
    return user_config;
  }

  if (server_id.size()) {
    cloud_sync::UserConfig user_config;
    user_config.use_sync = true;
    user_config.server_id = server_id.get();
    user_config.user_id = user_id.ToString();
    return user_config;
  }

  // |server_id| wasn't provided by Framework, default to the values from the
  // global config file.
  cloud_sync::UserConfig user_config;
  if (!global_config.use_sync) {
    user_config.use_sync = false;
    return user_config;
  }

  user_config.use_sync = true;
  user_config.server_id = global_config.sync_params.firebase_id;
  user_config.cloud_prefix = global_config.sync_params.cloud_prefix;
  user_config.user_id = user_id.ToString();
  FTL_LOG(WARNING) << "Sync configuration not specified by Framework, "
                   << "using the server id: " << user_config.server_id
                   << " specified in the global config file";
  return user_config;
}

bool WriteFileInTwoPhases(ftl::StringView data,
                          const std::string& temp_root,
                          const std::string& destination_path) {
  files::ScopedTempDir temp_dir(temp_root);

  std::string temp_file_path;
  if (!temp_dir.NewTempFile(&temp_file_path)) {
    FTL_LOG(ERROR)
        << "Failed to create a temporary file for a two-phase write.";
    return false;
  }

  if (!files::WriteFile(temp_file_path, data.data(), data.size())) {
    FTL_LOG(ERROR)
        << "Failed to write the temporary file for a two-phase write.";
    return false;
  }

  if (rename(temp_file_path.c_str(), destination_path.c_str()) != 0) {
    FTL_LOG(ERROR) << "Failed to move the temporary file to destination of "
                   << "the two-phase write.";
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
                     ftl::StringView repository_path) {
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
                     << "`cloud_sync clean`";
      return false;
    }

    return true;
  }

  std::string temp_dir_root = ftl::Concatenate({repository_path, "/tmp"});
  if (!WriteFileInTwoPhases(user_config.server_id, temp_dir_root,
                            server_id_path)) {
    FTL_LOG(ERROR) << "Failed to write the current server_id for compatibility "
                   << "check.";
    return false;
  }

  return true;
}

}  // namespace

LedgerRepositoryFactoryImpl::LedgerRepositoryFactoryImpl(
    configuration::Configuration config,
    ledger::Environment* environment)
    : config_(config), environment_(environment) {}

LedgerRepositoryFactoryImpl::~LedgerRepositoryFactoryImpl() {}

void LedgerRepositoryFactoryImpl::GetRepository(
    const fidl::String& repository_path,
    const fidl::String& server_id,
    fidl::InterfaceRequest<LedgerRepository> repository_request,
    const GetRepositoryCallback& callback) {
  TRACE_DURATION("ledger", "repository_factory_get_repository");
  std::string sanitized_path =
      files::SimplifyPath(std::move(repository_path.get()));
  auto it = repositories_.find(sanitized_path);
  if (it == repositories_.end()) {
    cloud_sync::UserConfig user_config = GetUserConfig(
        config_, server_id, GetStorageDirectoryName(sanitized_path));
    if (!CheckSyncConfig(user_config, sanitized_path)) {
      callback(Status::CONFIGURATION_ERROR);
      return;
    }
    auto result = repositories_.emplace(
        std::piecewise_construct, std::forward_as_tuple(sanitized_path),
        std::forward_as_tuple(sanitized_path, environment_,
                              std::move(user_config)));
    FTL_DCHECK(result.second);
    it = result.first;
  }
  it->second.BindRepository(std::move(repository_request));
  callback(Status::OK);
}

}  // namespace ledger

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/ledger_repository_factory_impl.h"

#include "apps/ledger/src/cloud_sync/public/user_config.h"
#include "apps/tracing/lib/trace/event.h"
#include "lib/ftl/files/path.h"

namespace ledger {

namespace {
ftl::StringView GetStorageDirectoryName(ftl::StringView repository_path) {
  size_t separator = repository_path.rfind('/');
  FTL_DCHECK(separator != std::string::npos);
  FTL_DCHECK(separator != repository_path.size() - 1);

  return repository_path.substr(separator + 1);
}

cloud_sync::UserConfig GetUserConfig(
    const configuration::Configuration& global_config,
    ftl::StringView user_id) {
  cloud_sync::UserConfig user_config;
  if (!global_config.use_sync) {
    user_config.use_sync = false;
    return user_config;
  }

  user_config.use_sync = true;
  user_config.server_id = global_config.sync_params.firebase_id;
  user_config.cloud_prefix = global_config.sync_params.cloud_prefix;
  user_config.user_id = user_id.ToString();
  return user_config;
}

}  // namespace

LedgerRepositoryFactoryImpl::LedgerRepositoryFactoryImpl(
    configuration::Configuration config,
    ledger::Environment* environment)
    : config_(config), environment_(environment) {}

LedgerRepositoryFactoryImpl::~LedgerRepositoryFactoryImpl() {}

void LedgerRepositoryFactoryImpl::GetRepository(
    const fidl::String& repository_path,
    fidl::InterfaceRequest<LedgerRepository> repository_request,
    const GetRepositoryCallback& callback) {
  TRACE_DURATION("ledger", "repository_factory_get_repository");
  std::string sanitized_path =
      files::SimplifyPath(std::move(repository_path.get()));
  auto it = repositories_.find(sanitized_path);
  if (it == repositories_.end()) {
    cloud_sync::UserConfig user_config =
        GetUserConfig(config_, GetStorageDirectoryName(sanitized_path));
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

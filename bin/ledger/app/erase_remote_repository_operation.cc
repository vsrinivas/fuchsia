// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/erase_remote_repository_operation.h"

#include "apps/ledger/src/app/auth_provider_impl.h"
#include "apps/ledger/src/cloud_sync/impl/paths.h"

namespace ledger {

EraseRemoteRepositoryOperation::EraseRemoteRepositoryOperation(
    ftl::RefPtr<ftl::TaskRunner> task_runner,
    ledger::NetworkService* network_service,
    std::string server_id,
    std::string api_key,
    modular::auth::TokenProviderPtr token_provider)
    : network_service_(network_service),
      server_id_(server_id),
      api_key_(std::move(api_key)) {
  token_provider.set_connection_error_handler([this] {
    FTL_LOG(ERROR) << "Lost connection to TokenProvider "
                   << "while trying to erase the repository";
    FTL_DCHECK(on_done_);
    on_done_(false);
  });
  auth_provider_ = std::make_unique<AuthProviderImpl>(
      task_runner, api_key_, std::move(token_provider));
}

EraseRemoteRepositoryOperation::EraseRemoteRepositoryOperation(
    EraseRemoteRepositoryOperation&& other) = default;

EraseRemoteRepositoryOperation& EraseRemoteRepositoryOperation::operator=(
    EraseRemoteRepositoryOperation&& other) = default;

void EraseRemoteRepositoryOperation::Start(std::function<void(bool)> on_done) {
  FTL_DCHECK(!on_done_);
  on_done_ = std::move(on_done);
  FTL_DCHECK(on_done_);

  auth_provider_->GetFirebaseUserId([this](cloud_sync::AuthStatus auth_status,
                                           std::string user_id) {
    if (auth_status != cloud_sync::AuthStatus::OK) {
      FTL_LOG(ERROR)
          << "Failed to retrieve Firebase user id from token provider.";
      on_done_(false);
      return;
    }
    user_id_ = std::move(user_id);
    auth_provider_->GetFirebaseToken([this](cloud_sync::AuthStatus auth_status,
                                            std::string auth_token) {
      if (auth_status != cloud_sync::AuthStatus::OK) {
        FTL_LOG(ERROR)
            << "Failed to retrieve the auth token to clean the remote state.";
        on_done_(false);
        return;
      }

      auth_token_ = std::move(auth_token);
      EraseRemote();
    });
  });
}

void EraseRemoteRepositoryOperation::EraseRemote() {
  if (user_id_.empty()) {
    FTL_LOG(ERROR) << "Missing credentials from the token provider, "
                   << "will not erase the remote state. (running as guest?)";
    on_done_(true);
    return;
  }

  firebase_ = std::make_unique<firebase::FirebaseImpl>(
      network_service_, server_id_,
      cloud_sync::GetFirebasePathForUser(user_id_));
  std::vector<std::string> query_params;
  if (!auth_token_.empty()) {
    query_params = {"auth=" + auth_token_};
  }
  firebase_->Delete(
      "", std::move(query_params), [this](firebase::Status status) {
        if (status != firebase::Status::OK) {
          FTL_LOG(ERROR) << "Failed to erase the remote state: " << status;
          on_done_(false);
          return;
        }
        FTL_LOG(INFO) << "Erased remote data at " << firebase_->api_url();
        on_done_(true);
      });
}

}  // namespace ledger

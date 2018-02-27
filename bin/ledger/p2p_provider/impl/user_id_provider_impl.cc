// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/p2p_provider/impl/user_id_provider_impl.h"

#include "garnet/lib/backoff/exponential_backoff.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/logging.h"
#include "peridot/lib/firebase_auth/firebase_auth_impl.h"
#include "peridot/lib/ledger_client/constants.h"

namespace p2p_provider {

UserIdProviderImpl::UserIdProviderImpl(
    ledger::Environment* environment,
    std::string user_directory,
    modular::auth::TokenProviderPtr token_provider_ptr)
    : user_directory_(std::move(user_directory)) {
  firebase_auth_ = std::make_unique<firebase_auth::FirebaseAuthImpl>(
      environment->main_runner(), modular::kFirebaseApiKey,
      std::move(token_provider_ptr),
      std::make_unique<backoff::ExponentialBackoff>());
}

void UserIdProviderImpl::GetUserId(
    std::function<void(Status, std::string)> callback) {
  FXL_DCHECK(callback);
  std::string stored_id;
  if (LoadUserIdFromFile(&stored_id)) {
    callback(Status::OK, stored_id);
    return;
  }

  firebase_auth_->GetFirebaseUserId([this, callback = std::move(callback)](
                                        firebase_auth::AuthStatus status,
                                        std::string user_id) {
    if (status != firebase_auth::AuthStatus::OK) {
      FXL_LOG(ERROR) << "Firebase auth returned an error.";
      callback(Status::ERROR, "");
      return;
    }
    if (!UpdateUserId(user_id)) {
      FXL_LOG(WARNING)
          << "Unable to persist the user id for caching. Continuing anyway...";
      // We have the user id, we can continue anyway.
    }
    callback(Status::OK, user_id);
  });
}

std::string UserIdProviderImpl::GetUserIdPath() {
  return user_directory_ + "/p2p_user_id";
}

bool UserIdProviderImpl::LoadUserIdFromFile(std::string* id) {
  std::string id_path = GetUserIdPath();
  if (!files::IsFile(id_path)) {
    return false;
  }

  if (!files::ReadFileToString(id_path, id)) {
    FXL_LOG(ERROR) << "Unable to read the id file at: " << id_path;
    return false;
  }
  return true;
}

bool UserIdProviderImpl::WriteUserIdToFile(std::string id) {
  std::string id_path = GetUserIdPath();
  if (!files::WriteFile(id_path, id.data(), id.size())) {
    FXL_LOG(ERROR) << "Failed to persist the id at " << id_path;
    return false;
  }
  return true;
}

bool UserIdProviderImpl::UpdateUserId(std::string user_id) {
  std::string stored_id;
  if (LoadUserIdFromFile(&stored_id) && stored_id == user_id) {
    return true;
  }
  return WriteUserIdToFile(user_id);
}

}  // namespace p2p_provider

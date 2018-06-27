// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/p2p_provider/impl/user_id_provider_impl.h"

#include <lib/fit/function.h>

#include "lib/backoff/exponential_backoff.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/logging.h"
#include "peridot/lib/firebase_auth/firebase_auth_impl.h"
#include "peridot/lib/ledger_client/constants.h"

namespace p2p_provider {

constexpr fxl::StringView user_id_filename = "p2p_user_id";

UserIdProviderImpl::UserIdProviderImpl(
    ledger::Environment* environment,
    fuchsia::sys::StartupContext* startup_context,
    ledger::DetachedPath user_directory,
    fuchsia::modular::auth::TokenProviderPtr token_provider_ptr,
    std::string cobalt_client_name)
    : user_id_path_(user_directory.SubPath(user_id_filename)),
      firebase_auth_(std::make_unique<firebase_auth::FirebaseAuthImpl>(
          firebase_auth::FirebaseAuthImpl::Config{modular::kFirebaseApiKey,
                                                  cobalt_client_name},
          environment->async(), std::move(token_provider_ptr),
          startup_context)) {}

void UserIdProviderImpl::GetUserId(
    fit::function<void(Status, std::string)> callback) {
  FXL_DCHECK(callback);
  std::string stored_id;
  if (LoadUserIdFromFile(&stored_id)) {
    callback(Status::OK, stored_id);
    return;
  }

  firebase_auth_->GetFirebaseUserId(
      [this, callback = std::move(callback)](firebase_auth::AuthStatus status,
                                             std::string user_id) {
        if (status != firebase_auth::AuthStatus::OK) {
          FXL_LOG(ERROR) << "Firebase auth returned an error.";
          callback(Status::ERROR, "");
          return;
        }
        if (!UpdateUserId(user_id)) {
          FXL_LOG(WARNING) << "Unable to persist the user id for caching. "
                              "Continuing anyway...";
          // We have the user id, we can continue anyway.
        }
        callback(Status::OK, user_id);
      });
}

bool UserIdProviderImpl::LoadUserIdFromFile(std::string* id) {
  if (!files::IsFileAt(user_id_path_.root_fd(), user_id_path_.path())) {
    return false;
  }

  if (!files::ReadFileToStringAt(user_id_path_.root_fd(), user_id_path_.path(),
                                 id)) {
    FXL_LOG(ERROR) << "Unable to read the id file at: " << user_id_path_.path();
    return false;
  }
  return true;
}

bool UserIdProviderImpl::WriteUserIdToFile(std::string id) {
  if (!files::WriteFileAt(user_id_path_.root_fd(), user_id_path_.path(),
                          id.data(), id.size())) {
    FXL_LOG(ERROR) << "Failed to persist the id at " << user_id_path_.path();
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

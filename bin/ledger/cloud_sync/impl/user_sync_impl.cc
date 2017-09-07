// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/impl/user_sync_impl.h"

#include <utility>

#include "apps/ledger/src/cloud_provider/impl/paths.h"
#include "apps/ledger/src/cloud_sync/impl/ledger_sync_impl.h"
#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/firebase/firebase_impl.h"
#include "apps/ledger/src/glue/crypto/rand.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/concatenate.h"

namespace cloud_sync {

namespace {
constexpr size_t kFingerprintSize = 16;

}  // namespace

UserSyncImpl::UserSyncImpl(ledger::Environment* environment,
                           UserConfig user_config,
                           std::unique_ptr<backoff::Backoff> backoff,
                           SyncStateWatcher* watcher,
                           ftl::Closure on_version_mismatch)
    : environment_(environment),
      user_config_(std::move(user_config)),
      backoff_(std::move(backoff)),
      on_version_mismatch_(std::move(on_version_mismatch)),
      aggregator_(watcher),
      weak_ptr_factory_(this) {
  FTL_DCHECK(on_version_mismatch_);
}

UserSyncImpl::~UserSyncImpl() {
  FTL_DCHECK(active_ledger_syncs_.empty());
}

std::unique_ptr<LedgerSync> UserSyncImpl::CreateLedgerSync(
    ftl::StringView app_id) {
  FTL_DCHECK(started_);

  auto result = std::make_unique<LedgerSyncImpl>(
      environment_, &user_config_, app_id, aggregator_.GetNewStateWatcher());
  result->set_on_delete([ this, ledger_sync = result.get() ]() {
    active_ledger_syncs_.erase(ledger_sync);
  });
  active_ledger_syncs_.insert(result.get());
  if (upload_enabled_) {
    result->EnableUpload();
  }
  return result;
}

std::string UserSyncImpl::GetFingerprintPath() {
  return ftl::Concatenate({user_config_.user_directory, "/fingerprint"});
}

void UserSyncImpl::Start() {
  FTL_DCHECK(!started_);

  CheckCloudNotErased();

  started_ = true;
}

void UserSyncImpl::CheckCloudNotErased() {
  FTL_DCHECK(user_config_.auth_provider);

  std::string fingerprint_path = GetFingerprintPath();
  if (!files::IsFile(fingerprint_path)) {
    CreateFingerprint();
    return;
  }

  if (!files::ReadFileToString(fingerprint_path, &fingerprint_)) {
    FTL_LOG(ERROR) << "Unable to read the fingerprint file at: "
                   << fingerprint_path << ", sync upload will not work.";
    return;
  }

  auto request =
      user_config_.auth_provider->GetFirebaseToken(
          [this](auth_provider::AuthStatus auth_status,
                 std::string auth_token) {
            if (auth_status != auth_provider::AuthStatus::OK) {
              FTL_LOG(ERROR)
                  << "Failed to retrieve the auth token for version check, "
                  << "sync upload will not work.";
              return;
            }

            user_config_.cloud_device_set->CheckFingerprint(
                std::move(auth_token), fingerprint_,
                [this](cloud_provider_firebase::CloudDeviceSet::Status status) {
                  HandleCheckCloudResult(status);
                });
          });
  auth_token_requests_.emplace(request);
}

void UserSyncImpl::CreateFingerprint() {
  // Generate the fingerprint.
  char fingerprint_array[kFingerprintSize];
  glue::RandBytes(fingerprint_array, kFingerprintSize);
  fingerprint_ =
      convert::ToHex(ftl::StringView(fingerprint_array, kFingerprintSize));

  auto request = user_config_.auth_provider->GetFirebaseToken(
      [this](auth_provider::AuthStatus auth_status, std::string auth_token) {
        if (auth_status != auth_provider::AuthStatus::OK) {
          FTL_LOG(ERROR)
              << "Failed to retrieve the auth token for fingerprint check, "
              << "sync upload will not work.";
          return;
        }

        user_config_.cloud_device_set->SetFingerprint(
            std::move(auth_token), fingerprint_,
            [this](cloud_provider_firebase::CloudDeviceSet::Status status) {
              if (status ==
                  cloud_provider_firebase::CloudDeviceSet::Status::OK) {
                // Persist the new fingerprint.
                FTL_DCHECK(!fingerprint_.empty());
                if (!files::WriteFile(GetFingerprintPath(), fingerprint_.data(),
                                      fingerprint_.size())) {
                  FTL_LOG(ERROR) << "Failed to persist the fingerprint, "
                                 << "sync upload will not work.";
                  return;
                }
              }
              HandleCheckCloudResult(status);
            });
      });
  auth_token_requests_.emplace(request);
}

void UserSyncImpl::HandleCheckCloudResult(
    cloud_provider_firebase::CloudDeviceSet::Status status) {
  // HACK: in order to test this codepath in an apptest, we expose a hook
  // that forces the cloud erased recovery closure to run.
  if (environment_->TriggerCloudErasedForTesting()) {
    on_version_mismatch_();
    return;
  }

  switch (status) {
    case cloud_provider_firebase::CloudDeviceSet::Status::OK:
      backoff_->Reset();
      SetCloudErasedWatcher();
      EnableUpload();
      return;
    case cloud_provider_firebase::CloudDeviceSet::Status::NETWORK_ERROR:
      // Retry after some backoff time.
      environment_->main_runner()->PostDelayedTask(
          [weak_this = weak_ptr_factory_.GetWeakPtr()] {
            if (weak_this) {
              weak_this->CheckCloudNotErased();
            }
          },
          backoff_->GetNext());
      return;
    case cloud_provider_firebase::CloudDeviceSet::Status::ERASED:
      // |this| can be deleted within on_version_mismatch_() - don't
      // access member variables afterwards.
      on_version_mismatch_();
      return;
  }
}

void UserSyncImpl::SetCloudErasedWatcher() {
  auto request = user_config_.auth_provider->GetFirebaseToken(
      [this](auth_provider::AuthStatus auth_status, std::string auth_token) {
        if (auth_status != auth_provider::AuthStatus::OK) {
          FTL_LOG(ERROR) << "Failed to retrieve the auth token for fingerprint "
                         << "watcher.";
          return;
        }

        user_config_.cloud_device_set->WatchFingerprint(
            std::move(auth_token), fingerprint_,
            [this](cloud_provider_firebase::CloudDeviceSet::Status status) {
              HandleWatcherResult(status);
            });
      });
  auth_token_requests_.emplace(request);
}

void UserSyncImpl::HandleWatcherResult(
    cloud_provider_firebase::CloudDeviceSet::Status status) {
  switch (status) {
    case cloud_provider_firebase::CloudDeviceSet::Status::OK:
      backoff_->Reset();
      return;
    case cloud_provider_firebase::CloudDeviceSet::Status::NETWORK_ERROR:
      environment_->main_runner()->PostDelayedTask(
          [weak_this = weak_ptr_factory_.GetWeakPtr()] {
            if (weak_this) {
              weak_this->SetCloudErasedWatcher();
            }
          },
          backoff_->GetNext());
      return;
    case cloud_provider_firebase::CloudDeviceSet::Status::ERASED:
      // |this| can be deleted within on_version_mismatch_() - don't
      // access member variables afterwards.
      on_version_mismatch_();
      return;
  }
}

void UserSyncImpl::EnableUpload() {
  upload_enabled_ = true;
  for (auto ledger_sync : active_ledger_syncs_) {
    ledger_sync->EnableUpload();
  }
}

}  // namespace cloud_sync

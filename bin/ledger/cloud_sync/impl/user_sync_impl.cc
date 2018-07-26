// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/impl/user_sync_impl.h"

#include <utility>

#include <lib/fit/function.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/concatenate.h>
#include <zircon/syscalls.h>

#include "peridot/bin/ledger/cloud_sync/impl/ledger_sync_impl.h"
#include "peridot/lib/convert/convert.h"

namespace cloud_sync {

namespace {
constexpr size_t kFingerprintSize = 16;

}  // namespace

UserSyncImpl::UserSyncImpl(ledger::Environment* environment,
                           UserConfig user_config,
                           std::unique_ptr<backoff::Backoff> backoff,
                           fit::closure on_version_mismatch)
    : environment_(environment),
      user_config_(std::move(user_config)),
      backoff_(std::move(backoff)),
      on_version_mismatch_(std::move(on_version_mismatch)),
      watcher_binding_(this),
      task_runner_(environment_->dispatcher()) {
  FXL_DCHECK(on_version_mismatch_);
}

UserSyncImpl::~UserSyncImpl() { FXL_DCHECK(active_ledger_syncs_.empty()); }

void UserSyncImpl::SetSyncWatcher(SyncStateWatcher* watcher) {
  aggregator_.SetBaseWatcher(watcher);
}

std::unique_ptr<LedgerSync> UserSyncImpl::CreateLedgerSync(
    fxl::StringView app_id, encryption::EncryptionService* encryption_service) {
  FXL_DCHECK(started_);

  auto result = std::make_unique<LedgerSyncImpl>(
      environment_, &user_config_, encryption_service, app_id,
      aggregator_.GetNewStateWatcher());
  result->set_on_delete([this, ledger_sync = result.get()]() {
    active_ledger_syncs_.erase(ledger_sync);
  });
  active_ledger_syncs_.insert(result.get());
  if (upload_enabled_) {
    result->EnableUpload();
  }
  return result;
}

ledger::DetachedPath UserSyncImpl::GetFingerprintPath() {
  return user_config_.user_directory.SubPath("fingerprint");
}

void UserSyncImpl::OnCloudErased() {
  // |this| can be deleted within on_version_mismatch_() - don't
  // access member variables afterwards.
  on_version_mismatch_();
}

void UserSyncImpl::OnNetworkError() {
  task_runner_.PostDelayedTask([this] { SetCloudErasedWatcher(); },
                               backoff_->GetNext());
}

void UserSyncImpl::Start() {
  FXL_DCHECK(!started_);

  user_config_.cloud_provider->GetDeviceSet(
      device_set_.NewRequest(), [this](auto status) {
        if (status != cloud_provider::Status::OK) {
          FXL_LOG(ERROR) << "Failed to retrieve the device map: "
                         << fidl::ToUnderlying(status)
                         << ", sync upload will not work.";
          return;
        }
        CheckCloudNotErased();
      });

  started_ = true;
}

void UserSyncImpl::CheckCloudNotErased() {
  FXL_DCHECK(device_set_);

  ledger::DetachedPath fingerprint_path = GetFingerprintPath();
  if (!files::IsFileAt(fingerprint_path.root_fd(), fingerprint_path.path())) {
    CreateFingerprint();
    return;
  }

  if (!files::ReadFileToStringAt(fingerprint_path.root_fd(),
                                 fingerprint_path.path(), &fingerprint_)) {
    FXL_LOG(ERROR) << "Unable to read the fingerprint file at: "
                   << fingerprint_path.path() << ", sync upload will not work.";
    return;
  }

  device_set_->CheckFingerprint(
      convert::ToArray(fingerprint_),
      [this](cloud_provider::Status status) { HandleDeviceSetResult(status); });
}

void UserSyncImpl::CreateFingerprint() {
  // Generate the fingerprint.
  char fingerprint_array[kFingerprintSize];
  zx_cprng_draw(fingerprint_array, kFingerprintSize);
  fingerprint_ =
      convert::ToHex(fxl::StringView(fingerprint_array, kFingerprintSize));

  device_set_->SetFingerprint(
      convert::ToArray(fingerprint_), [this](cloud_provider::Status status) {
        if (status == cloud_provider::Status::OK) {
          // Persist the new fingerprint.
          FXL_DCHECK(!fingerprint_.empty());
          ledger::DetachedPath fingerprint_path = GetFingerprintPath();
          if (!files::WriteFileAt(fingerprint_path.root_fd(),
                                  fingerprint_path.path(), fingerprint_.data(),
                                  fingerprint_.size())) {
            FXL_LOG(ERROR) << "Failed to persist the fingerprint at: "
                           << fingerprint_path.path()
                           << ", sync upload will not work.";
            return;
          }
        }
        HandleDeviceSetResult(status);
      });
}

void UserSyncImpl::HandleDeviceSetResult(cloud_provider::Status status) {
  switch (status) {
    case cloud_provider::Status::OK:
      backoff_->Reset();
      SetCloudErasedWatcher();
      EnableUpload();
      return;
    case cloud_provider::Status::NETWORK_ERROR:
      // Retry after some backoff time.
      task_runner_.PostDelayedTask([this] { CheckCloudNotErased(); },
                                   backoff_->GetNext());
      return;
    case cloud_provider::Status::NOT_FOUND:
      // |this| can be deleted within on_version_mismatch_() - don't
      // access member variables afterwards.
      on_version_mismatch_();
      return;
    default:
      FXL_LOG(ERROR) << "Unexpected status returned from device set: "
                     << fidl::ToUnderlying(status)
                     << ", sync upload will not work.";
      return;
  }
}

void UserSyncImpl::SetCloudErasedWatcher() {
  cloud_provider::DeviceSetWatcherPtr watcher;
  if (watcher_binding_.is_bound()) {
    watcher_binding_.Unbind();
  }
  watcher_binding_.Bind(watcher.NewRequest());
  device_set_->SetWatcher(convert::ToArray(fingerprint_), std::move(watcher),
                          [this](cloud_provider::Status status) {
                            if (status == cloud_provider::Status::OK) {
                              backoff_->Reset();
                            }
                            // Don't handle errors - in case of error, the
                            // corresponding call is made on the watcher
                            // itself and handled there (OnCloudErased(),
                            // OnNetworkError()).
                          });
}

void UserSyncImpl::EnableUpload() {
  upload_enabled_ = true;
  for (auto ledger_sync : active_ledger_syncs_) {
    ledger_sync->EnableUpload();
  }
}

}  // namespace cloud_sync

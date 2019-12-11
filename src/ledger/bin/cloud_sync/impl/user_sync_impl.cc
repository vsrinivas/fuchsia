// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/user_sync_impl.h"

#include <lib/fit/function.h>
#include <zircon/syscalls.h>

#include <utility>

#include "src/ledger/bin/clocks/public/device_fingerprint_manager.h"
#include "src/ledger/bin/cloud_sync/impl/ledger_sync_impl.h"
#include "src/ledger/bin/public/status.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/ledger/lib/coroutine/coroutine_manager.h"
#include "src/ledger/lib/logging/logging.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace cloud_sync {

UserSyncImpl::UserSyncImpl(ledger::Environment* environment, UserConfig user_config,
                           std::unique_ptr<backoff::Backoff> backoff,
                           fit::closure on_version_mismatch,
                           clocks::DeviceFingerprintManager* fingerprint_manager)
    : environment_(environment),
      user_config_(std::move(user_config)),
      backoff_(std::move(backoff)),
      on_version_mismatch_(std::move(on_version_mismatch)),
      watcher_binding_(this),
      fingerprint_manager_(fingerprint_manager),
      coroutine_manager_(environment_->coroutine_service()),
      task_runner_(environment_->dispatcher()) {
  LEDGER_DCHECK(on_version_mismatch_);
}

UserSyncImpl::~UserSyncImpl() { LEDGER_DCHECK(active_ledger_syncs_.empty()); }

void UserSyncImpl::SetSyncWatcher(SyncStateWatcher* watcher) {
  aggregator_.SetBaseWatcher(watcher);
}

std::unique_ptr<LedgerSync> UserSyncImpl::CreateLedgerSync(
    absl::string_view app_id, encryption::EncryptionService* encryption_service) {
  LEDGER_DCHECK(started_);

  auto result = std::make_unique<LedgerSyncImpl>(environment_, &user_config_, encryption_service,
                                                 app_id, aggregator_.GetNewStateWatcher());
  result->set_on_delete(
      [this, ledger_sync = result.get()]() { active_ledger_syncs_.erase(ledger_sync); });
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

void UserSyncImpl::OnError(cloud_provider::Status status) {
  task_runner_.PostDelayedTask([this] { SetCloudErasedWatcher(); }, backoff_->GetNext());
}

void UserSyncImpl::Start() {
  LEDGER_DCHECK(!started_);
  if (!user_config_.cloud_provider) {
    // TODO(ppi): handle recovery from cloud provider disconnection, LE-567.
    LEDGER_LOG(WARNING) << "Cloud provider is disconnected, will not verify "
                        << "the cloud fingerprint";
    return;
  }

  user_config_.cloud_provider->GetDeviceSet(
      device_set_.NewRequest(),
      task_runner_.MakeScoped([this](fuchsia::ledger::cloud::Status status) {
        if (status != cloud_provider::Status::OK) {
          LEDGER_LOG(ERROR) << "Failed to retrieve the device map: " << fidl::ToUnderlying(status)
                            << ", sync upload will not work.";
          return;
        }
        CheckCloudNotErased();
      }));
  started_ = true;
}

void UserSyncImpl::CheckCloudNotErased() {
  if (!device_set_) {
    // TODO(ppi): handle recovery from cloud provider disconnection, LE-567.
    LEDGER_LOG(WARNING) << "Cloud provider is disconnected, will not verify "
                        << "the cloud fingerprint";
    return;
  }

  coroutine_manager_.StartCoroutine([this](coroutine::CoroutineHandler* handler) {
    cloud_provider::Status status;
    clocks::DeviceFingerprintManager::CloudUploadStatus upload_status;
    if (fingerprint_manager_->GetDeviceFingerprint(handler, &fingerprint_, &upload_status) !=
        ledger::Status::OK) {
      return;
    }
    switch (upload_status) {
      case clocks::DeviceFingerprintManager::CloudUploadStatus::NOT_UPLOADED: {
        if (coroutine::SyncCall(
                handler,
                [this](fit::function<void(cloud_provider::Status)> callback) {
                  device_set_->SetFingerprint(convert::ToArray(fingerprint_), std::move(callback));
                },
                &status) == coroutine::ContinuationStatus::INTERRUPTED) {
          return;
        }
        if (status == cloud_provider::Status::OK) {
          ledger::Status s = fingerprint_manager_->SetDeviceFingerprintSynced(handler);
          if (s != ledger::Status::OK) {
            return;
          }
        }
      } break;
      case clocks::DeviceFingerprintManager::CloudUploadStatus::UPLOADED: {
        if (coroutine::SyncCall(
                handler,
                [this](fit::function<void(cloud_provider::Status)> callback) {
                  device_set_->CheckFingerprint(convert::ToArray(fingerprint_),
                                                std::move(callback));
                },
                &status) == coroutine::ContinuationStatus::INTERRUPTED) {
          return;
        }
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
      task_runner_.PostDelayedTask([this] { CheckCloudNotErased(); }, backoff_->GetNext());
      return;
    case cloud_provider::Status::NOT_FOUND:
      // |this| can be deleted within on_version_mismatch_() - don't
      // access member variables afterwards. Also, make sure we are not executing within a
      // coroutine.
      task_runner_.PostTask(std::move(on_version_mismatch_));
      return;
    default:
      LEDGER_LOG(ERROR) << "Unexpected status returned from device set: "
                        << fidl::ToUnderlying(status) << ", sync upload will not work.";
      return;
  }
}

void UserSyncImpl::SetCloudErasedWatcher() {
  if (!device_set_) {
    // TODO(ppi): handle recovery from cloud provider disconnection, LE-567.
    LEDGER_LOG(WARNING) << "Cloud provider is disconnected, will not verify "
                        << "the cloud fingerprint";
    return;
  }

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
                            // OnError()).
                          });
}

void UserSyncImpl::EnableUpload() {
  upload_enabled_ = true;
  for (auto ledger_sync : active_ledger_syncs_) {
    ledger_sync->EnableUpload();
  }
}

}  // namespace cloud_sync

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_USER_SYNC_IMPL_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_USER_SYNC_IMPL_H_

#include "apps/ledger/src/cloud_sync/public/user_sync.h"

#include <memory>
#include <unordered_set>

#include "apps/ledger/src/backoff/backoff.h"
#include "apps/ledger/src/callback/cancellable.h"
#include "apps/ledger/src/cloud_sync/impl/aggregator.h"
#include "apps/ledger/src/cloud_sync/impl/ledger_sync_impl.h"
#include "apps/ledger/src/device_set/cloud_device_set_impl.h"
#include "apps/ledger/src/environment/environment.h"
#include "apps/ledger/src/firebase/firebase.h"
#include "lib/ftl/memory/weak_ptr.h"

namespace cloud_sync {

class UserSyncImpl : public UserSync {
 public:
  // Parameters:
  //   |on_version_mismatch| is called when the local state is detected to be
  //     incompatible with the state in the cloud and has to be erased.
  UserSyncImpl(ledger::Environment* environment,
               UserConfig user_config,
               std::unique_ptr<backoff::Backoff> backoff,
               SyncStateWatcher* watcher,
               ftl::Closure on_version_mismatch);
  ~UserSyncImpl() override;

  // UserSync:
  std::unique_ptr<LedgerSync> CreateLedgerSync(ftl::StringView app_id) override;

  // Starts UserSyncImpl. This method must be called before any other method.
  void Start();

  // Returns the path where the device fingerprint is stored.
  std::string GetFingerprintPath();

 private:
  // Checks that the cloud was not erased since the last sync using the device
  // fingerprint.
  void CheckCloudNotErased();
  void CreateFingerprint();
  void HandleCheckCloudResult(
      cloud_provider_firebase::CloudDeviceSet::Status status);

  // Sets a watcher to detect that the cloud is cleared while sync is running.
  void SetCloudErasedWatcher();
  void HandleWatcherResult(
      cloud_provider_firebase::CloudDeviceSet::Status status);

  // Enables sync upload.
  void EnableUpload();

  ledger::Environment* environment_;
  const UserConfig user_config_;
  std::unique_ptr<backoff::Backoff> backoff_;
  ftl::Closure on_version_mismatch_;

  // UserSyncImpl must be started before it can be used.
  bool started_ = false;
  // Whether uploads should be enabled. It is false until the cloud version has
  // been checked.
  bool upload_enabled_ = false;
  // Fingerprint of the device in the cloud device list.
  std::string fingerprint_;
  std::unordered_set<LedgerSyncImpl*> active_ledger_syncs_;

  // Pending auth token requests to be cancelled when this class goes away.
  callback::CancellableContainer auth_token_requests_;

  // Aggregates the synchronization state of multiple ledgers into one
  // notification stream.
  Aggregator aggregator_;

  // This must be the last member of this class.
  ftl::WeakPtrFactory<UserSyncImpl> weak_ptr_factory_;
};

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_USER_SYNC_IMPL_H_

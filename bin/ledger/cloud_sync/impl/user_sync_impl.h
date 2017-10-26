// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_USER_SYNC_IMPL_H_
#define PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_USER_SYNC_IMPL_H_

#include "peridot/bin/ledger/cloud_sync/public/user_sync.h"

#include <memory>
#include <unordered_set>

#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "peridot/bin/ledger/backoff/backoff.h"
#include "peridot/bin/ledger/callback/scoped_task_runner.h"
#include "peridot/bin/ledger/cloud_sync/impl/aggregator.h"
#include "peridot/bin/ledger/cloud_sync/impl/ledger_sync_impl.h"
#include "peridot/bin/ledger/environment/environment.h"

namespace cloud_sync {

class UserSyncImpl : public UserSync, cloud_provider::DeviceSetWatcher {
 public:
  // Parameters:
  //   |on_version_mismatch| is called when the local state is detected to be
  //     incompatible with the state in the cloud and has to be erased.
  UserSyncImpl(ledger::Environment* environment,
               UserConfig user_config,
               std::unique_ptr<backoff::Backoff> backoff,
               SyncStateWatcher* watcher,
               fxl::Closure on_version_mismatch);
  ~UserSyncImpl() override;

  // UserSync:
  std::unique_ptr<LedgerSync> CreateLedgerSync(fxl::StringView app_id) override;

  // Starts UserSyncImpl. This method must be called before any other method.
  void Start();

  // Returns the path where the device fingerprint is stored.
  std::string GetFingerprintPath();

 private:
  // cloud_provider::DeviceSetWatcher:
  void OnCloudErased() override;

  void OnNetworkError() override;

  // Checks that the cloud was not erased since the last sync using the device
  // fingerprint.
  void CheckCloudNotErased();
  void CreateFingerprint();
  void HandleDeviceSetResult(cloud_provider::Status status);

  // Sets a watcher to detect that the cloud is cleared while sync is running.
  void SetCloudErasedWatcher();

  // Enables sync upload.
  void EnableUpload();

  ledger::Environment* environment_;
  const UserConfig user_config_;
  std::unique_ptr<backoff::Backoff> backoff_;
  fxl::Closure on_version_mismatch_;

  // UserSyncImpl must be started before it can be used.
  bool started_ = false;
  // Whether uploads should be enabled. It is false until the cloud version has
  // been checked.
  bool upload_enabled_ = false;
  cloud_provider::DeviceSetPtr device_set_;
  fidl::Binding<cloud_provider::DeviceSetWatcher> watcher_binding_;
  // Fingerprint of the device in the cloud device list.
  std::string fingerprint_;
  std::unordered_set<LedgerSyncImpl*> active_ledger_syncs_;

  // Aggregates the synchronization state of multiple ledgers into one
  // notification stream.
  Aggregator aggregator_;

  // This must be the last member of this class.
  callback::ScopedTaskRunner task_runner_;
};

}  // namespace cloud_sync

#endif  // PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_USER_SYNC_IMPL_H_

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_USER_SYNC_IMPL_H_
#define SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_USER_SYNC_IMPL_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>

#include <memory>
#include <set>

#include "src/ledger/bin/clocks/public/device_fingerprint_manager.h"
#include "src/ledger/bin/cloud_sync/impl/aggregator.h"
#include "src/ledger/bin/cloud_sync/impl/ledger_sync_impl.h"
#include "src/ledger/bin/cloud_sync/public/user_sync.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/lib/backoff/backoff.h"
#include "src/ledger/lib/coroutine/coroutine_manager.h"
#include "src/lib/callback/scoped_task_runner.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace cloud_sync {
class UserSyncImpl : public UserSync, cloud_provider::DeviceSetWatcher {
 public:
  // Parameters:
  //   |on_version_mismatch| is called when the local state is detected to be
  //     incompatible with the state in the cloud and has to be erased.
  UserSyncImpl(ledger::Environment* environment, UserConfig user_config,
               std::unique_ptr<ledger::Backoff> backoff, fit::closure on_version_mismatch,
               clocks::DeviceFingerprintManager* fingerprint_manager);
  ~UserSyncImpl() override;

  // UserSync:
  void SetSyncWatcher(SyncStateWatcher* watcher) override;
  void Start() override;
  std::unique_ptr<LedgerSync> CreateLedgerSync(
      absl::string_view app_id, encryption::EncryptionService* encryption_service) override;

  // Returns the path where the device fingerprint is stored.
  ledger::DetachedPath GetFingerprintPath();

 private:
  // cloud_provider::DeviceSetWatcher:
  void OnCloudErased() override;

  void OnError(cloud_provider::Status status) override;

  // Checks that the cloud was not erased since the last sync using the device
  // fingerprint.
  void CheckCloudNotErased();
  void HandleDeviceSetResult(cloud_provider::Status status);

  // Sets a watcher to detect that the cloud is cleared while sync is running.
  void SetCloudErasedWatcher();

  // Enables sync upload.
  void EnableUpload();

  ledger::Environment* environment_;
  const UserConfig user_config_;
  std::unique_ptr<ledger::Backoff> backoff_;
  fit::closure on_version_mismatch_;

  // UserSyncImpl must be started before it can be used.
  bool started_ = false;
  // Whether uploads should be enabled. It is false until the cloud version has
  // been checked.
  bool upload_enabled_ = false;
  cloud_provider::DeviceSetPtr device_set_;
  fidl::Binding<cloud_provider::DeviceSetWatcher> watcher_binding_;
  clocks::DeviceFingerprint fingerprint_;
  clocks::DeviceFingerprintManager* fingerprint_manager_;
  std::set<LedgerSyncImpl*> active_ledger_syncs_;

  // Aggregates the synchronization state of multiple ledgers into one
  // notification stream.
  Aggregator aggregator_;

  coroutine::CoroutineManager coroutine_manager_;
  // This must be the last member of this class.
  callback::ScopedTaskRunner task_runner_;
};

}  // namespace cloud_sync

#endif  // SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_USER_SYNC_IMPL_H_

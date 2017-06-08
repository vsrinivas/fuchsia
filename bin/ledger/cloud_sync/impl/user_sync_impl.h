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
#include "apps/ledger/src/cloud_sync/impl/ledger_sync_impl.h"
#include "apps/ledger/src/cloud_sync/impl/local_version_checker.h"
#include "apps/ledger/src/environment/environment.h"
#include "apps/ledger/src/firebase/firebase.h"
#include "lib/ftl/memory/weak_ptr.h"

namespace cloud_sync {

class UserSyncImpl : public UserSync {
 public:
  UserSyncImpl(ledger::Environment* environment,
               UserConfig user_config,
               std::unique_ptr<backoff::Backoff> backoff);
  ~UserSyncImpl() override;

  // Starts UserSyncImpl. This method must be called before any other method.
  void Start();

 private:
  // Returns the path where the local version is stored.
  std::string GetLocalVersionPath();
  // Check that the version on the cloud is compatible with the local version on
  // the device.
  void CheckCloudVersion();
  // Enable sync upload.
  void EnableUpload();

  // UserSync:
  std::unique_ptr<LedgerSync> CreateLedgerSync(ftl::StringView app_id) override;

  ledger::Environment* environment_;
  const UserConfig user_config_;
  std::unique_ptr<backoff::Backoff> backoff_;

  // Utility to check that the local version is compatible with the cloud
  // version.
  LocalVersionChecker local_version_checker_;

  // UserSyncImpl must be started before it can be used.
  bool started_ = false;
  // Whether uploads should be enabled. It is false until the cloud version has
  // been checked.
  bool upload_enabled_ = false;
  std::unique_ptr<firebase::Firebase> user_firebase_;
  std::unordered_set<LedgerSyncImpl*> active_ledger_syncs_;

  // Pending auth token requests to be cancelled when this class goes away.
  callback::CancellableContainer auth_token_requests_;

  // This must be the last member of this class.
  ftl::WeakPtrFactory<UserSyncImpl> weak_ptr_factory_;
};

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_USER_SYNC_IMPL_H_

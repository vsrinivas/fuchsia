// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_LEDGER_SYNC_IMPL_H_
#define PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_LEDGER_SYNC_IMPL_H_

#include <memory>
#include <set>

#include <lib/fit/function.h>

#include "peridot/bin/ledger/cloud_sync/impl/aggregator.h"
#include "peridot/bin/ledger/cloud_sync/impl/page_sync_impl.h"
#include "peridot/bin/ledger/cloud_sync/public/ledger_sync.h"
#include "peridot/bin/ledger/cloud_sync/public/sync_state_watcher.h"
#include "peridot/bin/ledger/cloud_sync/public/user_config.h"
#include "peridot/bin/ledger/encryption/public/encryption_service.h"
#include "peridot/bin/ledger/environment/environment.h"

namespace cloud_sync {

class LedgerSyncImpl : public LedgerSync {
 public:
  LedgerSyncImpl(ledger::Environment* environment,
                 const UserConfig* user_config,
                 encryption::EncryptionService* encryption_service,
                 fxl::StringView app_id,
                 std::unique_ptr<SyncStateWatcher> watcher);
  ~LedgerSyncImpl() override;

  std::unique_ptr<PageSync> CreatePageSync(
      storage::PageStorage* page_storage,
      storage::PageSyncClient* page_sync_client,
      fit::closure error_callback) override;

  // Enables upload. Has no effect if this method has already been called.
  void EnableUpload();

  bool IsUploadEnabled() const { return upload_enabled_; }

  // |on_delete| will be called when this class is deleted.
  void set_on_delete(fit::function<void()> on_delete) {
    FXL_DCHECK(!on_delete_);
    on_delete_ = std::move(on_delete);
  }

 private:
  ledger::Environment* const environment_;
  const UserConfig* const user_config_;
  encryption::EncryptionService* const encryption_service_;
  const std::string app_id_;
  bool upload_enabled_ = false;
  std::set<PageSyncImpl*> active_page_syncs_;
  // Called on destruction.
  fit::function<void()> on_delete_;
  std::unique_ptr<SyncStateWatcher> user_watcher_;
  Aggregator aggregator_;
};

}  // namespace cloud_sync

#endif  // PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_LEDGER_SYNC_IMPL_H_

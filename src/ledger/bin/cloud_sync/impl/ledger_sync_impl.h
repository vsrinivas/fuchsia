// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_LEDGER_SYNC_IMPL_H_
#define SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_LEDGER_SYNC_IMPL_H_

#include <lib/fit/function.h>

#include <memory>
#include <set>

#include "src/ledger/bin/cloud_sync/impl/aggregator.h"
#include "src/ledger/bin/cloud_sync/impl/page_sync_impl.h"
#include "src/ledger/bin/cloud_sync/public/ledger_sync.h"
#include "src/ledger/bin/cloud_sync/public/sync_state_watcher.h"
#include "src/ledger/bin/cloud_sync/public/user_config.h"
#include "src/ledger/bin/encryption/public/encryption_service.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/lib/logging/logging.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace cloud_sync {

class LedgerSyncImpl : public LedgerSync {
 public:
  LedgerSyncImpl(ledger::Environment* environment, const UserConfig* user_config,
                 encryption::EncryptionService* encryption_service, absl::string_view app_id,
                 std::unique_ptr<SyncStateWatcher> watcher);
  ~LedgerSyncImpl() override;

  void CreatePageSync(
      storage::PageStorage* page_storage, storage::PageSyncClient* page_sync_client,
      fit::function<void(storage::Status, std::unique_ptr<PageSync>)> callback) override;

  // Enables upload. Has no effect if this method has already been called.
  void EnableUpload();

  bool IsUploadEnabled() const { return upload_enabled_; }

  // |on_delete| will be called when this class is deleted.
  void set_on_delete(fit::function<void()> on_delete) {
    LEDGER_DCHECK(!on_delete_);
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

#endif  // SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_LEDGER_SYNC_IMPL_H_

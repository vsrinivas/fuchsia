// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_PAGE_UPLOAD_H_
#define PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_PAGE_UPLOAD_H_

#include <memory>
#include <vector>

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/backoff/backoff.h>
#include <lib/callback/scoped_task_runner.h>
#include <lib/fit/function.h>
#include <lib/fxl/memory/weak_ptr.h>

#include "peridot/bin/ledger/cloud_sync/impl/batch_upload.h"
#include "peridot/bin/ledger/cloud_sync/public/sync_state_watcher.h"
#include "peridot/bin/ledger/encryption/public/encryption_service.h"
#include "peridot/bin/ledger/storage/public/commit.h"
#include "peridot/bin/ledger/storage/public/commit_watcher.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace cloud_sync {
// Internal state of PageUpload.
// This ensures there is only one stream of work at any given time, and one in
// "backlog".
enum PageUploadState {
  // No upload attempt is in progress.
  NO_COMMIT = 0,
  // An upload attempt is in progress and after completing we should become
  // idle.
  PROCESSING,
  // An upload attempt is in progress and after completing we should start a new
  // one.
  PROCESSING_NEW_COMMIT,
};

// PageUpload handles all the upload operations for a page.
class PageUpload : public storage::CommitWatcher {
 public:
  // Delegate ensuring coordination between PageUpload and the class that owns
  // it.
  class Delegate {
   public:
    // Reports that the upload state changed.
    virtual void SetUploadState(UploadSyncState sync_state) = 0;

    // Returns true if no download is in progress.
    virtual bool IsDownloadIdle() = 0;
  };

  PageUpload(callback::ScopedTaskRunner* task_runner,
             storage::PageStorage* storage,
             encryption::EncryptionService* encryption_service,
             cloud_provider::PageCloudPtr* page_cloud, Delegate* delegate,
             std::unique_ptr<backoff::Backoff> backoff);

  ~PageUpload() override;

  // Starts or restarts the upload process.
  //
  // The first time this method is called this sets up the storage watcher. It
  // might be called again in the future to restart the upload after it's
  // stopped due to a remote download in progress.
  void StartUpload();

  // Returns true if PageUpload is idle.
  bool IsIdle();

 private:
  // storage::CommitWatcher:
  void OnNewCommits(
      const std::vector<std::unique_ptr<const storage::Commit>>& /*commits*/,
      storage::ChangeSource source) override;

  void UploadUnsyncedCommits();
  void VerifyUnsyncedCommits(
      std::vector<std::unique_ptr<const storage::Commit>> commits);
  void HandleUnsyncedCommits(
      std::vector<std::unique_ptr<const storage::Commit>> commits);

  // Sets the internal state.
  void SetState(UploadSyncState new_state);

  void HandleError(const char error_description[]);

  void RetryWithBackoff(fit::closure callable);

  // These methods manage the internal state machine.

  // Registers a signal to trigger an upload attempt, and triggers it if
  // appropriate, that is, if we don't have an upload process already in
  // progress.
  void NextState();

  // Registers completion of an upload attempt, for example due to an error, or
  // because it completed. This will trigger another upload attempt if
  // appropriate, that is, if a signal to trigger an upload attempt was
  // delivered while an earlier upload attempt was in progress.
  void PreviousState();

  // Owned by whoever owns this class.
  callback::ScopedTaskRunner* const task_runner_;
  storage::PageStorage* const storage_;
  encryption::EncryptionService* const encryption_service_;
  cloud_provider::PageCloudPtr* const page_cloud_;
  Delegate* const delegate_;
  const std::string log_prefix_;

  std::unique_ptr<backoff::Backoff> backoff_;

  // Work queue:
  // Current batch of local commits being uploaded.
  std::unique_ptr<BatchUpload> batch_upload_;
  // Internal state.
  PageUploadState internal_state_ = PageUploadState::NO_COMMIT;

  // External state.
  UploadSyncState external_state_ = UPLOAD_NOT_STARTED;

  // Must be the last member.
  fxl::WeakPtrFactory<PageUpload> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageUpload);
};

}  // namespace cloud_sync

#endif  // PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_PAGE_UPLOAD_H_

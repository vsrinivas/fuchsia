// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_PAGE_DOWNLOAD_H_
#define SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_PAGE_DOWNLOAD_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>

#include "src/ledger/bin/cloud_sync/impl/batch_download.h"
#include "src/ledger/bin/cloud_sync/public/sync_state_watcher.h"
#include "src/ledger/bin/encryption/public/encryption_service.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/storage/public/page_sync_delegate.h"
#include "src/lib/backoff/backoff.h"
#include "src/lib/callback/managed_container.h"
#include "src/lib/callback/scoped_task_runner.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace cloud_sync {
// PageDownload handles all the download operations (commits and objects) for a page.
class PageDownload : public cloud_provider::PageCloudWatcher, public storage::PageSyncDelegate {
 public:
  // Delegate ensuring coordination between PageDownload and the class that owns it.
  class Delegate {
   public:
    // Report that the download state changed.
    virtual void SetDownloadState(DownloadSyncState sync_state) = 0;
  };

  PageDownload(callback::ScopedTaskRunner* task_runner, storage::PageStorage* storage,
               storage::PageSyncClient* sync_client,
               encryption::EncryptionService* encryption_service,
               cloud_provider::PageCloudPtr* page_cloud, Delegate* delegate,
               std::unique_ptr<backoff::Backoff> backoff);
  PageDownload(const PageDownload&) = delete;
  PageDownload& operator=(const PageDownload&) = delete;

  ~PageDownload() override;

  // Downloads the initial backlog of remote commits, and sets up the remote watcher upon success.
  void StartDownload();

  // Returns if PageDownload is paused (idle or in backoff).
  bool IsPaused();

  // Returns if PageDownload is idle (all remote commits downloaded).
  bool IsIdle();

 private:
  // cloud_provider::PageCloudWatcher:
  void OnNewCommits(cloud_provider::CommitPack commits,
                    cloud_provider::PositionToken position_token,
                    OnNewCommitsCallback callback) override;

  void OnNewObject(std::vector<uint8_t> id, fuchsia::mem::Buffer data,
                   OnNewObjectCallback callback) override;

  void OnError(cloud_provider::Status status) override;

  // Called when the initial commit backlog is downloaded.
  void BacklogDownloaded();

  // Starts watching for Cloud commit notifications.
  void SetRemoteWatcher(bool is_retry);

  // Downloads the given batch of commits.
  void DownloadBatch(std::vector<cloud_provider::Commit> entries,
                     std::unique_ptr<cloud_provider::PositionToken> position_token,
                     fit::closure on_done);

  // storage::PageSyncDelegate:
  void GetObject(storage::ObjectIdentifier object_identifier,
                 storage::RetrievedObjectType retrieved_object_type,
                 fit::function<void(ledger::Status, storage::ChangeSource, storage::IsObjectSynced,
                                    std::unique_ptr<storage::DataSource::DataChunk>)>
                     callback) override;
  void GetDiff(
      storage::CommitId commit_id, std::vector<storage::CommitId> possible_bases,
      fit::function<void(ledger::Status, storage::CommitId, std::vector<storage::EntryChange>)>
          callback) override;

  // Actual implementation of |GetObject|: |retrieved_object_type| is ignored at this level.
  void GetObject(storage::ObjectIdentifier object_identifier,
                 fit::function<void(ledger::Status, storage::ChangeSource, storage::IsObjectSynced,
                                    std::unique_ptr<storage::DataSource::DataChunk>)>
                     callback);

  void DecryptObject(
      storage::ObjectIdentifier object_identifier, std::unique_ptr<storage::DataSource> content,
      fit::function<void(ledger::Status, storage::ChangeSource, storage::IsObjectSynced,
                         std::unique_ptr<storage::DataSource::DataChunk>)>
          callback);

  void ReadDiffEntry(const cloud_provider::DiffEntry& change,
                     fit::function<void(ledger::Status, storage::EntryChange)> callback);

  void DecodeAndParseDiff(
      const cloud_provider::DiffPack& diff_pack,
      fit::function<void(ledger::Status, storage::CommitId, std::vector<storage::EntryChange>)>
          callback);

  void HandleGetObjectError(
      storage::ObjectIdentifier object_identifier, bool is_permanent, fxl::StringView error_name,
      fit::function<void(ledger::Status, storage::ChangeSource, storage::IsObjectSynced,
                         std::unique_ptr<storage::DataSource::DataChunk>)>
          callback);

  void HandleGetDiffError(
      storage::CommitId commit_id, std::vector<storage::CommitId> possible_bases, bool is_permanent,
      fxl::StringView error_name,
      fit::function<void(ledger::Status, storage::CommitId, std::vector<storage::EntryChange>)>
          callback);

  void HandleDownloadCommitError(fxl::StringView error_description);

  // Sets the state for commit download.
  void SetCommitState(DownloadSyncState new_state);
  void UpdateDownloadState();

  void RetryWithBackoff(fit::closure callable);

  // Owned by whoever owns this class.
  callback::ScopedTaskRunner* const task_runner_;
  storage::PageStorage* const storage_;
  storage::PageSyncClient* sync_client_;
  encryption::EncryptionService* const encryption_service_;
  cloud_provider::PageCloudPtr* const page_cloud_;
  Delegate* const delegate_;

  std::unique_ptr<backoff::Backoff> backoff_;

  const std::string log_prefix_;

  // Work queue:
  // The current batch of remote commits being downloaded.
  std::unique_ptr<BatchDownload> batch_download_;
  // Pending remote commits to download.
  std::vector<cloud_provider::Commit> commits_to_download_;
  std::unique_ptr<cloud_provider::PositionToken> position_token_;
  // Container for in-progress datasource.
  callback::ManagedContainer managed_container_;

  // State:
  // Commit download state.
  DownloadSyncState commit_state_ = DOWNLOAD_NOT_STARTED;
  // The number of active GetObject and GetDiff calls.
  int current_get_calls_ = 0;
  // Merged state of commit and object download.
  DownloadSyncState merged_state_ = DOWNLOAD_NOT_STARTED;

  fidl::Binding<cloud_provider::PageCloudWatcher> watcher_binding_;

  // Must be the last member.
  fxl::WeakPtrFactory<PageDownload> weak_factory_;
};

}  // namespace cloud_sync

#endif  // SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_PAGE_DOWNLOAD_H_

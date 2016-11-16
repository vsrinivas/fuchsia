// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_PAGE_SYNC_IMPL_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_PAGE_SYNC_IMPL_H_

#include <queue>

#include "apps/ledger/src/backoff/backoff.h"
#include "apps/ledger/src/cloud_provider/public/cloud_provider.h"
#include "apps/ledger/src/cloud_sync/impl/commit_upload.h"
#include "apps/ledger/src/cloud_sync/public/page_sync.h"
#include "apps/ledger/src/storage/public/commit_watcher.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/ftl/time/time_delta.h"

namespace cloud_sync {

// Manages cloud sync for a single page.
//
// Contract: commits are uploaded in the same order as storage delivers them.
// The backlog of unsynced commits is uploaded first, then we upload commits
// delivered through storage watcher in the notification order.
//
// Recoverable errors (such as network errors) are automatically retried with
// the given backoff policy, using the given task runner to schedule the tasks.
// TODO(ppi): once the network service can notify us about regained
// connectivity, thread this signal through CloudProvider and use it as a signal
// to trigger retries.
//
// Unrecoverable errors (such as internal errors accessing the storage) cause
// the page sync to stop, in which case the client is notified using the given
// error callback.
class PageSyncImpl : public PageSync, public storage::CommitWatcher {
 public:
  PageSyncImpl(ftl::RefPtr<ftl::TaskRunner> task_runner,
               storage::PageStorage* storage,
               cloud_provider::CloudProvider* cloud_provider,
               std::unique_ptr<backoff::Backoff> backoff,
               std::function<void()> error_callback);
  ~PageSyncImpl() override;

  void Start() override;

  // storage::CommitWatcher:
  void OnNewCommit(const storage::Commit& commit,
                   storage::ChangeSource source) override;

 private:
  void EnqueueUpload(std::unique_ptr<const storage::Commit> commit);

  void HandleError(const char error_description[]);

  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  storage::PageStorage* const storage_;
  cloud_provider::CloudProvider* const cloud_provider_;
  const std::unique_ptr<backoff::Backoff> backoff_;
  const std::function<void()> error_callback_;

  // Ensures that each instance is started only once.
  bool started_ = false;
  // Set to true on unrecoverable error. This indicates that PageSyncImpl is in
  // broken state without a storage watcher registered.
  bool errored_ = false;
  std::queue<CommitUpload> commit_uploads_;

  // Must be the last member field.
  ftl::WeakPtrFactory<PageSyncImpl> weak_factory_;
};

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_PAGE_SYNC_IMPL_H_

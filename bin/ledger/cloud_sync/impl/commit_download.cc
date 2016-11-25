// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/impl/commit_download.h"

#include <utility>

namespace cloud_sync {

CommitDownload::CommitDownload(storage::PageStorage* storage,
                               std::vector<cloud_provider::Record> records,
                               ftl::Closure on_done,
                               ftl::Closure on_error)
    : storage_(storage),
      records_(std::move(records)),
      on_done_(std::move(on_done)),
      on_error_(std::move(on_error)) {
  FTL_DCHECK(storage);
}

CommitDownload::~CommitDownload() {}

void CommitDownload::Start() {
  FTL_DCHECK(!started_);
  started_ = true;
  std::vector<storage::PageStorage::CommitIdAndBytes> commits;
  for (auto& record : records_) {
    commits.push_back(storage::PageStorage::CommitIdAndBytes(
        std::move(record.commit.id), std::move(record.commit.content)));
  }
  storage_->AddCommitsFromSync(
      std::move(commits), [this](storage::Status status) {
        if (status != storage::Status::OK) {
          on_error_();
          return;
        }

        if (storage_->SetSyncMetadata(std::move(records_.back().timestamp)) !=
            storage::Status::OK) {
          on_error_();
          return;
        }

        on_done_();
      });
}

}  // namespace cloud_sync

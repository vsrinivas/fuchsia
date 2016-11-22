// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/impl/commit_download.h"

#include <utility>

namespace cloud_sync {

CommitDownload::CommitDownload(storage::PageStorage* storage,
                               cloud_provider::Record record,
                               ftl::Closure done_callback,
                               ftl::Closure error_callback)
    : storage_(storage),
      record_(std::move(record)),
      done_callback_(std::move(done_callback)),
      error_callback_(std::move(error_callback)) {
  FTL_DCHECK(storage);
}

CommitDownload::~CommitDownload() {}

void CommitDownload::Start() {
  FTL_DCHECK(!started_);
  started_ = true;
  storage_->AddCommitFromSync(
      record_.commit.id, std::move(record_.commit.content),
      [this](storage::Status status) {
        if (status != storage::Status::OK) {
          error_callback_();
          return;
        }

        if (storage_->SetSyncMetadata(std::move(record_.timestamp)) !=
            storage::Status::OK) {
          error_callback_();
          return;
        }

        done_callback_();
      });
}

}  // namespace cloud_sync

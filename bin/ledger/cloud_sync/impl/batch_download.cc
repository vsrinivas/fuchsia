// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/impl/batch_download.h"

#include <utility>

#include "peridot/bin/ledger/callback/scoped_callback.h"
#include "peridot/bin/ledger/cloud_sync/impl/constants.h"

namespace cloud_sync {

BatchDownload::BatchDownload(
    storage::PageStorage* storage,
    std::vector<cloud_provider_firebase::Record> records,
    fxl::Closure on_done,
    fxl::Closure on_error)
    : storage_(storage),
      records_(std::move(records)),
      on_done_(std::move(on_done)),
      on_error_(std::move(on_error)),
      weak_ptr_factory_(this) {
  FXL_DCHECK(storage);
}

BatchDownload::~BatchDownload() {}

void BatchDownload::Start() {
  FXL_DCHECK(!started_);
  started_ = true;
  std::vector<storage::PageStorage::CommitIdAndBytes> commits;
  for (auto& record : records_) {
    commits.emplace_back(std::move(record.commit.id),
                         std::move(record.commit.content));
  }
  storage_->AddCommitsFromSync(
      std::move(commits),
      callback::MakeScoped(weak_ptr_factory_.GetWeakPtr(),
                           [this](storage::Status status) {
                             if (status != storage::Status::OK) {
                               on_error_();
                               return;
                             }

                             UpdateTimestampAndQuit();
                           }));
}

void BatchDownload::UpdateTimestampAndQuit() {
  storage_->SetSyncMetadata(
      kTimestampKey, records_.back().timestamp,
      callback::MakeScoped(weak_ptr_factory_.GetWeakPtr(),
                           [this](storage::Status status) {
                             if (status != storage::Status::OK) {
                               on_error_();
                               return;
                             }

                             // Can be deleted within.
                             on_done_();
                           }));
}

}  // namespace cloud_sync

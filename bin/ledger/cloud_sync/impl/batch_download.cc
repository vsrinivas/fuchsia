// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/impl/batch_download.h"

#include <utility>

#include <trace/event.h>

#include "lib/callback/scoped_callback.h"
#include "lib/callback/waiter.h"
#include "peridot/bin/ledger/cloud_sync/impl/constants.h"

namespace cloud_sync {

BatchDownload::BatchDownload(
    storage::PageStorage* storage,
    encryption::EncryptionService* encryption_service,
    fidl::VectorPtr<cloud_provider::Commit> commits,
    std::unique_ptr<cloud_provider::Token> position_token, fxl::Closure on_done,
    fxl::Closure on_error)
    : storage_(storage),
      encryption_service_(encryption_service),
      commits_(std::move(commits)),
      position_token_(std::move(position_token)),
      on_done_(std::move(on_done)),
      on_error_(std::move(on_error)),
      weak_ptr_factory_(this) {
  FXL_DCHECK(storage);
  TRACE_ASYNC_BEGIN("ledger", "batch_download",
                    reinterpret_cast<uintptr_t>(this));
}

BatchDownload::~BatchDownload() {
  TRACE_ASYNC_END("ledger", "batch_download",
                  reinterpret_cast<uintptr_t>(this));
}

void BatchDownload::Start() {
  FXL_DCHECK(!started_);
  started_ = true;
  auto waiter = fxl::MakeRefCounted<callback::Waiter<
      encryption::Status, storage::PageStorage::CommitIdAndBytes>>(
      encryption::Status::OK);
  for (auto& commit : *commits_) {
    encryption_service_->DecryptCommit(
        convert::ToString(commit.data),
        [id = convert::ToString(commit.id), callback = waiter->NewCallback()](
            encryption::Status status, std::string content) mutable {
          callback(status, storage::PageStorage::CommitIdAndBytes(
                               std::move(id), std::move(content)));
        });
  }
  waiter->Finalize(callback::MakeScoped(
      weak_ptr_factory_.GetWeakPtr(),
      [this](encryption::Status status,
             std::vector<storage::PageStorage::CommitIdAndBytes> commits) {
        if (status != encryption::Status::OK) {
          on_error_();
          return;
        }

        storage_->AddCommitsFromSync(
            std::move(commits), storage::ChangeSource::CLOUD,
            callback::MakeScoped(weak_ptr_factory_.GetWeakPtr(),
                                 [this](storage::Status status) {
                                   if (status != storage::Status::OK) {
                                     on_error_();
                                     return;
                                   }

                                   UpdateTimestampAndQuit();
                                 }));
      }));
}

void BatchDownload::UpdateTimestampAndQuit() {
  if (!position_token_) {
    // Can be deleted within.
    on_done_();
    return;
  }

  storage_->SetSyncMetadata(
      kTimestampKey, convert::ToString(position_token_->opaque_id),
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

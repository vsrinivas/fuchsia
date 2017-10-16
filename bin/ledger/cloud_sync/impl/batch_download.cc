// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/impl/batch_download.h"

#include <utility>

#include <trace/event.h>

#include "peridot/bin/ledger/callback/scoped_callback.h"
#include "peridot/bin/ledger/callback/waiter.h"
#include "peridot/bin/ledger/cloud_sync/impl/constants.h"

namespace cloud_sync {

BatchDownload::BatchDownload(
    storage::PageStorage* storage,
    encryption::EncryptionService* encryption_service,
    std::vector<cloud_provider_firebase::Record> records,
    fxl::Closure on_done,
    fxl::Closure on_error)
    : storage_(storage),
      encryption_service_(encryption_service),
      records_(std::move(records)),
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
  auto waiter = callback::Waiter<
      encryption::Status,
      storage::PageStorage::CommitIdAndBytes>::Create(encryption::Status::OK);
  for (auto& record : records_) {
    encryption_service_->DecryptCommit(
        record.commit.content,
        [id = std::move(record.commit.id), callback = waiter->NewCallback()](
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
            std::move(commits),
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

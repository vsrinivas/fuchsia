// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/app/page_impl.h"

#include <string>
#include <utility>
#include <vector>

#include "apps/ledger/convert/convert.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/data_pipe/strings.h"

namespace ledger {
namespace {
Status ConvertStatus(storage::Status status) {
  if (status != storage::Status::OK) {
    return Status::IO_ERROR;
  } else {
    return Status::OK;
  }
}

}  // namespace

PageImpl::PageImpl(storage::PageStorage* storage) : storage_(storage) {}

PageImpl::~PageImpl() {}

storage::CommitId PageImpl::GetLocalBranchHeadCommit() {
  std::vector<storage::CommitId> commit_ids;
  // TODO(etiennej): Fail more nicely.
  FTL_CHECK(storage_->GetHeadCommitIds(&commit_ids) == storage::Status::OK);
  FTL_DCHECK(commit_ids.size() > 0);
  if (commit_ids.size() == 1) {
    return commit_ids[0];
  }
  // TODO(etiennej): Make sure we stay on the same branch. We can do it
  // inefficiently here, or maybe storage can keep some additional data for us?
  return commit_ids[0];
}

// GetId() => (array<uint8> id);
void PageImpl::GetId(const GetIdCallback& callback) {
  callback.Run(convert::ToArray(storage_->GetId()));
}

// GetSnapshot() => (Status status, PageSnapshot? snapshot);
void PageImpl::GetSnapshot(const GetSnapshotCallback& callback) {
  FTL_LOG(ERROR) << "PageImpl::GetSnapshot not implemented";
  callback.Run(Status::UNKNOWN_ERROR, nullptr);
}

// Watch(PageWatcher watcher) => (Status status);
void PageImpl::Watch(mojo::InterfaceHandle<PageWatcher> watcher,
                     const WatchCallback& callback) {
  FTL_LOG(ERROR) << "PageImpl::Watch not implemented";
  callback.Run(Status::UNKNOWN_ERROR);
}

Status PageImpl::RunInTransaction(
    std::function<Status(storage::Journal* journal)> callback) {
  if (journal_) {
    // A transaction is in progress; add this change to it.
    return callback(journal_.get());
  }
  // No transaction is in progress; create one just for this change.
  // TODO(etiennej): Add a change batching strategy for operations outside
  // transactions. Currently, we create a commit for every change; we would
  // like to group changes that happen "close enough" together in one commit.
  storage::CommitId commit_id = GetLocalBranchHeadCommit();
  std::unique_ptr<storage::Journal> journal;
  storage::Status status = storage_->StartCommit(
      commit_id, storage::JournalType::IMPLICIT, &journal);
  if (status != storage::Status::OK) {
    return ConvertStatus(status);
  }
  Status ledger_status = callback(journal.get());
  if (ledger_status != Status::OK) {
    return ledger_status;
  }
  storage::CommitId new_commit_id;
  status = journal->Commit(&new_commit_id);
  return ConvertStatus(status);
}

// Put(array<uint8> key, array<uint8> value) => (Status status);
void PageImpl::Put(mojo::Array<uint8_t> key,
                   mojo::Array<uint8_t> value,
                   const PutCallback& callback) {
  PutWithPriority(std::move(key), std::move(value), Priority::EAGER, callback);
}

// PutWithPriority(array<uint8> key, array<uint8> value, Priority priority)
//   => (Status status);
void PageImpl::PutWithPriority(mojo::Array<uint8_t> key,
                               mojo::Array<uint8_t> value,
                               Priority priority,
                               const PutWithPriorityCallback& callback) {
  // Store the value.
  storage::ObjectId object_id;
  // TODO(etiennej): Use asynchronous write, otherwise the run loop may block
  // until the pipe is drained.
  mojo::ScopedDataPipeConsumerHandle data_pipe =
      mtl::WriteStringToConsumerHandle(convert::ToStringView(value));
  storage::Status status = storage_->AddObjectFromLocal(
      data_pipe.release(), value.size(), &object_id);
  if (status != storage::Status::OK) {
    callback.Run(ConvertStatus(status));
    return;
  }

  callback.Run(PutInCommit(key, object_id, priority == Priority::EAGER
                                               ? storage::KeyPriority::EAGER
                                               : storage::KeyPriority::LAZY));
}

// PutReference(array<uint8> key, Reference? reference, Priority priority)
//   => (Status status);
void PageImpl::PutReference(mojo::Array<uint8_t> key,
                            ReferencePtr reference,
                            Priority priority,
                            const PutReferenceCallback& callback) {
  storage::ObjectIdView object_id(reference->opaque_id);
  callback.Run(PutInCommit(key, object_id, priority == Priority::EAGER
                                               ? storage::KeyPriority::EAGER
                                               : storage::KeyPriority::LAZY));
}

Status PageImpl::PutInCommit(convert::ExtendedStringView key,
                             storage::ObjectIdView object_id,
                             storage::KeyPriority priority) {
  return RunInTransaction(
      [&key, &object_id, &priority](storage::Journal* journal) {
        return ConvertStatus(journal->Put(key, object_id, priority));
      });
}

// Delete(array<uint8> key) => (Status status);
void PageImpl::Delete(mojo::Array<uint8_t> key,
                      const DeleteCallback& callback) {
  callback.Run(RunInTransaction([&key](storage::Journal* journal) {
    return ConvertStatus(journal->Delete(key));
  }));
}

// CreateReference(int64 size, handle<data_pipe_producer> data)
//   => (Status status, Reference reference);
void PageImpl::CreateReference(int64_t size,
                               mojo::ScopedDataPipeConsumerHandle data,
                               const CreateReferenceCallback& callback) {
  FTL_LOG(ERROR) << "PageImpl::CreateReference not implemented";
  callback.Run(Status::UNKNOWN_ERROR, nullptr);
}

// GetReference(Reference reference) => (Status status, Value? value);
void PageImpl::GetReference(ReferencePtr reference,
                            const GetReferenceCallback& callback) {
  FTL_LOG(ERROR) << "PageImpl::GetReference not implemented";
  callback.Run(Status::UNKNOWN_ERROR, nullptr);
}

// GetPartialReference(Reference reference, int64 offset, int64 max_size)
//   => (Status status, Stream? stream);
void PageImpl::GetPartialReference(
    ReferencePtr reference,
    int64_t offset,
    int64_t max_size,
    const GetPartialReferenceCallback& callback) {
  FTL_LOG(ERROR) << "PageImpl::GetPartialReference not implemented";
  callback.Run(Status::UNKNOWN_ERROR, nullptr);
}

// StartTransaction() => (Status status);
void PageImpl::StartTransaction(const StartTransactionCallback& callback) {
  if (journal_) {
    callback.Run(Status::TRANSACTION_ALREADY_IN_PROGRESS);
    return;
  }
  storage::CommitId commit_id = GetLocalBranchHeadCommit();
  storage::Status status = storage_->StartCommit(
      commit_id, storage::JournalType::EXPLICIT, &journal_);
  callback.Run(ConvertStatus(status));
}

// Commit() => (Status status);
void PageImpl::Commit(const CommitCallback& callback) {
  if (!journal_) {
    callback.Run(Status::NO_TRANSACTION_IN_PROGRESS);
    return;
  }
  storage::CommitId new_commit_id;
  storage::Status status = journal_->Commit(&new_commit_id);
  callback.Run(ConvertStatus(status));
}

// Rollback() => (Status status);
void PageImpl::Rollback(const RollbackCallback& callback) {
  if (!journal_) {
    callback.Run(Status::NO_TRANSACTION_IN_PROGRESS);
    return;
  }
  storage::Status status = journal_->Rollback();
  callback.Run(ConvertStatus(status));
}

}  // namespace ledger

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/page_impl.h"

#include <string>
#include <utility>
#include <vector>

#include "apps/ledger/src/app/constants.h"
#include "apps/ledger/src/app/page_manager.h"
#include "apps/ledger/src/app/page_snapshot_impl.h"
#include "apps/ledger/src/app/page_utils.h"
#include "apps/ledger/src/convert/convert.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/data_pipe/strings.h"
#include "lib/mtl/shared_buffer/strings.h"

namespace ledger {

PageImpl::PageImpl(PageManager* manager, storage::PageStorage* storage)
    : manager_(manager), storage_(storage) {}

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

// GetSnapshot(PageSnapshot& snapshot) => (Status status);
void PageImpl::GetSnapshot(
    mojo::InterfaceRequest<PageSnapshot> snapshot_request,
    const GetSnapshotCallback& callback) {
  // TODO(etiennej): Commit implicit transactions when we have those.
  storage::CommitId commit_id;
  if (!journal_) {
    commit_id = GetLocalBranchHeadCommit();
  } else {
    commit_id = journal_parent_commit_;
  }
  std::unique_ptr<const storage::Commit> commit;
  storage::Status status = storage_->GetCommit(commit_id, &commit);
  if (status != storage::Status::OK) {
    callback.Run(PageUtils::ConvertStatus(status));
    return;
  }
  manager_->BindPageSnapshot(commit->GetContents(),
                             std::move(snapshot_request));
  callback.Run(Status::OK);
}

// Watch(PageWatcher watcher) => (Status status);
void PageImpl::Watch(mojo::InterfaceHandle<PageWatcher> watcher,
                     const WatchCallback& callback) {
  FTL_LOG(ERROR) << "PageImpl::Watch not implemented";
  callback.Run(Status::UNKNOWN_ERROR);
}

void PageImpl::RunInTransaction(
    std::function<Status(storage::Journal* journal)> runnable,
    std::function<void(Status)> callback) {
  if (journal_) {
    // A transaction is in progress; add this change to it.
    callback(runnable(journal_.get()));
    return;
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
    callback(PageUtils::ConvertStatus(status));
    journal->Rollback();
    return;
  }
  Status ledger_status = runnable(journal.get());
  if (ledger_status != Status::OK) {
    callback(ledger_status);
    journal->Rollback();
    return;
  }

  CommitJournal(std::move(journal), callback);
}

void PageImpl::CommitJournal(std::unique_ptr<storage::Journal> journal,
                             std::function<void(Status)> callback) {
  storage::Journal* journal_ptr = journal.get();
  in_progress_journals_.push_back(std::move(journal));

  journal_ptr->Commit([this, callback, journal_ptr](
      storage::Status status, const storage::CommitId& commit_id) {
    in_progress_journals_.erase(std::remove_if(
        in_progress_journals_.begin(), in_progress_journals_.end(),
        [&journal_ptr](const std::unique_ptr<storage::Journal>& journal) {
          return journal_ptr == journal.get();
        }));
    callback(PageUtils::ConvertStatus(status));
  });
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
  // TODO(etiennej): Use asynchronous write, otherwise the run loop may block
  // until the pipe is drained.
  mojo::ScopedDataPipeConsumerHandle data_pipe =
      mtl::WriteStringToConsumerHandle(convert::ToStringView(value));
  storage_->AddObjectFromLocal(
      std::move(data_pipe), value.size(),
      ftl::MakeCopyable([ this, key = std::move(key), priority, callback ](
          storage::Status status, storage::ObjectId object_id) {
        if (status != storage::Status::OK) {
          callback.Run(PageUtils::ConvertStatus(status));
          return;
        }

        PutInCommit(key, object_id,
                    priority == Priority::EAGER ? storage::KeyPriority::EAGER
                                                : storage::KeyPriority::LAZY,
                    [callback](Status status) { callback.Run(status); });
      }));
}

// PutReference(array<uint8> key, Reference? reference, Priority priority)
//   => (Status status);
void PageImpl::PutReference(mojo::Array<uint8_t> key,
                            ReferencePtr reference,
                            Priority priority,
                            const PutReferenceCallback& callback) {
  storage::ObjectIdView object_id(reference->opaque_id);
  PutInCommit(key, object_id,
              priority == Priority::EAGER ? storage::KeyPriority::EAGER
                                          : storage::KeyPriority::LAZY,
              [callback](Status status) { callback.Run(status); });
}

void PageImpl::PutInCommit(convert::ExtendedStringView key,
                           storage::ObjectIdView object_id,
                           storage::KeyPriority priority,
                           std::function<void(Status)> callback) {
  RunInTransaction(
      [&key, &object_id, &priority](storage::Journal* journal) {
        return PageUtils::ConvertStatus(journal->Put(key, object_id, priority));
      },
      callback);
}

// Delete(array<uint8> key) => (Status status);
void PageImpl::Delete(mojo::Array<uint8_t> key,
                      const DeleteCallback& callback) {
  RunInTransaction(
      [&key](storage::Journal* journal) {
        return PageUtils::ConvertStatus(journal->Delete(key),
                                        Status::KEY_NOT_FOUND);
      },
      [callback](Status status) { callback.Run(status); });
}

// CreateReference(int64 size, handle<data_pipe_producer> data)
//   => (Status status, Reference reference);
void PageImpl::CreateReference(int64_t size,
                               mojo::ScopedDataPipeConsumerHandle data,
                               const CreateReferenceCallback& callback) {
  storage_->AddObjectFromLocal(
      std::move(data), size,
      [callback](storage::Status status, storage::ObjectId object_id) {
        if (status != storage::Status::OK) {
          callback.Run(PageUtils::ConvertStatus(status), nullptr);
          return;
        }

        ReferencePtr reference = Reference::New();
        reference->opaque_id = convert::ToArray(object_id);
        callback.Run(Status::OK, std::move(reference));
      });
}

// GetReference(Reference reference) => (Status status, Value? value);
void PageImpl::GetReference(ReferencePtr reference,
                            const GetReferenceCallback& callback) {
  PageUtils::GetReferenceAsValuePtr(storage_, reference->opaque_id,
                                    [callback](Status status, ValuePtr value) {
                                      callback.Run(status, std::move(value));
                                    });
}

// GetPartialReference(Reference reference, int64 offset, int64 max_size)
//   => (Status status, handle<shared_buffer>? buffer);
void PageImpl::GetPartialReference(
    ReferencePtr reference,
    int64_t offset,
    int64_t max_size,
    const GetPartialReferenceCallback& callback) {
  PageUtils::GetPartialReferenceAsBuffer(
      storage_, reference->opaque_id, offset, max_size,
      [callback](Status status, mojo::ScopedSharedBufferHandle buffer) {
        callback.Run(status, std::move(buffer));
      });
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
  journal_parent_commit_ = commit_id;
  callback.Run(PageUtils::ConvertStatus(status));
}

// Commit() => (Status status);
void PageImpl::Commit(const CommitCallback& callback) {
  if (!journal_) {
    callback.Run(Status::NO_TRANSACTION_IN_PROGRESS);
    return;
  }
  journal_parent_commit_.clear();
  CommitJournal(std::move(journal_),
                [callback](Status status) { callback.Run(status); });
}

// Rollback() => (Status status);
void PageImpl::Rollback(const RollbackCallback& callback) {
  if (!journal_) {
    callback.Run(Status::NO_TRANSACTION_IN_PROGRESS);
    return;
  }
  storage::Status status = journal_->Rollback();
  journal_.reset();
  journal_parent_commit_.clear();
  callback.Run(PageUtils::ConvertStatus(status));
}

}  // namespace ledger

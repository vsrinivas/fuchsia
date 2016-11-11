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

namespace ledger {

PageImpl::PageImpl(PageManager* manager,
                   storage::PageStorage* storage,
                   BranchTracker* branch_tracker)
    : manager_(manager), storage_(storage), branch_tracker_(branch_tracker) {}

PageImpl::~PageImpl() {}

// GetId() => (array<uint8> id);
void PageImpl::GetId(const GetIdCallback& callback) {
  callback(convert::ToArray(storage_->GetId()));
}

// GetSnapshot(PageSnapshot& snapshot) => (Status status);
void PageImpl::GetSnapshot(
    fidl::InterfaceRequest<PageSnapshot> snapshot_request,
    const GetSnapshotCallback& callback) {
  // TODO(etiennej): Commit implicit transactions when we have those.
  storage::CommitId commit_id;
  if (!journal_) {
    commit_id = branch_tracker_->GetBranchHeadId();
  } else {
    commit_id = journal_parent_commit_;
  }
  std::unique_ptr<const storage::Commit> commit;
  storage::Status status = storage_->GetCommit(commit_id, &commit);
  if (status != storage::Status::OK) {
    callback(PageUtils::ConvertStatus(status));
    return;
  }
  manager_->BindPageSnapshot(commit->GetContents(),
                             std::move(snapshot_request));
  callback(Status::OK);
}

// Watch(PageWatcher watcher) => (Status status);
void PageImpl::Watch(fidl::InterfaceHandle<PageWatcher> watcher,
                     const WatchCallback& callback) {
  FTL_LOG(ERROR) << "PageImpl::Watch not implemented";
  callback(Status::UNKNOWN_ERROR);
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
  storage::CommitId commit_id = branch_tracker_->GetBranchHeadId();
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
    if (status == storage::Status::OK) {
      branch_tracker_->SetBranchHead(commit_id);
    }
    callback(PageUtils::ConvertStatus(status));
  });
}

// Put(array<uint8> key, array<uint8> value) => (Status status);
void PageImpl::Put(fidl::Array<uint8_t> key,
                   fidl::Array<uint8_t> value,
                   const PutCallback& callback) {
  PutWithPriority(std::move(key), std::move(value), Priority::EAGER, callback);
}

// PutWithPriority(array<uint8> key, array<uint8> value, Priority priority)
//   => (Status status);
void PageImpl::PutWithPriority(fidl::Array<uint8_t> key,
                               fidl::Array<uint8_t> value,
                               Priority priority,
                               const PutWithPriorityCallback& callback) {
  // TODO(etiennej): Use asynchronous write, otherwise the run loop may block
  // until the pipe is drained.
  mx::datapipe_consumer data_pipe =
      mtl::WriteStringToConsumerHandle(convert::ToStringView(value));
  storage_->AddObjectFromLocal(
      std::move(data_pipe), value.size(),
      ftl::MakeCopyable([ this, key = std::move(key), priority, callback ](
          storage::Status status, storage::ObjectId object_id) {
        if (status != storage::Status::OK) {
          callback(PageUtils::ConvertStatus(status));
          return;
        }

        PutInCommit(key, object_id,
                    priority == Priority::EAGER ? storage::KeyPriority::EAGER
                                                : storage::KeyPriority::LAZY,
                    callback);
      }));
}

// PutReference(array<uint8> key, Reference? reference, Priority priority)
//   => (Status status);
void PageImpl::PutReference(fidl::Array<uint8_t> key,
                            ReferencePtr reference,
                            Priority priority,
                            const PutReferenceCallback& callback) {
  storage::ObjectIdView object_id(reference->opaque_id);
  PutInCommit(key, object_id,
              priority == Priority::EAGER ? storage::KeyPriority::EAGER
                                          : storage::KeyPriority::LAZY,
              callback);
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
void PageImpl::Delete(fidl::Array<uint8_t> key,
                      const DeleteCallback& callback) {
  RunInTransaction(
      [&key](storage::Journal* journal) {
        return PageUtils::ConvertStatus(journal->Delete(key),
                                        Status::KEY_NOT_FOUND);
      },
      callback);
}

// CreateReference(int64 size, handle<data_pipe_producer> data)
//   => (Status status, Reference reference);
void PageImpl::CreateReference(int64_t size,
                               mx::datapipe_consumer data,
                               const CreateReferenceCallback& callback) {
  storage_->AddObjectFromLocal(
      std::move(data), size,
      [callback](storage::Status status, storage::ObjectId object_id) {
        if (status != storage::Status::OK) {
          callback(PageUtils::ConvertStatus(status), nullptr);
          return;
        }

        ReferencePtr reference = Reference::New();
        reference->opaque_id = convert::ToArray(object_id);
        callback(Status::OK, std::move(reference));
      });
}

// GetReference(Reference reference) => (Status status, Value? value);
void PageImpl::GetReference(ReferencePtr reference,
                            const GetReferenceCallback& callback) {
  PageUtils::GetReferenceAsValuePtr(storage_, reference->opaque_id, callback);
}

// GetPartialReference(Reference reference, int64 offset, int64 max_size)
//   => (Status status, handle<vmo>? buffer);
void PageImpl::GetPartialReference(
    ReferencePtr reference,
    int64_t offset,
    int64_t max_size,
    const GetPartialReferenceCallback& callback) {
  PageUtils::GetPartialReferenceAsBuffer(storage_, reference->opaque_id, offset,
                                         max_size, callback);
}

// StartTransaction() => (Status status);
void PageImpl::StartTransaction(const StartTransactionCallback& callback) {
  if (journal_) {
    callback(Status::TRANSACTION_ALREADY_IN_PROGRESS);
    return;
  }
  storage::CommitId commit_id = branch_tracker_->GetBranchHeadId();
  storage::Status status = storage_->StartCommit(
      commit_id, storage::JournalType::EXPLICIT, &journal_);
  journal_parent_commit_ = commit_id;
  callback(PageUtils::ConvertStatus(status));
}

// Commit() => (Status status);
void PageImpl::Commit(const CommitCallback& callback) {
  if (!journal_) {
    callback(Status::NO_TRANSACTION_IN_PROGRESS);
    return;
  }
  journal_parent_commit_.clear();
  CommitJournal(std::move(journal_), callback);
}

// Rollback() => (Status status);
void PageImpl::Rollback(const RollbackCallback& callback) {
  if (!journal_) {
    callback(Status::NO_TRANSACTION_IN_PROGRESS);
    return;
  }
  storage::Status status = journal_->Rollback();
  journal_.reset();
  journal_parent_commit_.clear();
  callback(PageUtils::ConvertStatus(status));
}

}  // namespace ledger

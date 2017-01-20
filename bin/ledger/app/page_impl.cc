// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/page_impl.h"

#include <string>
#include <utility>
#include <vector>

#include "apps/ledger/src/app/branch_tracker.h"
#include "apps/ledger/src/app/constants.h"
#include "apps/ledger/src/app/page_manager.h"
#include "apps/ledger/src/app/page_snapshot_impl.h"
#include "apps/ledger/src/app/page_utils.h"
#include "apps/ledger/src/callback/trace_callback.h"
#include "apps/ledger/src/convert/convert.h"
#include "apps/tracing/lib/trace/event.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/socket/strings.h"

namespace ledger {

PageImpl::PageImpl(storage::PageStorage* storage,
                   PageManager* manager,
                   BranchTracker* branch_tracker)
    : storage_(storage), manager_(manager), branch_tracker_(branch_tracker) {}

PageImpl::~PageImpl() {}

// GetId() => (array<uint8> id);
void PageImpl::GetId(const GetIdCallback& callback) {
  TRACE_DURATION("page", "get_id");

  callback(convert::ToArray(storage_->GetId()));
}

const storage::CommitId& PageImpl::GetCurrentCommitId() {
  // TODO(etiennej): Commit implicit transactions when we have those.
  if (!journal_) {
    return branch_tracker_->GetBranchHeadId();
  } else {
    return journal_parent_commit_;
  }
}

// GetSnapshot(PageSnapshot& snapshot, PageWatcher& watcher) => (Status status);
void PageImpl::GetSnapshot(
    fidl::InterfaceRequest<PageSnapshot> snapshot_request,
    fidl::InterfaceHandle<PageWatcher> watcher,
    const GetSnapshotCallback& callback) {
  TRACE_DURATION("page", "get_snapshot");

  std::unique_ptr<const storage::Commit> commit;
  storage::Status status =
      storage_->GetCommitSynchronous(GetCurrentCommitId(), &commit);
  if (status != storage::Status::OK) {
    callback(PageUtils::ConvertStatus(status));
    return;
  }
  manager_->BindPageSnapshot(commit->Clone(), std::move(snapshot_request));
  if (watcher) {
    PageWatcherPtr watcher_ptr = PageWatcherPtr::Create(std::move(watcher));
    branch_tracker_->RegisterPageWatcher(std::move(watcher_ptr),
                                         std::move(commit));
  }
  callback(Status::OK);
}

void PageImpl::RunInTransaction(
    std::function<Status(storage::Journal* journal)> runnable,
    std::function<void(Status)> callback) {
  SerializeOperation(
      std::move(callback),
      [ this, runnable = std::move(runnable) ](StatusCallback callback) {
        if (journal_) {
          // A transaction is in progress; add this change to it.
          callback(runnable(journal_.get()));
          return;
        }
        // No transaction is in progress; create one just for this change.
        // TODO(etiennej): Add a change batching strategy for operations outside
        // transactions. Currently, we create a commit for every change; we
        // would like to group changes that happen "close enough" together in
        // one commit.
        branch_tracker_->StartTransaction([] {});
        storage::CommitId commit_id = branch_tracker_->GetBranchHeadId();
        std::unique_ptr<storage::Journal> journal;
        storage::Status status = storage_->StartCommit(
            commit_id, storage::JournalType::IMPLICIT, &journal);
        if (status != storage::Status::OK) {
          callback(PageUtils::ConvertStatus(status));
          journal->Rollback();
          branch_tracker_->StopTransaction("");
          return;
        }
        Status ledger_status = runnable(journal.get());
        if (ledger_status != Status::OK) {
          callback(ledger_status);
          journal->Rollback();
          branch_tracker_->StopTransaction("");
          return;
        }

        CommitJournal(std::move(journal), [
          this, callback = std::move(callback)
        ](Status status, storage::CommitId commit_id) {
          branch_tracker_->StopTransaction(status == Status::OK ? commit_id
                                                                : "");
          callback(status);
        });
      });
}

void PageImpl::CommitJournal(
    std::unique_ptr<storage::Journal> journal,
    std::function<void(Status, storage::CommitId)> callback) {
  storage::Journal* journal_ptr = journal.get();
  in_progress_journals_.push_back(std::move(journal));

  journal_ptr->Commit([this, callback, journal_ptr](
      storage::Status status, storage::CommitId commit_id) {
    in_progress_journals_.erase(std::remove_if(
        in_progress_journals_.begin(), in_progress_journals_.end(),
        [&journal_ptr](const std::unique_ptr<storage::Journal>& journal) {
          return journal_ptr == journal.get();
        }));
    callback(PageUtils::ConvertStatus(status), std::move(commit_id));
  });
}

void PageImpl::SerializeOperation(
    StatusCallback callback,
    std::function<void(StatusCallback)> operation) {
  auto closure = [
    this, callback = std::move(callback), operation = std::move(operation)
  ] {
    operation([ this, callback = std::move(callback) ](Status status) {
      callback(status);
      this->queued_operations_.pop();
      if (!this->queued_operations_.empty()) {
        queued_operations_.front()();
      }
    });
  };
  queued_operations_.emplace(std::move(closure));
  if (queued_operations_.size() == 1) {
    queued_operations_.front()();
  }
}

// Put(array<uint8> key, array<uint8> value) => (Status status);
void PageImpl::Put(fidl::Array<uint8_t> key,
                   fidl::Array<uint8_t> value,
                   const PutCallback& callback) {
  PutWithPriority(std::move(key), std::move(value), Priority::EAGER,
                  std::move(callback));
}

// PutWithPriority(array<uint8> key, array<uint8> value, Priority priority)
//   => (Status status);
void PageImpl::PutWithPriority(fidl::Array<uint8_t> key,
                               fidl::Array<uint8_t> value,
                               Priority priority,
                               const PutWithPriorityCallback& callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "page", "put_with_priority");

  // TODO(etiennej): Use asynchronous write, otherwise the run loop may block
  // until the socket is drained.
  mx::socket socket = mtl::WriteStringToSocket(convert::ToStringView(value));
  storage_->AddObjectFromLocal(
      std::move(socket), value.size(), ftl::MakeCopyable([
        this, key = std::move(key), priority,
        callback = std::move(timed_callback)
      ](storage::Status status, storage::ObjectId object_id) mutable {
        if (status != storage::Status::OK) {
          callback(PageUtils::ConvertStatus(status));
          return;
        }

        PutInCommit(std::move(key), std::move(object_id),
                    priority == Priority::EAGER ? storage::KeyPriority::EAGER
                                                : storage::KeyPriority::LAZY,
                    std::move(callback));
      }));
}

// PutReference(array<uint8> key, Reference? reference, Priority priority)
//   => (Status status);
void PageImpl::PutReference(fidl::Array<uint8_t> key,
                            ReferencePtr reference,
                            Priority priority,
                            const PutReferenceCallback& callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "page", "put_reference");

  storage::ObjectIdView object_id(reference->opaque_id);
  storage_->GetObject(
      object_id, ftl::MakeCopyable([
        this, key = std::move(key), object_id = object_id.ToString(), priority,
        timed_callback
      ](storage::Status status,
                 std::unique_ptr<const storage::Object> object) mutable {
        if (status != storage::Status::OK) {
          timed_callback(
              PageUtils::ConvertStatus(status, Status::REFERENCE_NOT_FOUND));
          return;
        }
        PutInCommit(std::move(key), std::move(object_id),
                    priority == Priority::EAGER ? storage::KeyPriority::EAGER
                                                : storage::KeyPriority::LAZY,
                    std::move(timed_callback));
      }));
}

void PageImpl::PutInCommit(fidl::Array<uint8_t> key,
                           storage::ObjectId object_id,
                           storage::KeyPriority priority,
                           std::function<void(Status)> callback) {
  RunInTransaction(
      ftl::MakeCopyable([
        key = std::move(key), object_id = std::move(object_id), priority
      ](storage::Journal * journal) mutable {
        return PageUtils::ConvertStatus(
            journal->Put(std::move(key), std::move(object_id), priority));
      }),
      std::move(callback));
}

// Delete(array<uint8> key) => (Status status);
void PageImpl::Delete(fidl::Array<uint8_t> key,
                      const DeleteCallback& callback) {
  RunInTransaction(ftl::MakeCopyable([key = std::move(key)](storage::Journal *
                                                            journal) mutable {
                     return PageUtils::ConvertStatus(
                         journal->Delete(std::move(key)),
                         Status::KEY_NOT_FOUND);
                   }),
                   TRACE_CALLBACK(std::move(callback), "page", "delete"));
}

// CreateReference(int64 size, handle<socket> data)
//   => (Status status, Reference reference);
void PageImpl::CreateReference(int64_t size,
                               mx::socket data,
                               const CreateReferenceCallback& callback) {
  storage_->AddObjectFromLocal(
      std::move(data), size,
      [callback =
           TRACE_CALLBACK(std::move(callback), "page", "create_reference")](
          storage::Status status, storage::ObjectId object_id) {
        if (status != storage::Status::OK) {
          callback(PageUtils::ConvertStatus(status), nullptr);
          return;
        }

        ReferencePtr reference = Reference::New();
        reference->opaque_id = convert::ToArray(object_id);
        callback(Status::OK, std::move(reference));
      });
}

// StartTransaction() => (Status status);
void PageImpl::StartTransaction(const StartTransactionCallback& callback) {
  SerializeOperation(
      TRACE_CALLBACK(std::move(callback), "page", "start_transaction"),
      [this](StatusCallback callback) {
        if (journal_) {
          callback(Status::TRANSACTION_ALREADY_IN_PROGRESS);
          return;
        }
        storage::CommitId commit_id = branch_tracker_->GetBranchHeadId();
        storage::Status status = storage_->StartCommit(
            commit_id, storage::JournalType::EXPLICIT, &journal_);
        if (status != storage::Status::OK) {
          callback(PageUtils::ConvertStatus(status));
          return;
        }
        journal_parent_commit_ = commit_id;
        branch_tracker_->StartTransaction([callback = std::move(callback)]() {
          callback(Status::OK);
        });
      });
}

// Commit() => (Status status);
void PageImpl::Commit(const CommitCallback& callback) {
  SerializeOperation(TRACE_CALLBACK(std::move(callback), "page", "commit"),
                     [this](StatusCallback callback) {
                       if (!journal_) {
                         callback(Status::NO_TRANSACTION_IN_PROGRESS);
                         return;
                       }
                       journal_parent_commit_.clear();
                       CommitJournal(std::move(journal_), [
                         this, callback = std::move(callback)
                       ](Status status, storage::CommitId commit_id) {
                         branch_tracker_->StopTransaction(commit_id);
                         callback(status);
                       });
                     });
}

// Rollback() => (Status status);
void PageImpl::Rollback(const RollbackCallback& callback) {
  SerializeOperation(TRACE_CALLBACK(std::move(callback), "page", "rollback"),
                     [this](StatusCallback callback) {
                       if (!journal_) {
                         callback(Status::NO_TRANSACTION_IN_PROGRESS);
                         return;
                       }
                       storage::Status status = journal_->Rollback();
                       journal_.reset();
                       journal_parent_commit_.clear();
                       callback(PageUtils::ConvertStatus(status));
                       branch_tracker_->StopTransaction("");
                     });
}

}  // namespace ledger

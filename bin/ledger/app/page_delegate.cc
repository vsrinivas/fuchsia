// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/page_delegate.h"

#include <string>
#include <utility>
#include <vector>

#include <lib/callback/scoped_callback.h>
#include <lib/callback/waiter.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>
#include <lib/fsl/socket/strings.h>
#include <lib/fxl/memory/ref_ptr.h>
#include <trace/event.h>

#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/app/page_manager.h"
#include "peridot/bin/ledger/app/page_snapshot_impl.h"
#include "peridot/bin/ledger/app/page_utils.h"
#include "peridot/lib/convert/convert.h"

namespace ledger {

PageDelegate::PageDelegate(coroutine::CoroutineService* coroutine_service,
                           PageManager* manager, storage::PageStorage* storage,
                           MergeResolver* merge_resolver,
                           fidl::InterfaceRequest<Page> request,
                           SyncWatcherSet* watchers)
    : manager_(manager),
      storage_(storage),
      merge_resolver_(merge_resolver),
      request_(std::move(request)),
      interface_(this),
      branch_tracker_(coroutine_service, manager, storage),
      watcher_set_(watchers),
      weak_factory_(this) {
  interface_.set_on_empty([this] {
    operation_serializer_.Serialize<Status>(
        [](Status status) {},
        [this](fit::function<void(Status)> callback) {
          branch_tracker_.StopTransaction(nullptr);
          callback(Status::OK);
        });
  });
  branch_tracker_.set_on_empty([this] { CheckEmpty(); });
  operation_serializer_.set_on_empty([this] { CheckEmpty(); });
}

PageDelegate::~PageDelegate() {}

void PageDelegate::Init(fit::function<void(Status)> on_done) {
  branch_tracker_.Init([this, on_done = std::move(on_done)](Status status) {
    if (status != Status::OK) {
      on_done(status);
      return;
    }
    interface_.Bind(std::move(request_));
    on_done(Status::OK);
  });
}

void PageDelegate::GetId(Page::GetIdCallback callback) {
  ledger::PageId page_id;
  convert::ToArray(storage_->GetId(), &page_id.id);
  callback(page_id);
}

void PageDelegate::GetSnapshot(
    fidl::InterfaceRequest<PageSnapshot> snapshot_request,
    fidl::VectorPtr<uint8_t> key_prefix,
    fidl::InterfaceHandle<PageWatcher> watcher,
    Page::GetSnapshotCallback callback) {
  // TODO(qsr): Update this so that only |GetCurrentCommitId| is done in a the
  // operation serializer.
  operation_serializer_.Serialize<Status>(
      std::move(callback),
      [this, snapshot_request = std::move(snapshot_request),
       key_prefix = std::move(key_prefix), watcher = std::move(watcher)](
          Page::GetSnapshotCallback callback) mutable {
        storage_->GetCommit(
            GetCurrentCommitId(),
            callback::MakeScoped(
                weak_factory_.GetWeakPtr(),
                [this, snapshot_request = std::move(snapshot_request),
                 key_prefix = std::move(key_prefix),
                 watcher = std::move(watcher), callback = std::move(callback)](
                    storage::Status status,
                    std::unique_ptr<const storage::Commit> commit) mutable {
                  if (status != storage::Status::OK) {
                    callback(PageUtils::ConvertStatus(status));
                    return;
                  }
                  std::string prefix = convert::ToString(key_prefix);
                  if (watcher) {
                    PageWatcherPtr watcher_ptr = watcher.Bind();
                    branch_tracker_.RegisterPageWatcher(
                        std::move(watcher_ptr), commit->Clone(), prefix);
                  }
                  manager_->BindPageSnapshot(std::move(commit),
                                             std::move(snapshot_request),
                                             std::move(prefix));
                  callback(Status::OK);
                }));
      });
}

void PageDelegate::Put(fidl::VectorPtr<uint8_t> key,
                       fidl::VectorPtr<uint8_t> value,
                       Page::PutCallback callback) {
  PutWithPriority(std::move(key), std::move(value), Priority::EAGER,
                  std::move(callback));
}

void PageDelegate::PutWithPriority(fidl::VectorPtr<uint8_t> key,
                                   fidl::VectorPtr<uint8_t> value,
                                   Priority priority,
                                   Page::PutWithPriorityCallback callback) {
  FXL_DCHECK(key->size() <= kMaxKeySize);
  auto promise = fxl::MakeRefCounted<
      callback::Promise<storage::Status, storage::ObjectIdentifier>>(
      storage::Status::ILLEGAL_STATE);
  storage_->AddObjectFromLocal(storage::DataSource::Create(std::move(value)),
                               promise->NewCallback());

  operation_serializer_.Serialize<Status>(
      std::move(callback),
      [this, promise = std::move(promise), key = std::move(key),
       priority](Page::PutWithPriorityCallback callback) mutable {
        promise->Finalize(callback::MakeScoped(
            weak_factory_.GetWeakPtr(),
            [this, key = std::move(key), priority,
             callback = std::move(callback)](
                storage::Status status,
                storage::ObjectIdentifier object_identifier) mutable {
              if (status != storage::Status::OK) {
                callback(PageUtils::ConvertStatus(status));
                return;
              }

              PutInCommit(std::move(key), std::move(object_identifier),
                          priority == Priority::EAGER
                              ? storage::KeyPriority::EAGER
                              : storage::KeyPriority::LAZY,
                          std::move(callback));
            }));
      });
}

void PageDelegate::PutReference(fidl::VectorPtr<uint8_t> key,
                                Reference reference, Priority priority,
                                Page::PutReferenceCallback callback) {
  FXL_DCHECK(key->size() <= kMaxKeySize);
  storage::ObjectIdentifier object_identifier;
  Status status =
      manager_->ResolveReference(std::move(reference), &object_identifier);
  if (status != Status::OK) {
    callback(status);
    return;
  }

  auto promise = fxl::MakeRefCounted<callback::Promise<
      storage::Status, std::unique_ptr<const storage::Object>>>(
      storage::Status::ILLEGAL_STATE);

  storage_->GetObject(object_identifier, storage::PageStorage::Location::LOCAL,
                      promise->NewCallback());

  operation_serializer_.Serialize<Status>(
      std::move(callback),
      [this, promise = std::move(promise), key = std::move(key),
       object_identifier = std::move(object_identifier),
       priority](Page::PutReferenceCallback callback) mutable {
        promise->Finalize(callback::MakeScoped(
            weak_factory_.GetWeakPtr(),
            [this, key = std::move(key),
             object_identifier = std::move(object_identifier), priority,
             callback = std::move(callback)](
                storage::Status status,
                std::unique_ptr<const storage::Object> object) mutable {
              if (status != storage::Status::OK) {
                callback(PageUtils::ConvertStatus(status,
                                                  Status::REFERENCE_NOT_FOUND));
                return;
              }
              PutInCommit(std::move(key), std::move(object_identifier),
                          priority == Priority::EAGER
                              ? storage::KeyPriority::EAGER
                              : storage::KeyPriority::LAZY,
                          std::move(callback));
            }));
      });
}

void PageDelegate::Delete(fidl::VectorPtr<uint8_t> key,
                          Page::DeleteCallback callback) {
  operation_serializer_.Serialize<Status>(
      std::move(callback),
      [this, key = std::move(key)](Page::DeleteCallback callback) mutable {
        RunInTransaction(
            [key = std::move(key)](storage::Journal* journal,
                                   fit::function<void(Status)> callback) {
              journal->Delete(key, [callback = std::move(callback)](
                                       storage::Status status) {
                callback(
                    PageUtils::ConvertStatus(status, Status::KEY_NOT_FOUND));
              });
            },
            std::move(callback));
      });
}

void PageDelegate::CreateReference(
    std::unique_ptr<storage::DataSource> data,
    fit::function<void(Status, ReferencePtr)> callback) {
  storage_->AddObjectFromLocal(
      std::move(data),
      callback::MakeScoped(
          weak_factory_.GetWeakPtr(),
          [this, callback = std::move(callback)](
              storage::Status status,
              storage::ObjectIdentifier object_identifier) {
            if (status != storage::Status::OK) {
              callback(PageUtils::ConvertStatus(status), nullptr);
              return;
            }

            callback(Status::OK, fidl::MakeOptional(manager_->CreateReference(
                                     std::move(object_identifier))));
          }));
}

void PageDelegate::StartTransaction(Page::StartTransactionCallback callback) {
  operation_serializer_.Serialize<Status>(
      std::move(callback), [this](StatusCallback callback) {
        if (journal_) {
          callback(Status::TRANSACTION_ALREADY_IN_PROGRESS);
          return;
        }
        storage::CommitId commit_id = branch_tracker_.GetBranchHeadId();
        storage_->StartCommit(
            commit_id, storage::JournalType::EXPLICIT,
            callback::MakeScoped(
                weak_factory_.GetWeakPtr(),
                [this, commit_id, callback = std::move(callback)](
                    storage::Status status,
                    std::unique_ptr<storage::Journal> journal) mutable {
                  journal_ = std::move(journal);
                  if (status != storage::Status::OK) {
                    callback(PageUtils::ConvertStatus(status));
                    return;
                  }
                  journal_parent_commit_ = commit_id;

                  branch_tracker_.StartTransaction(
                      [callback = std::move(callback)]() {
                        callback(Status::OK);
                      });
                }));
      });
}

void PageDelegate::Commit(Page::CommitCallback callback) {
  operation_serializer_.Serialize<Status>(
      std::move(callback), [this](StatusCallback callback) {
        if (!journal_) {
          callback(Status::NO_TRANSACTION_IN_PROGRESS);
          return;
        }
        journal_parent_commit_.clear();
        CommitJournal(std::move(journal_),
                      callback::MakeScoped(
                          weak_factory_.GetWeakPtr(),
                          [this, callback = std::move(callback)](
                              Status status,
                              std::unique_ptr<const storage::Commit> commit) {
                            branch_tracker_.StopTransaction(std::move(commit));
                            callback(status);
                          }));
      });
}

void PageDelegate::Rollback(Page::RollbackCallback callback) {
  operation_serializer_.Serialize<Status>(
      std::move(callback), [this](StatusCallback callback) {
        if (!journal_) {
          callback(Status::NO_TRANSACTION_IN_PROGRESS);
          return;
        }
        storage_->RollbackJournal(
            std::move(journal_),
            callback::MakeScoped(
                weak_factory_.GetWeakPtr(),
                [this, callback = std::move(callback)](storage::Status status) {
                  journal_.reset();
                  journal_parent_commit_.clear();
                  callback(PageUtils::ConvertStatus(status));
                  branch_tracker_.StopTransaction(nullptr);
                }));
      });
}

void PageDelegate::SetSyncStateWatcher(
    fidl::InterfaceHandle<SyncWatcher> watcher,
    Page::SetSyncStateWatcherCallback callback) {
  SyncWatcherPtr watcher_ptr = watcher.Bind();
  watcher_set_->AddSyncWatcher(std::move(watcher_ptr));
  callback(Status::OK);
}

void PageDelegate::WaitForConflictResolution(
    Page::WaitForConflictResolutionCallback callback) {
  if (!merge_resolver_->HasUnfinishedMerges()) {
    callback(ConflictResolutionWaitStatus::NO_CONFLICTS);
    return;
  }
  merge_resolver_->RegisterNoConflictCallback(std::move(callback));
}

const storage::CommitId& PageDelegate::GetCurrentCommitId() {
  // TODO(etiennej): Commit implicit transactions when we have those.
  if (!journal_) {
    return branch_tracker_.GetBranchHeadId();
  }
  return journal_parent_commit_;
}

void PageDelegate::PutInCommit(fidl::VectorPtr<uint8_t> key,
                               storage::ObjectIdentifier object_identifier,
                               storage::KeyPriority priority,
                               fit::function<void(Status)> callback) {
  RunInTransaction(
      [key = std::move(key), object_identifier = std::move(object_identifier),
       priority](storage::Journal* journal,
                 fit::function<void(Status status)> callback) mutable {
        journal->Put(key, std::move(object_identifier), priority,
                     [callback = std::move(callback)](storage::Status status) {
                       callback(PageUtils::ConvertStatus(status));
                     });
      },
      std::move(callback));
}

void PageDelegate::RunInTransaction(
    fit::function<void(storage::Journal*, fit::function<void(Status)>)>
        runnable,
    fit::function<void(Status)> callback) {
  if (journal_) {
    // A transaction is in progress; add this change to it.
    runnable(journal_.get(), std::move(callback));
    return;
  }
  // No transaction is in progress; create one just for this change.
  // TODO(etiennej): Add a change batching strategy for operations outside
  // transactions. Currently, we create a commit for every change; we
  // would like to group changes that happen "close enough" together in
  // one commit.
  branch_tracker_.StartTransaction([] {});
  storage::CommitId commit_id = branch_tracker_.GetBranchHeadId();
  std::unique_ptr<storage::Journal> journal;
  storage_->StartCommit(
      commit_id, storage::JournalType::IMPLICIT,
      callback::MakeScoped(
          weak_factory_.GetWeakPtr(),
          [this, runnable = std::move(runnable),
           callback = std::move(callback)](
              storage::Status status,
              std::unique_ptr<storage::Journal> journal) mutable {
            if (status != storage::Status::OK) {
              callback(PageUtils::ConvertStatus(status));
              branch_tracker_.StopTransaction(nullptr);
              return;
            }
            runnable(
                journal.get(),
                callback::MakeScoped(
                    weak_factory_.GetWeakPtr(),
                    [this, journal = std::move(journal),
                     callback =
                         std::move(callback)](Status ledger_status) mutable {
                      if (ledger_status != Status::OK) {
                        callback(ledger_status);
                        storage_->RollbackJournal(
                            std::move(journal),
                            [](storage::Status /*rollback_status*/) {});
                        branch_tracker_.StopTransaction(nullptr);
                        return;
                      }

                      CommitJournal(
                          std::move(journal),
                          callback::MakeScoped(
                              weak_factory_.GetWeakPtr(),
                              [this, callback = std::move(callback)](
                                  Status status,
                                  std::unique_ptr<const storage::Commit>
                                      commit) {
                                branch_tracker_.StopTransaction(
                                    status == Status::OK ? std::move(commit)
                                                         : nullptr);
                                callback(status);
                              }));
                    }));
          }));
}

void PageDelegate::CommitJournal(
    std::unique_ptr<storage::Journal> journal,
    fit::function<void(Status, std::unique_ptr<const storage::Commit>)>
        callback) {
  storage_->CommitJournal(
      std::move(journal), [callback = std::move(callback)](
                              storage::Status status,
                              std::unique_ptr<const storage::Commit> commit) {
        callback(PageUtils::ConvertStatus(status), std::move(commit));
      });
}

void PageDelegate::CheckEmpty() {
  if (on_empty_callback_ && !interface_.is_bound() &&
      branch_tracker_.IsEmpty() && operation_serializer_.empty()) {
    on_empty_callback_();
  }
}

}  // namespace ledger

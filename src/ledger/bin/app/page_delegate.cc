// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_delegate.h"

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

#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/page_manager.h"
#include "src/ledger/bin/app/page_snapshot_impl.h"
#include "src/ledger/bin/app/page_utils.h"
#include "src/ledger/bin/fidl/include/types.h"

namespace ledger {

PageDelegate::PageDelegate(coroutine::CoroutineService* coroutine_service,
                           PageManager* manager, storage::PageStorage* storage,
                           MergeResolver* merge_resolver,
                           SyncWatcherSet* watchers,
                           std::unique_ptr<PageImpl> page_impl)
    : manager_(manager),
      storage_(storage),
      merge_resolver_(merge_resolver),
      branch_tracker_(coroutine_service, manager, storage),
      watcher_set_(watchers),
      page_impl_(std::move(page_impl)),
      weak_factory_(this) {
  page_impl_->set_on_binding_unbound([this] {
    operation_serializer_.Serialize<storage::Status>(
        [](storage::Status status) {},
        [this](fit::function<void(storage::Status)> callback) {
          branch_tracker_.StopTransaction(nullptr);
          callback(storage::Status::OK);
        });
  });
  branch_tracker_.set_on_empty([this] { CheckEmpty(); });
  operation_serializer_.set_on_empty([this] { CheckEmpty(); });
}

PageDelegate::~PageDelegate() {}

void PageDelegate::Init(fit::function<void(storage::Status)> on_done) {
  storage::Status status = branch_tracker_.Init();
  if (status != storage::Status::OK) {
    on_done(status);
    return;
  }
  page_impl_->SetPageDelegate(this);
  on_done(storage::Status::OK);
}

void PageDelegate::GetSnapshot(
    fidl::InterfaceRequest<PageSnapshot> snapshot_request,
    std::vector<uint8_t> key_prefix, fidl::InterfaceHandle<PageWatcher> watcher,
    fit::function<void(Status)> callback) {
  // TODO(qsr): Update this so that only |GetCurrentCommitId| is done in a the
  // operation serializer.
  operation_serializer_.Serialize<storage::Status>(
      PageUtils::AdaptStatusCallback(std::move(callback)),
      [this, snapshot_request = std::move(snapshot_request),
       key_prefix = std::move(key_prefix), watcher = std::move(watcher)](
          fit::function<void(storage::Status)> callback) mutable {
        std::unique_ptr<const storage::Commit> commit =
            branch_tracker_.GetBranchHead();
        std::string prefix = convert::ToString(key_prefix);
        if (watcher) {
          PageWatcherPtr watcher_ptr = watcher.Bind();
          branch_tracker_.RegisterPageWatcher(std::move(watcher_ptr),
                                              commit->Clone(), prefix);
        }
        manager_->BindPageSnapshot(
            std::move(commit), std::move(snapshot_request), std::move(prefix));
        callback(storage::Status::OK);
      });
}

void PageDelegate::Put(std::vector<uint8_t> key, std::vector<uint8_t> value,
                       fit::function<void(Status)> callback) {
  PutWithPriority(std::move(key), std::move(value), Priority::EAGER,
                  std::move(callback));
}

void PageDelegate::PutWithPriority(std::vector<uint8_t> key,
                                   std::vector<uint8_t> value,
                                   Priority priority,
                                   fit::function<void(Status)> callback) {
  FXL_DCHECK(key.size() <= kMaxKeySize);
  auto promise = fxl::MakeRefCounted<
      callback::Promise<storage::Status, storage::ObjectIdentifier>>(
      storage::Status::ILLEGAL_STATE);
  storage_->AddObjectFromLocal(storage::ObjectType::BLOB,
                               storage::DataSource::Create(std::move(value)),
                               {}, promise->NewCallback());

  operation_serializer_.Serialize<storage::Status>(
      PageUtils::AdaptStatusCallback(std::move(callback)),
      [this, promise = std::move(promise), key = std::move(key),
       priority](fit::function<void(storage::Status)> callback) mutable {
        promise->Finalize(callback::MakeScoped(
            weak_factory_.GetWeakPtr(),
            [this, key = std::move(key), priority,
             callback = std::move(callback)](
                storage::Status status,
                storage::ObjectIdentifier object_identifier) mutable {
              if (status != storage::Status::OK) {
                callback(status);
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

void PageDelegate::PutReference(std::vector<uint8_t> key, Reference reference,
                                Priority priority,
                                fit::function<void(Status)> callback) {
  FXL_DCHECK(key.size() <= kMaxKeySize);
  // |ResolveReference| also makes sure that the reference was created for this
  // page.
  storage::ObjectIdentifier object_identifier;
  storage::Status status =
      manager_->ResolveReference(std::move(reference), &object_identifier);
  if (status != storage::Status::OK) {
    callback(PageUtils::ConvertStatus(status));
    return;
  }

  operation_serializer_.Serialize<storage::Status>(
      PageUtils::AdaptStatusCallback(std::move(callback)),
      [this, key = std::move(key),
       object_identifier = std::move(object_identifier),
       priority](fit::function<void(storage::Status)> callback) mutable {
        PutInCommit(std::move(key), std::move(object_identifier),
                    priority == Priority::EAGER ? storage::KeyPriority::EAGER
                                                : storage::KeyPriority::LAZY,
                    std::move(callback));
      });
}

void PageDelegate::Delete(std::vector<uint8_t> key,
                          fit::function<void(Status)> callback) {
  operation_serializer_.Serialize<storage::Status>(
      PageUtils::AdaptStatusCallback(std::move(callback)),
      [this, key = std::move(key)](
          fit::function<void(storage::Status)> callback) mutable {
        RunInTransaction(
            [key = std::move(key)](storage::Journal* journal) {
              journal->Delete(key);
            },
            std::move(callback));
      });
}

void PageDelegate::Clear(fit::function<void(Status)> callback) {
  operation_serializer_.Serialize<storage::Status>(
      PageUtils::AdaptStatusCallback(std::move(callback)),
      [this](fit::function<void(storage::Status)> callback) mutable {
        RunInTransaction([](storage::Journal* journal) { journal->Clear(); },
                         std::move(callback));
      });
}

void PageDelegate::CreateReference(
    std::unique_ptr<storage::DataSource> data,
    fit::function<void(Status, CreateReferenceStatus, ReferencePtr)> callback) {
  storage_->AddObjectFromLocal(
      storage::ObjectType::BLOB, std::move(data), {},
      callback::MakeScoped(
          weak_factory_.GetWeakPtr(),
          [this, callback = std::move(callback)](
              storage::Status status,
              storage::ObjectIdentifier object_identifier) {
            if (status != storage::Status::OK &&
                status != storage::Status::IO_ERROR) {
              callback(PageUtils::ConvertStatus(status),
                       CreateReferenceStatus::OK, nullptr);
              return;
            }

            // Convert IO_ERROR into INVALID_ARGUMENT.
            // TODO(qsr): Refactor status handling so that io error due to
            // storage and io error due to invalid argument can be
            // distinguished.
            // An INVALID_ARGUMENT should not cause the page to get
            // disconnected, so use OK as status.
            if (status == storage::Status::IO_ERROR) {
              callback(Status::OK, CreateReferenceStatus::INVALID_ARGUMENT,
                       nullptr);
              return;
            }

            callback(Status::OK, CreateReferenceStatus::OK,
                     fidl::MakeOptional(manager_->CreateReference(
                         std::move(object_identifier))));
          }));
}

void PageDelegate::StartTransaction(fit::function<void(Status)> callback) {
  operation_serializer_.Serialize<Status>(
      std::move(callback),
      [this](fit::function<void(ledger::Status)> callback) {
        if (journal_) {
          callback(Status::TRANSACTION_ALREADY_IN_PROGRESS);
          return;
        }
        std::unique_ptr<const storage::Commit> commit =
            branch_tracker_.GetBranchHead();
        journal_ = storage_->StartCommit(std::move(commit));

        branch_tracker_.StartTransaction(
            [callback = std::move(callback)]() { callback(Status::OK); });
      });
}

void PageDelegate::Commit(fit::function<void(Status)> callback) {
  operation_serializer_.Serialize<Status>(
      std::move(callback),
      [this](fit::function<void(ledger::Status)> callback) {
        if (!journal_) {
          callback(Status::NO_TRANSACTION_IN_PROGRESS);
          return;
        }
        CommitJournal(std::move(journal_),
                      callback::MakeScoped(
                          weak_factory_.GetWeakPtr(),
                          [this, callback = std::move(callback)](
                              storage::Status status,
                              std::unique_ptr<const storage::Commit> commit) {
                            branch_tracker_.StopTransaction(std::move(commit));
                            callback(PageUtils::ConvertStatus(status));
                          }));
      });
}

void PageDelegate::Rollback(fit::function<void(Status)> callback) {
  operation_serializer_.Serialize<Status>(
      std::move(callback),
      [this](fit::function<void(ledger::Status)> callback) {
        if (!journal_) {
          callback(Status::NO_TRANSACTION_IN_PROGRESS);
          return;
        }
        journal_.reset();
        callback(Status::OK);
        branch_tracker_.StopTransaction(nullptr);
      });
}

void PageDelegate::SetSyncStateWatcher(
    fidl::InterfaceHandle<SyncWatcher> watcher,
    fit::function<void(Status)> callback) {
  SyncWatcherPtr watcher_ptr = watcher.Bind();
  watcher_set_->AddSyncWatcher(std::move(watcher_ptr));
  callback(Status::OK);
}

void PageDelegate::WaitForConflictResolution(
    fit::function<void(Status, ConflictResolutionWaitStatus)> callback) {
  if (!merge_resolver_->HasUnfinishedMerges()) {
    callback(Status::OK, ConflictResolutionWaitStatus::NO_CONFLICTS);
    return;
  }
  merge_resolver_->RegisterNoConflictCallback(
      [callback = std::move(callback)](ConflictResolutionWaitStatus status) {
        callback(Status::OK, status);
      });
}

void PageDelegate::PutInCommit(std::vector<uint8_t> key,
                               storage::ObjectIdentifier object_identifier,
                               storage::KeyPriority priority,
                               fit::function<void(storage::Status)> callback) {
  RunInTransaction(
      [key = std::move(key), object_identifier = std::move(object_identifier),
       priority](storage::Journal* journal) mutable {
        journal->Put(key, std::move(object_identifier), priority);
      },
      std::move(callback));
}

void PageDelegate::RunInTransaction(
    fit::function<void(storage::Journal*)> runnable,
    fit::function<void(storage::Status)> callback) {
  if (journal_) {
    // A transaction is in progress; add this change to it.
    runnable(journal_.get());
    callback(storage::Status::OK);
    return;
  }
  // No transaction is in progress; create one just for this change.
  // TODO(LE-690): Batch together operations outside transactions that have been
  // accumulated while waiting for the previous one to be committed.
  branch_tracker_.StartTransaction([] {});
  std::unique_ptr<const storage::Commit> commit =
      branch_tracker_.GetBranchHead();
  std::unique_ptr<storage::Journal> journal =
      storage_->StartCommit(std::move(commit));
  runnable(journal.get());

  CommitJournal(
      std::move(journal),
      callback::MakeScoped(
          weak_factory_.GetWeakPtr(),
          [this, callback = std::move(callback)](
              storage::Status status,
              std::unique_ptr<const storage::Commit> commit) {
            branch_tracker_.StopTransaction(
                status == storage::Status::OK ? std::move(commit) : nullptr);
            callback(status);
          }));
}

void PageDelegate::CommitJournal(
    std::unique_ptr<storage::Journal> journal,
    fit::function<void(storage::Status, std::unique_ptr<const storage::Commit>)>
        callback) {
  storage_->CommitJournal(std::move(journal),
                          [callback = std::move(callback)](
                              storage::Status status,
                              std::unique_ptr<const storage::Commit> commit) {
                            callback(status, std::move(commit));
                          });
}

void PageDelegate::CheckEmpty() {
  if (on_empty_callback_ && page_impl_->IsEmpty() &&
      branch_tracker_.IsEmpty() && operation_serializer_.empty()) {
    on_empty_callback_();
  }
}

}  // namespace ledger

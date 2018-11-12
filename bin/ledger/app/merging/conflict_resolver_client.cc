// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/merging/conflict_resolver_client.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <lib/callback/waiter.h>
#include <lib/fit/function.h>
#include <lib/fsl/socket/strings.h>
#include <lib/fxl/memory/ref_ptr.h>
#include <lib/fxl/memory/weak_ptr.h>

#include "peridot/bin/ledger/app/diff_utils.h"
#include "peridot/bin/ledger/app/fidl/serialization_size.h"
#include "peridot/bin/ledger/app/page_manager.h"
#include "peridot/bin/ledger/app/page_utils.h"
#include "peridot/lib/util/ptr.h"

namespace ledger {

ConflictResolverClient::ConflictResolverClient(
    storage::PageStorage* storage, PageManager* page_manager,
    ConflictResolver* conflict_resolver,
    std::unique_ptr<const storage::Commit> left,
    std::unique_ptr<const storage::Commit> right,
    std::unique_ptr<const storage::Commit> ancestor,
    fit::function<void(Status)> callback)
    : storage_(storage),
      manager_(page_manager),
      conflict_resolver_(conflict_resolver),
      left_(std::move(left)),
      right_(std::move(right)),
      ancestor_(std::move(ancestor)),
      callback_(std::move(callback)),
      merge_result_provider_binding_(this),
      weak_factory_(this) {
  FXL_DCHECK(left_->GetTimestamp() >= right_->GetTimestamp());
  FXL_DCHECK(callback_);
}

ConflictResolverClient::~ConflictResolverClient() {
  if (journal_) {
    storage_->RollbackJournal(std::move(journal_),
                              [](storage::Status /*status*/) {});
  }
}

void ConflictResolverClient::Start() {
  // Prepare the journal for the merge commit.
  storage_->StartMergeCommit(
      left_->GetId(), right_->GetId(),
      callback::MakeScoped(
          weak_factory_.GetWeakPtr(),
          [this](storage::Status status,
                 std::unique_ptr<storage::Journal> journal) {
            if (cancelled_) {
              Finalize(Status::INTERNAL_ERROR);
              return;
            }
            journal_ = std::move(journal);
            if (status != storage::Status::OK) {
              FXL_LOG(ERROR) << "Unable to start merge commit: "
                             << fidl::ToUnderlying(status);
              Finalize(PageUtils::ConvertStatus(status));
              return;
            }

            PageSnapshotPtr page_snapshot_ancestor;
            manager_->BindPageSnapshot(ancestor_->Clone(),
                                       page_snapshot_ancestor.NewRequest(), "");

            PageSnapshotPtr page_snapshot_left;
            manager_->BindPageSnapshot(left_->Clone(),
                                       page_snapshot_left.NewRequest(), "");

            PageSnapshotPtr page_snapshot_right;
            manager_->BindPageSnapshot(right_->Clone(),
                                       page_snapshot_right.NewRequest(), "");

            in_client_request_ = true;
            conflict_resolver_->Resolve(
                std::move(page_snapshot_left), std::move(page_snapshot_right),
                std::move(page_snapshot_ancestor),
                merge_result_provider_binding_.NewBinding());
          }));
}

void ConflictResolverClient::Cancel() {
  cancelled_ = true;
  if (in_client_request_) {
    Finalize(Status::INTERNAL_ERROR);
  }
}

void ConflictResolverClient::OnNextMergeResult(
    const MergedValue& merged_value,
    const fxl::RefPtr<
        callback::Waiter<storage::Status, storage::ObjectIdentifier>>& waiter) {
  switch (merged_value.source) {
    case ValueSource::RIGHT: {
      std::string key = convert::ToString(merged_value.key);
      storage_->GetEntryFromCommit(
          *right_, key,
          [key, callback = waiter->NewCallback()](storage::Status status,
                                                  storage::Entry entry) {
            if (status != storage::Status::OK) {
              if (status == storage::Status::NOT_FOUND) {
                FXL_LOG(ERROR)
                    << "Key " << key
                    << " is not present in the right change. Unable to proceed";
              }
              callback(status, {});
              return;
            }
            callback(storage::Status::OK, entry.object_identifier);
          });
      break;
    }
    case ValueSource::NEW: {
      if (merged_value.new_value->is_bytes()) {
        storage_->AddObjectFromLocal(storage::ObjectType::BLOB,
                                     storage::DataSource::Create(std::move(
                                         merged_value.new_value->bytes())),
                                     waiter->NewCallback());
      } else {
        storage::ObjectIdentifier object_identifier;
        Status status = manager_->ResolveReference(
            std::move(merged_value.new_value->reference()), &object_identifier);
        if (status != Status::OK) {
          waiter->NewCallback()(storage::Status::NOT_FOUND, {});
          return;
        }
        waiter->NewCallback()(storage::Status::OK,
                              std::move(object_identifier));
      }
      break;
    }
    case ValueSource::DELETE: {
      journal_->Delete(merged_value.key,
                       [callback = waiter->NewCallback()](
                           storage::Status status) { callback(status, {}); });
      break;
    }
  }
}

void ConflictResolverClient::Finalize(Status status) {
  FXL_DCHECK(callback_) << "Finalize must only be called once.";
  if (journal_) {
    storage_->RollbackJournal(std::move(journal_),
                              [](storage::Status /*rollback_status*/) {});
    journal_.reset();
  }
  merge_result_provider_binding_.Close(status);
  auto callback = std::move(callback_);
  callback_ = nullptr;
  callback(status);
}

void ConflictResolverClient::GetFullDiffNew(
    std::unique_ptr<Token> token,
    fit::function<void(Status, IterationStatus, fidl::VectorPtr<DiffEntry>,
                       std::unique_ptr<Token>)>
        callback) {
  GetDiff(diff_utils::DiffType::FULL, std::move(token), std::move(callback));
}

void ConflictResolverClient::GetFullDiff(
    std::unique_ptr<Token> token,
    fit::function<void(Status, Status, fidl::VectorPtr<DiffEntry>,
                       std::unique_ptr<Token>)>
        callback) {
  GetFullDiffNew(
      std::move(token),
      [callback = std::move(callback)](
          Status status, IterationStatus diff_status,
          fidl::VectorPtr<DiffEntry> entries, std::unique_ptr<Token> token) {
        if (status != Status::OK && status != Status::PARTIAL_RESULT) {
          callback(status, status, std::move(entries), std::move(token));
          return;
        }
        callback(Status::OK,
                 diff_status == IterationStatus::OK ? Status::OK
                                                    : Status::PARTIAL_RESULT,
                 std::move(entries), std::move(token));
      });
}

void ConflictResolverClient::GetConflictingDiffNew(
    std::unique_ptr<Token> token,
    fit::function<void(Status, IterationStatus, fidl::VectorPtr<DiffEntry>,
                       std::unique_ptr<Token>)>
        callback) {
  GetDiff(diff_utils::DiffType::CONFLICTING, std::move(token),
          std::move(callback));
}

void ConflictResolverClient::GetConflictingDiff(
    std::unique_ptr<Token> token,
    fit::function<void(Status, Status, fidl::VectorPtr<DiffEntry>,
                       std::unique_ptr<Token>)>
        callback) {
  GetConflictingDiffNew(
      std::move(token),
      [callback = std::move(callback)](
          Status status, IterationStatus diff_status,
          fidl::VectorPtr<DiffEntry> entries, std::unique_ptr<Token> token) {
        if (status != Status::OK && status != Status::PARTIAL_RESULT) {
          callback(status, status, std::move(entries), std::move(token));
          return;
        }
        callback(Status::OK,
                 diff_status == IterationStatus::OK ? Status::OK
                                                    : Status::PARTIAL_RESULT,
                 std::move(entries), std::move(token));
      });
}

void ConflictResolverClient::GetDiff(
    diff_utils::DiffType type, std::unique_ptr<Token> token,
    fit::function<void(Status, IterationStatus, fidl::VectorPtr<DiffEntry>,
                       std::unique_ptr<Token>)>
        callback) {
  diff_utils::ComputeThreeWayDiff(
      storage_, *ancestor_, *left_, *right_, "",
      token ? convert::ToString(token->opaque_id) : "", type,
      callback::MakeScoped(
          weak_factory_.GetWeakPtr(),
          [this, callback = std::move(callback)](
              Status status,
              std::pair<fidl::VectorPtr<DiffEntry>, std::string> page_change) {
            if (cancelled_) {
              callback(Status::INTERNAL_ERROR, IterationStatus::OK,
                       fidl::VectorPtr<DiffEntry>::New(0), nullptr);
              Finalize(Status::INTERNAL_ERROR);
              return;
            }
            if (status != Status::OK) {
              FXL_LOG(ERROR) << "Unable to compute diff due to error "
                             << fidl::ToUnderlying(status) << ", aborting.";
              callback(status, IterationStatus::OK,
                       fidl::VectorPtr<DiffEntry>::New(0), nullptr);
              Finalize(status);
              return;
            }

            const std::string& next_token = page_change.second;
            IterationStatus diff_status = next_token.empty()
                                              ? IterationStatus::OK
                                              : IterationStatus::PARTIAL_RESULT;
            std::unique_ptr<Token> token;
            if (!next_token.empty()) {
              token = std::make_unique<Token>();
              token->opaque_id = convert::ToArray(next_token);
            }
            callback(Status::OK, diff_status, std::move(page_change.first),
                     std::move(token));
          }));
}

void ConflictResolverClient::MergeNew(
    fidl::VectorPtr<MergedValue> merged_values,
    fit::function<void(Status)> callback) {
  has_merged_values_ = true;
  operation_serializer_.Serialize<Status>(
      std::move(callback), [this, weak_this = weak_factory_.GetWeakPtr(),
                            merged_values = std::move(merged_values)](
                               fit::function<void(Status)> callback) mutable {
        if (!IsInValidStateAndNotify(weak_this, callback)) {
          return;
        }
        auto waiter = fxl::MakeRefCounted<
            callback::Waiter<storage::Status, storage::ObjectIdentifier>>(
            storage::Status::OK);
        for (const MergedValue& merged_value : *merged_values) {
          OnNextMergeResult(merged_value, waiter);
        }
        waiter->Finalize([this, weak_this,
                          merged_values = std::move(merged_values),
                          callback = std::move(callback)](
                             storage::Status status,
                             std::vector<storage::ObjectIdentifier>
                                 object_identifiers) mutable {
          if (!IsInValidStateAndNotify(weak_this, callback, status)) {
            return;
          }

          auto waiter =
              fxl::MakeRefCounted<callback::StatusWaiter<storage::Status>>(
                  storage::Status::OK);
          for (size_t i = 0; i < object_identifiers.size(); ++i) {
            // DELETE is encoded with an invalid digest in OnNextMergeResult.
            if (!object_identifiers[i].object_digest().IsValid()) {
              continue;
            }
            journal_->Put(merged_values->at(i).key, object_identifiers[i],
                          merged_values->at(i).priority == Priority::EAGER
                              ? storage::KeyPriority::EAGER
                              : storage::KeyPriority::LAZY,
                          waiter->NewCallback());
          }
          waiter->Finalize(
              [callback = std::move(callback)](storage::Status status) {
                callback(PageUtils::ConvertStatus(status));
              });
        });
      });
}

void ConflictResolverClient::Merge(
    fidl::VectorPtr<MergedValue> merged_values,
    fit::function<void(Status, Status)> callback) {
  MergeNew(std::move(merged_values),
           [callback = std::move(callback)](Status status) {
             callback(status, status);
           });
}

void ConflictResolverClient::MergeNonConflictingEntriesNew(
    fit::function<void(Status)> callback) {
  operation_serializer_.Serialize<Status>(
      std::move(callback), [this, weak_this = weak_factory_.GetWeakPtr()](
                               fit::function<void(Status)> callback) mutable {
        if (!IsInValidStateAndNotify(weak_this, callback)) {
          return;
        }
        auto waiter =
            fxl::MakeRefCounted<callback::StatusWaiter<storage::Status>>(
                storage::Status::OK);

        auto on_next = [this, weak_this,
                        waiter](storage::ThreeWayChange change) {
          if (!weak_this) {
            return false;
          }
          // When |MergeNonConflictingEntries| is called first, we know that the
          // base state of |journal_| is equal to the left version. In that
          // case, we only want to merge diffs where the change is only on the
          // right side: no change means no diff, 3 different versions means
          // conflict (so we skip), and left-only changes are already taken into
          // account.
          if (util::EqualPtr(change.base, change.left)) {
            if (change.right) {
              journal_->Put(change.right->key, change.right->object_identifier,
                            change.right->priority, waiter->NewCallback());
            } else {
              journal_->Delete(change.base->key, waiter->NewCallback());
            }
          } else if (util::EqualPtr(change.base, change.right) &&
                     has_merged_values_) {
            if (change.left) {
              journal_->Put(change.left->key, change.left->object_identifier,
                            change.left->priority, waiter->NewCallback());
            } else {
              journal_->Delete(change.base->key, waiter->NewCallback());
            }
          }
          return true;
        };
        auto on_done = [waiter, callback = std::move(callback)](
                           storage::Status status) mutable {
          if (status != storage::Status::OK) {
            callback(PageUtils::ConvertStatus(status));
            return;
          }
          waiter->Finalize(
              [callback = std::move(callback)](storage::Status status) {
                callback(PageUtils::ConvertStatus(status));
              });
        };
        storage_->GetThreeWayContentsDiff(*ancestor_, *left_, *right_, "",
                                          std::move(on_next),
                                          std::move(on_done));
      });
}

void ConflictResolverClient::MergeNonConflictingEntries(
    fit::function<void(Status, Status)> callback) {
  MergeNonConflictingEntriesNew(
      [callback = std::move(callback)](Status status) {
        callback(status, status);
      });
}

void ConflictResolverClient::DoneNew(fit::function<void(Status)> callback) {
  operation_serializer_.Serialize<Status>(
      std::move(callback), [this, weak_this = weak_factory_.GetWeakPtr()](
                               fit::function<void(Status)> callback) mutable {
        if (!IsInValidStateAndNotify(weak_this, callback)) {
          return;
        }
        in_client_request_ = false;
        FXL_DCHECK(!cancelled_);
        FXL_DCHECK(journal_);

        storage_->CommitJournal(
            std::move(journal_),
            callback::MakeScoped(
                weak_factory_.GetWeakPtr(),
                [this, weak_this, callback = std::move(callback)](
                    storage::Status status,
                    std::unique_ptr<const storage::Commit>) {
                  if (!IsInValidStateAndNotify(weak_this, callback, status)) {
                    return;
                  }
                  callback(Status::OK);
                  Finalize(Status::OK);
                }));
      });
}

void ConflictResolverClient::Done(
    fit::function<void(Status, Status)> callback) {
  DoneNew([callback = std::move(callback)](Status status) {
    callback(status, status);
  });
}

bool ConflictResolverClient::IsInValidStateAndNotify(
    const fxl::WeakPtr<ConflictResolverClient>& weak_this,
    const fit::function<void(Status)>& callback, storage::Status status) {
  if (!weak_this) {
    callback(Status::INTERNAL_ERROR);
    return false;
  }
  if (!weak_this->cancelled_ && status == storage::Status::OK) {
    return true;
  }
  Status ledger_status =
      weak_this->cancelled_
          ? Status::INTERNAL_ERROR
          // The only not found error that can occur is a key not
          // found when processing a MergedValue with
          // ValueSource::RIGHT.
          : PageUtils::ConvertStatus(status, Status::KEY_NOT_FOUND);
  // An eventual error was logged before, no need to do it again here.
  callback(ledger_status);
  // Finalize destroys this object; we need to do it after executing
  // the callback.
  weak_this->Finalize(ledger_status);
  return false;
}

}  // namespace ledger

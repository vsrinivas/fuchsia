// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/merging/conflict_resolver_client.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <lib/callback/scoped_callback.h>
#include <lib/callback/waiter.h>
#include <lib/fit/function.h>
#include <lib/fsl/socket/strings.h>
#include <src/lib/fxl/memory/ref_ptr.h>
#include <src/lib/fxl/memory/weak_ptr.h>

#include "peridot/lib/util/ptr.h"
#include "src/ledger/bin/app/diff_utils.h"
#include "src/ledger/bin/app/fidl/serialization_size.h"
#include "src/ledger/bin/app/page_manager.h"
#include "src/ledger/bin/app/page_utils.h"

namespace ledger {

ConflictResolverClient::ConflictResolverClient(
    storage::PageStorage* storage, PageManager* page_manager,
    ConflictResolver* conflict_resolver,
    std::unique_ptr<const storage::Commit> left,
    std::unique_ptr<const storage::Commit> right,
    std::unique_ptr<const storage::Commit> ancestor,
    fit::function<void(storage::Status)> callback)
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

ConflictResolverClient::~ConflictResolverClient() {}

void ConflictResolverClient::Start() {
  // Prepare the journal for the merge commit.
  journal_ = storage_->StartMergeCommit(left_->Clone(), right_->Clone());

  PageSnapshotPtr page_snapshot_ancestor;
  manager_->BindPageSnapshot(ancestor_->Clone(),
                             page_snapshot_ancestor.NewRequest(), "");

  PageSnapshotPtr page_snapshot_left;
  manager_->BindPageSnapshot(left_->Clone(), page_snapshot_left.NewRequest(),
                             "");

  PageSnapshotPtr page_snapshot_right;
  manager_->BindPageSnapshot(right_->Clone(), page_snapshot_right.NewRequest(),
                             "");

  in_client_request_ = true;
  conflict_resolver_->Resolve(std::move(page_snapshot_left),
                              std::move(page_snapshot_right),
                              std::move(page_snapshot_ancestor),
                              merge_result_provider_binding_.NewBinding());
}

void ConflictResolverClient::Cancel() {
  cancelled_ = true;
  if (in_client_request_) {
    Finalize(storage::Status::OK);
  }
}

void ConflictResolverClient::GetOrCreateObjectIdentifier(
    const MergedValue& merged_value,
    fit::function<void(storage::Status, storage::ObjectIdentifier)> callback) {
  FXL_DCHECK(merged_value.source == ValueSource::RIGHT ||
             merged_value.source == ValueSource::NEW);
  switch (merged_value.source) {
    case ValueSource::RIGHT: {
      std::string key = convert::ToString(merged_value.key);
      storage_->GetEntryFromCommit(
          *right_, key,
          [key, callback = std::move(callback)](storage::Status status,
                                                storage::Entry entry) {
            if (status != storage::Status::OK) {
              if (status == storage::Status::KEY_NOT_FOUND) {
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
                                     {}, std::move(callback));
      } else {
        storage::ObjectIdentifier object_identifier;
        storage::Status status = manager_->ResolveReference(
            std::move(merged_value.new_value->reference()), &object_identifier);
        if (status != storage::Status::OK) {
          callback(storage::Status::REFERENCE_NOT_FOUND, {});
          return;
        }
        callback(storage::Status::OK, std::move(object_identifier));
      }
      break;
    }
    default: {
      FXL_NOTREACHED();
    }
  }
}

void ConflictResolverClient::Finalize(storage::Status status) {
  FXL_DCHECK(callback_) << "Finalize must only be called once.";
  if (journal_) {
    journal_.reset();
  }
  merge_result_provider_binding_.Close(PageUtils::ConvertStatus(status));
  auto callback = std::move(callback_);
  callback_ = nullptr;
  callback(status);
}

void ConflictResolverClient::GetFullDiff(
    std::unique_ptr<Token> token,
    fit::function<void(Status, IterationStatus, std::vector<DiffEntry>,
                       std::unique_ptr<Token>)>
        callback) {
  GetDiff(diff_utils::DiffType::FULL, std::move(token),
          PageUtils::AdaptStatusCallback(std::move(callback)));
}

void ConflictResolverClient::GetConflictingDiff(
    std::unique_ptr<Token> token,
    fit::function<void(Status, IterationStatus, std::vector<DiffEntry>,
                       std::unique_ptr<Token>)>
        callback) {
  GetDiff(diff_utils::DiffType::CONFLICTING, std::move(token),
          PageUtils::AdaptStatusCallback(std::move(callback)));
}

void ConflictResolverClient::GetDiff(
    diff_utils::DiffType type, std::unique_ptr<Token> token,
    fit::function<void(storage::Status, IterationStatus, std::vector<DiffEntry>,
                       std::unique_ptr<Token>)>
        callback) {
  diff_utils::ComputeThreeWayDiff(
      storage_, *ancestor_, *left_, *right_, "",
      token ? convert::ToString(token->opaque_id) : "", type,
      callback::MakeScoped(
          weak_factory_.GetWeakPtr(),
          [this, callback = std::move(callback)](
              storage::Status status,
              std::pair<std::vector<DiffEntry>, std::string> page_change) {
            if (cancelled_) {
              callback(storage::Status::INTERNAL_ERROR, IterationStatus::OK, {},
                       nullptr);
              Finalize(storage::Status::INTERNAL_ERROR);
              return;
            }
            if (status != storage::Status::OK) {
              FXL_LOG(ERROR) << "Unable to compute diff due to error "
                             << fidl::ToUnderlying(status) << ", aborting.";
              callback(status, IterationStatus::OK, {}, nullptr);
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
            callback(storage::Status::OK, diff_status,
                     std::move(page_change.first), std::move(token));
          }));
}

void ConflictResolverClient::Merge(std::vector<MergedValue> merged_values,
                                   fit::function<void(Status)> callback) {
  has_merged_values_ = true;
  operation_serializer_.Serialize<storage::Status>(
      PageUtils::AdaptStatusCallback(std::move(callback)),
      [this, weak_this = weak_factory_.GetWeakPtr(),
       merged_values = std::move(merged_values)](
          fit::function<void(storage::Status)> callback) mutable {
        if (!IsInValidStateAndNotify(weak_this, callback)) {
          return;
        }
        auto waiter = fxl::MakeRefCounted<
            callback::Waiter<storage::Status, storage::ObjectIdentifier>>(
            storage::Status::OK);
        for (const MergedValue& merged_value : merged_values) {
          if (merged_value.source != ValueSource::DELETE) {
            GetOrCreateObjectIdentifier(merged_value, waiter->NewCallback());
          }
        }
        waiter->Finalize(
            [this, weak_this, merged_values = std::move(merged_values),
             callback = std::move(callback)](
                storage::Status status, std::vector<storage::ObjectIdentifier>
                                            object_identifiers) mutable {
              if (!IsInValidStateAndNotify(weak_this, callback, status)) {
                return;
              }

              // |object_identifiers| contains only the identifiers of objects
              // that have been inserted.
              size_t i = 0;
              for (const MergedValue& merged_value : merged_values) {
                if (merged_value.source == ValueSource::DELETE) {
                  journal_->Delete(merged_value.key);
                } else {
                  journal_->Put(merged_value.key, object_identifiers[i],
                                merged_value.priority == Priority::EAGER
                                    ? storage::KeyPriority::EAGER
                                    : storage::KeyPriority::LAZY);
                  ++i;
                }
              }
              FXL_DCHECK(i == object_identifiers.size());
              callback(storage::Status::OK);
            });
      });
}

void ConflictResolverClient::MergeNonConflictingEntries(
    fit::function<void(Status)> callback) {
  operation_serializer_.Serialize<storage::Status>(
      PageUtils::AdaptStatusCallback(std::move(callback)),
      [this, weak_this = weak_factory_.GetWeakPtr()](
          fit::function<void(storage::Status)> callback) mutable {
        if (!IsInValidStateAndNotify(weak_this, callback)) {
          return;
        }

        auto on_next = [this, weak_this](storage::ThreeWayChange change) {
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
                            change.right->priority);
            } else {
              journal_->Delete(change.base->key);
            }
          } else if (util::EqualPtr(change.base, change.right) &&
                     has_merged_values_) {
            if (change.left) {
              journal_->Put(change.left->key, change.left->object_identifier,
                            change.left->priority);
            } else {
              journal_->Delete(change.base->key);
            }
          }
          return true;
        };
        storage_->GetThreeWayContentsDiff(*ancestor_, *left_, *right_, "",
                                          std::move(on_next),
                                          std::move(callback));
      });
}

void ConflictResolverClient::Done(fit::function<void(Status)> callback) {
  operation_serializer_.Serialize<storage::Status>(
      PageUtils::AdaptStatusCallback(std::move(callback)),
      [this, weak_this = weak_factory_.GetWeakPtr()](
          fit::function<void(storage::Status)> callback) mutable {
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
                  callback(storage::Status::OK);
                  Finalize(storage::Status::OK);
                }));
      });
}

bool ConflictResolverClient::IsInValidStateAndNotify(
    const fxl::WeakPtr<ConflictResolverClient>& weak_this,
    const fit::function<void(storage::Status)>& callback,
    storage::Status status) {
  if (!weak_this) {
    callback(storage::Status::INTERNAL_ERROR);
    return false;
  }
  if (!weak_this->cancelled_ && status == storage::Status::OK) {
    return true;
  }
  storage::Status ledger_status =
      weak_this->cancelled_ ? storage::Status::INTERNAL_ERROR : status;
  // An eventual error was logged before, no need to do it again here.
  callback(ledger_status);
  // Finalize destroys this object; we need to do it after executing
  // the callback.
  weak_this->Finalize(ledger_status);
  return false;
}

}  // namespace ledger

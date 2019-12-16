// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/merging/conflict_resolver_client.h"

#include <lib/fit/function.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "src/ledger/bin/app/active_page_manager.h"
#include "src/ledger/bin/app/diff_utils.h"
#include "src/ledger/bin/app/fidl/serialization_size.h"
#include "src/ledger/bin/app/page_utils.h"
#include "src/ledger/bin/public/status.h"
#include "src/ledger/lib/callback/scoped_callback.h"
#include "src/ledger/lib/callback/waiter.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/memory/ref_ptr.h"
#include "src/ledger/lib/memory/weak_ptr.h"
#include "src/ledger/lib/socket/strings.h"
#include "src/ledger/lib/util/ptr.h"

namespace ledger {
ConflictResolverClient::ConflictResolverClient(storage::PageStorage* storage,
                                               ActivePageManager* active_page_manager,
                                               ConflictResolver* conflict_resolver,
                                               std::unique_ptr<const storage::Commit> left,
                                               std::unique_ptr<const storage::Commit> right,
                                               std::unique_ptr<const storage::Commit> ancestor,
                                               fit::function<void(Status)> callback)
    : storage_(storage),
      manager_(active_page_manager),
      conflict_resolver_(conflict_resolver),
      left_(std::move(left)),
      right_(std::move(right)),
      ancestor_(std::move(ancestor)),
      callback_(std::move(callback)),
      merge_result_provider_binding_(this),
      weak_factory_(this) {
  LEDGER_DCHECK(left_->GetTimestamp() >= right_->GetTimestamp());
  LEDGER_DCHECK(callback_);
}

ConflictResolverClient::~ConflictResolverClient() = default;

void ConflictResolverClient::Start() {
  // Prepare the journal for the merge commit.
  journal_ = storage_->StartMergeCommit(left_->Clone(), right_->Clone());

  PageSnapshotPtr page_snapshot_ancestor;
  manager_->BindPageSnapshot(ancestor_->Clone(), page_snapshot_ancestor.NewRequest(), "");

  PageSnapshotPtr page_snapshot_left;
  manager_->BindPageSnapshot(left_->Clone(), page_snapshot_left.NewRequest(), "");

  PageSnapshotPtr page_snapshot_right;
  manager_->BindPageSnapshot(right_->Clone(), page_snapshot_right.NewRequest(), "");

  in_client_request_ = true;
  conflict_resolver_->Resolve(std::move(page_snapshot_left), std::move(page_snapshot_right),
                              std::move(page_snapshot_ancestor),
                              merge_result_provider_binding_.NewBinding());
}

void ConflictResolverClient::Cancel() {
  cancelled_ = true;
  if (in_client_request_) {
    Finalize(Status::OK);
  }
}

void ConflictResolverClient::GetOrCreateObjectIdentifier(
    const MergedValue& merged_value,
    fit::function<void(Status, storage::ObjectIdentifier)> callback) {
  LEDGER_DCHECK(merged_value.source == ValueSource::RIGHT ||
                merged_value.source == ValueSource::NEW);
  switch (merged_value.source) {
    case ValueSource::RIGHT: {
      std::string key = convert::ToString(merged_value.key);
      storage_->GetEntryFromCommit(
          *right_, key, [key, callback = std::move(callback)](Status status, storage::Entry entry) {
            if (status != Status::OK) {
              if (status == Status::KEY_NOT_FOUND) {
                LEDGER_LOG(ERROR) << "Key " << key
                                  << " is not present in the right change. Unable to proceed";
              }
              callback(Status::INVALID_ARGUMENT, {});
              return;
            }
            callback(Status::OK, entry.object_identifier);
          });
      break;
    }
    case ValueSource::NEW: {
      if (merged_value.new_value->is_bytes()) {
        storage_->AddObjectFromLocal(
            storage::ObjectType::BLOB,
            storage::DataSource::Create(std::move(merged_value.new_value->bytes())), {},
            std::move(callback));
      } else {
        storage::ObjectIdentifier object_identifier;
        Status status = manager_->ResolveReference(std::move(merged_value.new_value->reference()),
                                                   &object_identifier);
        if (status != Status::OK) {
          callback(Status::INVALID_ARGUMENT, {});
          return;
        }
        callback(Status::OK, std::move(object_identifier));
      }
      break;
    }
    default: {
      LEDGER_NOTREACHED();
    }
  }
}

void ConflictResolverClient::Finalize(Status status) {
  LEDGER_DCHECK(callback_) << "Finalize must only be called once.";
  if (journal_) {
    journal_.reset();
  }
  zx_status_t zx_status = ZX_OK;
  if (status != Status::OK) {
    zx_status = ConvertToEpitaph(status);
  }
  merge_result_provider_binding_.Close(zx_status);
  auto callback = std::move(callback_);
  callback_ = nullptr;
  callback(status);
}

void ConflictResolverClient::GetFullDiff(
    std::unique_ptr<Token> token,
    fit::function<void(Status, std::vector<DiffEntry>, std::unique_ptr<Token>)> callback) {
  GetDiff(diff_utils::DiffType::FULL, std::move(token), std::move(callback));
}

void ConflictResolverClient::GetConflictingDiff(
    std::unique_ptr<Token> token,
    fit::function<void(Status, std::vector<DiffEntry>, std::unique_ptr<Token>)> callback) {
  GetDiff(diff_utils::DiffType::CONFLICTING, std::move(token), std::move(callback));
}

void ConflictResolverClient::GetDiff(
    diff_utils::DiffType type, std::unique_ptr<Token> token,
    fit::function<void(Status, std::vector<DiffEntry>, std::unique_ptr<Token>)> callback) {
  diff_utils::ComputeThreeWayDiff(
      storage_, *ancestor_, *left_, *right_, "", token ? convert::ToString(token->opaque_id) : "",
      type,
      MakeScoped(weak_factory_.GetWeakPtr(),
                 [this, callback = std::move(callback)](
                     Status status, std::pair<std::vector<DiffEntry>, std::string> page_change) {
                   if (cancelled_) {
                     callback(Status::INTERNAL_ERROR, {}, nullptr);
                     Finalize(Status::INTERNAL_ERROR);
                     return;
                   }
                   if (status != Status::OK) {
                     LEDGER_LOG(ERROR)
                         << "Unable to compute diff due to error " << status << ", aborting.";
                     callback(status, {}, nullptr);
                     Finalize(status);
                     return;
                   }

                   const std::string& next_token = page_change.second;
                   std::unique_ptr<Token> token;
                   if (!next_token.empty()) {
                     token = std::make_unique<Token>();
                     token->opaque_id = convert::ToArray(next_token);
                   }
                   callback(Status::OK, std::move(page_change.first), std::move(token));
                 }));
}

void ConflictResolverClient::Merge(std::vector<MergedValue> merged_values,
                                   fit::function<void(Status)> callback) {
  has_merged_values_ = true;
  operation_serializer_.Serialize<Status>(
      std::move(callback),
      [this, weak_this = weak_factory_.GetWeakPtr(),
       merged_values = std::move(merged_values)](fit::function<void(Status)> callback) mutable {
        if (!IsInValidStateAndNotify(weak_this, callback)) {
          return;
        }
        auto waiter = MakeRefCounted<Waiter<Status, storage::ObjectIdentifier>>(Status::OK);
        for (const MergedValue& merged_value : merged_values) {
          if (merged_value.source != ValueSource::DELETE) {
            GetOrCreateObjectIdentifier(merged_value, waiter->NewCallback());
          }
        }
        waiter->Finalize([this, weak_this, merged_values = std::move(merged_values),
                          callback = std::move(callback)](
                             Status status,
                             std::vector<storage::ObjectIdentifier> object_identifiers) mutable {
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
                            merged_value.priority == Priority::EAGER ? storage::KeyPriority::EAGER
                                                                     : storage::KeyPriority::LAZY);
              ++i;
            }
          }
          LEDGER_DCHECK(i == object_identifiers.size());
          callback(Status::OK);
        });
      });
}

void ConflictResolverClient::MergeNonConflictingEntries(fit::function<void(Status)> callback) {
  operation_serializer_.Serialize<Status>(
      std::move(callback),
      [this, weak_this = weak_factory_.GetWeakPtr()](fit::function<void(Status)> callback) mutable {
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
          if (EqualPtr(change.base, change.left)) {
            if (change.right) {
              journal_->Put(change.right->key, change.right->object_identifier,
                            change.right->priority);
            } else {
              journal_->Delete(change.base->key);
            }
          } else if (EqualPtr(change.base, change.right) && has_merged_values_) {
            if (change.left) {
              journal_->Put(change.left->key, change.left->object_identifier,
                            change.left->priority);
            } else {
              journal_->Delete(change.base->key);
            }
          }
          return true;
        };
        storage_->GetThreeWayContentsDiff(*ancestor_, *left_, *right_, "", std::move(on_next),
                                          std::move(callback));
      });
}

void ConflictResolverClient::Done(fit::function<void(Status)> callback) {
  operation_serializer_.Serialize<Status>(
      std::move(callback),
      [this, weak_this = weak_factory_.GetWeakPtr()](fit::function<void(Status)> callback) mutable {
        if (!IsInValidStateAndNotify(weak_this, callback)) {
          return;
        }
        in_client_request_ = false;
        LEDGER_DCHECK(!cancelled_);
        LEDGER_DCHECK(journal_);

        storage_->CommitJournal(
            std::move(journal_),
            MakeScoped(weak_factory_.GetWeakPtr(),
                       [this, weak_this, callback = std::move(callback)](
                           Status status, std::unique_ptr<const storage::Commit>) {
                         if (!IsInValidStateAndNotify(weak_this, callback, status)) {
                           return;
                         }
                         callback(Status::OK);
                         Finalize(Status::OK);
                       }));
      });
}

bool ConflictResolverClient::IsInValidStateAndNotify(
    const WeakPtr<ConflictResolverClient>& weak_this, const fit::function<void(Status)>& callback,
    Status status) {
  if (!weak_this) {
    callback(Status::INTERNAL_ERROR);
    return false;
  }
  if (!weak_this->cancelled_ && status == Status::OK) {
    return true;
  }
  Status ledger_status = weak_this->cancelled_ ? Status::INTERNAL_ERROR : status;
  // An eventual error was logged before, no need to do it again here.
  callback(ledger_status);
  // Finalize destroys this object; we need to do it after executing
  // the callback.
  weak_this->Finalize(ledger_status);
  return false;
}

}  // namespace ledger

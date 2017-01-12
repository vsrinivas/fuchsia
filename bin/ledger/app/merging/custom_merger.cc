// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/merging/custom_merger.h"

#include <memory>
#include <string>
#include <vector>

#include "apps/ledger/src/app/diff_utils.h"
#include "apps/ledger/src/app/page_manager.h"
#include "apps/ledger/src/callback/cancellable_helper.h"
#include "apps/ledger/src/callback/waiter.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/mtl/socket/strings.h"

namespace ledger {
namespace {
class Merger : public callback::Cancellable {
 public:
  Merger(storage::PageStorage* storage,
         PageManager* page_manager,
         ConflictResolver* conflict_resolver,
         std::unique_ptr<const storage::Commit> left,
         std::unique_ptr<const storage::Commit> right,
         std::unique_ptr<const storage::Commit> ancestor);
  ~Merger();

  static ftl::RefPtr<Merger> Create(
      storage::PageStorage* storage,
      PageManager* page_manager,
      ConflictResolver* conflict_resolver,
      std::unique_ptr<const storage::Commit> left,
      std::unique_ptr<const storage::Commit> right,
      std::unique_ptr<const storage::Commit> ancestor);

  void Start();

  // Cancellable
  void Cancel() override;
  bool IsDone() override;
  void SetOnDone(ftl::Closure callback) override;

 private:
  void OnChangesReady(storage::Status status,
                      std::vector<PageChangePtr> changes);
  void OnMergeDone(fidl::Array<MergedValuePtr> merged_values);
  void Done();

  ftl::Closure on_done_;
  storage::PageStorage* const storage_;
  PageManager* const manager_;
  ConflictResolver* const conflict_resolver_;

  std::unique_ptr<const storage::Commit> const left_;
  std::unique_ptr<const storage::Commit> const right_;
  std::unique_ptr<const storage::Commit> const ancestor_;

  std::unique_ptr<storage::Journal> journal_;
  bool is_done_ = false;
  bool cancelled_ = false;
};

Merger::Merger(storage::PageStorage* storage,
               PageManager* page_manager,
               ConflictResolver* conflict_resolver,
               std::unique_ptr<const storage::Commit> left,
               std::unique_ptr<const storage::Commit> right,
               std::unique_ptr<const storage::Commit> ancestor)
    : storage_(storage),
      manager_(page_manager),
      conflict_resolver_(conflict_resolver),
      left_(std::move(left)),
      right_(std::move(right)),
      ancestor_(std::move(ancestor)) {}

Merger::~Merger() {}

ftl::RefPtr<Merger> Merger::Create(
    storage::PageStorage* storage,
    PageManager* page_manager,
    ConflictResolver* conflict_resolver,
    std::unique_ptr<const storage::Commit> left,
    std::unique_ptr<const storage::Commit> right,
    std::unique_ptr<const storage::Commit> ancestor) {
  return ftl::AdoptRef(new Merger(storage, page_manager, conflict_resolver,
                                  std::move(left), std::move(right),
                                  std::move(ancestor)));
}

void Merger::Start() {
  ftl::RefPtr<callback::Waiter<storage::Status, PageChangePtr>> waiter =
      callback::Waiter<storage::Status, PageChangePtr>::Create(
          storage::Status::OK);

  diff_utils::ComputePageChange(storage_, *ancestor_, *left_,
                                waiter->NewCallback());
  diff_utils::ComputePageChange(storage_, *ancestor_, *right_,
                                waiter->NewCallback());

  waiter->Finalize([self = ftl::RefPtr<Merger>(this)](
      storage::Status status, std::vector<PageChangePtr> page_changes) mutable {
    self->OnChangesReady(std::move(status), std::move(page_changes));
  });
}

void Merger::OnChangesReady(storage::Status status,
                            std::vector<PageChangePtr> changes) {
  if (cancelled_) {
    return;
  }

  if (status != storage::Status::OK) {
    FTL_LOG(ERROR) << "Unable to compute diff due to error " << status
                   << ", aborting.";
    Done();
    return;
  }

  FTL_DCHECK(changes.size() == 2);

  PageSnapshotPtr page_snapshot;
  manager_->BindPageSnapshot(ancestor_->Clone(), page_snapshot.NewRequest());
  conflict_resolver_->Resolve(std::move(changes[0]), std::move(changes[1]),
                              std::move(page_snapshot),
                              [self = ftl::RefPtr<Merger>(this)](
                                  fidl::Array<MergedValuePtr> merged_values) {
                                self->OnMergeDone(std::move(merged_values));
                              });
}

void Merger::OnMergeDone(fidl::Array<MergedValuePtr> merged_values) {
  if (cancelled_) {
    return;
  }

  storage::Status s =
      storage_->StartMergeCommit(left_->GetId(), right_->GetId(), &journal_);
  if (s != storage::Status::OK) {
    FTL_LOG(ERROR) << "Unable to start merge commit: " << s;
    Done();
    return;
  }

  std::unique_ptr<storage::CommitContents> right_contents =
      right_->GetContents();
  ftl::RefPtr<callback::Waiter<storage::Status, storage::ObjectId>> waiter =
      callback::Waiter<storage::Status, storage::ObjectId>::Create(
          storage::Status::OK);
  bool has_error = false;
  for (const MergedValuePtr& merged_value : merged_values) {
    if (has_error) {
      break;
    }
    switch (merged_value->source) {
      case ValueSource::RIGHT: {
        std::unique_ptr<storage::Iterator<const storage::Entry>> entries =
            right_contents->find(merged_value->key);
        if (!entries || !entries->Valid() ||
            (*entries)->key != convert::ExtendedStringView(merged_value->key)) {
          FTL_LOG(ERROR)
              << "Key " << convert::ExtendedStringView(merged_value->key)
              << " is not present in the right change. Unable to proceed";
          waiter->NewCallback()(storage::Status::NOT_FOUND,
                                storage::ObjectId());
          has_error = true;
        } else {
          waiter->NewCallback()(storage::Status::OK, (*entries)->object_id);
        }
        break;
      }
      case ValueSource::NEW: {
        if (merged_value->new_value->is_bytes()) {
          // TODO(etiennej): Use asynchronous write, otherwise the run loop will
          // block until the socket is drained.
          mx::socket socket = mtl::WriteStringToSocket(
              convert::ToStringView(merged_value->new_value->get_bytes()));
          storage_->AddObjectFromLocal(
              std::move(socket), merged_value->new_value->get_bytes().size(),
              ftl::MakeCopyable([callback = waiter->NewCallback()](
                  storage::Status status, storage::ObjectId object_id) {
                callback(status, std::move(object_id));
              }));
        } else {
          waiter->NewCallback()(
              storage::Status::OK,
              convert::ToString(
                  merged_value->new_value->get_reference()->opaque_id));
        }
      } break;
      case ValueSource::DELETE: {
        journal_->Delete(merged_value->key);
        waiter->NewCallback()(storage::Status::OK, storage::ObjectId());
      } break;
    }
  }

  waiter->Finalize(ftl::MakeCopyable([
    self = ftl::RefPtr<Merger>(this), merged_values = std::move(merged_values)
  ](storage::Status status, std::vector<storage::ObjectId> object_ids) {
    if (self->cancelled_) {
      return;
    }

    if (status != storage::Status::OK) {
      // The error was logged before, no need to do it again here.
      self->Done();
      return;
    }

    for (size_t i = 0; i < object_ids.size(); ++i) {
      if (object_ids[i].empty()) {
        continue;
      }
      self->journal_->Put(merged_values[i]->key, object_ids[i],
                          merged_values[i]->priority == Priority::EAGER
                              ? storage::KeyPriority::EAGER
                              : storage::KeyPriority::LAZY);
    }
    self->journal_->Commit(
        [self](storage::Status status, const storage::CommitId& commit_id) {
          if (status != storage::Status::OK) {
            FTL_LOG(ERROR) << "Unable to commit merge journal: " << status;
          }
          self->Done();
        });
  }));
}

void Merger::Cancel() {
  cancelled_ = true;
}

bool Merger::IsDone() {
  return is_done_;
}

void Merger::SetOnDone(ftl::Closure callback) {
  on_done_ = callback;
}

void Merger::Done() {
  if (cancelled_) {
    return;
  }
  is_done_ = true;
  if (on_done_) {
    on_done_();
  }
}

}  // namespace

CustomMerger::CustomMerger(ConflictResolverPtr conflict_resolver)
    : conflict_resolver_(std::move(conflict_resolver)) {
  conflict_resolver_.set_connection_error_handler([this]() {
    if (on_error_) {
      on_error_();
    }
  });
}

CustomMerger::~CustomMerger() {}

void CustomMerger::SetOnError(ftl::Closure on_error) {
  on_error_ = on_error;
}

ftl::RefPtr<callback::Cancellable> CustomMerger::Merge(
    storage::PageStorage* storage,
    PageManager* page_manager,
    std::unique_ptr<const storage::Commit> head_1,
    std::unique_ptr<const storage::Commit> head_2,
    std::unique_ptr<const storage::Commit> ancestor) {
  if (head_1->GetTimestamp() < head_2->GetTimestamp()) {
    // Use the most recent commit as the base.
    head_1.swap(head_2);
  }

  // Both merger and this CustomMerger instance are owned by the same
  // MergeResolver object. MergeResolver makes sure that |Merger|s are cancelled
  // before destroying the CustomMerger merge strategy.
  ftl::RefPtr<Merger> merger =
      Merger::Create(storage, page_manager, &*conflict_resolver_,
                     std::move(head_1), std::move(head_2), std::move(ancestor));

  merger->Start();
  return merger;
}

}  // namespace ledger

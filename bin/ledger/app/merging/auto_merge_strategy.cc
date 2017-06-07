// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/merging/auto_merge_strategy.h"

#include <memory>
#include <string>
#include <vector>

#include "apps/ledger/src/app/merging/conflict_resolver_client.h"
#include "apps/ledger/src/app/page_manager.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/memory/weak_ptr.h"

namespace ledger {
class AutoMergeStrategy::AutoMerger {
 public:
  AutoMerger(storage::PageStorage* storage,
             PageManager* page_manager,
             ConflictResolver* conflict_resolver,
             std::unique_ptr<const storage::Commit> left,
             std::unique_ptr<const storage::Commit> right,
             std::unique_ptr<const storage::Commit> ancestor,
             ftl::Closure on_done);
  ~AutoMerger();

  void Start();
  void Cancel();
  void Done();

 private:
  void OnRightChangeReady(
      storage::Status status,
      std::unique_ptr<std::vector<storage::EntryChange>> right_change);
  void OnComparisonDone(storage::Status status,
                        std::unique_ptr<std::vector<storage::EntryChange>>,
                        bool distinct);

  storage::PageStorage* const storage_;
  PageManager* const manager_;
  ConflictResolver* const conflict_resolver_;

  std::unique_ptr<const storage::Commit> left_;
  std::unique_ptr<const storage::Commit> right_;
  std::unique_ptr<const storage::Commit> ancestor_;

  std::unique_ptr<ConflictResolverClient> delegated_merge_;

  ftl::Closure on_done_;

  std::unique_ptr<storage::Journal> journal_;
  bool cancelled_ = false;

  // This must be the last member of the class.
  ftl::WeakPtrFactory<AutoMergeStrategy::AutoMerger> weak_factory_;
};

AutoMergeStrategy::AutoMerger::AutoMerger(
    storage::PageStorage* storage,
    PageManager* page_manager,
    ConflictResolver* conflict_resolver,
    std::unique_ptr<const storage::Commit> left,
    std::unique_ptr<const storage::Commit> right,
    std::unique_ptr<const storage::Commit> ancestor,
    ftl::Closure on_done)
    : storage_(storage),
      manager_(page_manager),
      conflict_resolver_(conflict_resolver),
      left_(std::move(left)),
      right_(std::move(right)),
      ancestor_(std::move(ancestor)),
      on_done_(std::move(on_done)),
      weak_factory_(this) {
  FTL_DCHECK(on_done_);
}

AutoMergeStrategy::AutoMerger::~AutoMerger() {
  if (journal_) {
    storage_->RollbackJournal(std::move(journal_));
  }
}

void AutoMergeStrategy::AutoMerger::Start() {
  std::unique_ptr<std::vector<storage::EntryChange>> changes(
      new std::vector<storage::EntryChange>());
  auto on_next =
      [ weak_this = weak_factory_.GetWeakPtr(),
        changes = changes.get() ](storage::EntryChange change) {
    if (!weak_this) {
      return false;
    }

    if (weak_this->cancelled_) {
      return false;
    }

    changes->push_back(change);
    return true;
  };

  auto on_done = ftl::MakeCopyable([
    weak_this = weak_factory_.GetWeakPtr(), changes = std::move(changes)
  ](storage::Status status) mutable {
    if (weak_this) {
      weak_this->OnRightChangeReady(std::move(status), std::move(changes));
    }
  });

  storage_->GetCommitContentsDiff(*ancestor_, *right_, "", std::move(on_next),
                                  std::move(on_done));
}

void AutoMergeStrategy::AutoMerger::OnRightChangeReady(
    storage::Status status,
    std::unique_ptr<std::vector<storage::EntryChange>> right_change) {
  if (cancelled_) {
    Done();
    return;
  }

  if (status != storage::Status::OK) {
    FTL_LOG(ERROR) << "Unable to compute diff due to error " << status
                   << ", aborting.";
    Done();
    return;
  }

  struct PageChangeIndex {
    size_t entry_index = 0;
    bool distinct = true;
  };

  std::unique_ptr<PageChangeIndex> index(new PageChangeIndex());

  auto on_next = ftl::MakeCopyable([
    weak_this = weak_factory_.GetWeakPtr(), index = index.get(),
    right_change = right_change.get()
  ](storage::EntryChange change) {
    if (!weak_this) {
      return false;
    }

    if (weak_this->cancelled_) {
      return false;
    }

    while (change.entry.key > (*right_change)[index->entry_index].entry.key) {
      index->entry_index++;
      if (index->entry_index >= right_change->size()) {
        return false;
      }
    }
    if (change.entry.key == (*right_change)[index->entry_index].entry.key) {
      if (change == (*right_change)[index->entry_index]) {
        return true;
      }
      index->distinct = false;
      return false;
    }
    return true;
  });

  // |on_done| is called when the full diff is computed.
  auto on_done = ftl::MakeCopyable([
    weak_this = weak_factory_.GetWeakPtr(),
    right_change = std::move(right_change), index = std::move(index)
  ](storage::Status status) mutable {
    if (weak_this) {
      weak_this->OnComparisonDone(status, std::move(right_change),
                                  index->distinct);
    }
  });

  storage_->GetCommitContentsDiff(*ancestor_, *left_, "", std::move(on_next),
                                  std::move(on_done));
}

void AutoMergeStrategy::AutoMerger::OnComparisonDone(
    storage::Status status,
    std::unique_ptr<std::vector<storage::EntryChange>> right_changes,
    bool distinct) {
  if (cancelled_) {
    Done();
    return;
  }

  if (status != storage::Status::OK) {
    FTL_LOG(ERROR) << "Unable to compute diff due to error " << status
                   << ", aborting.";
    Done();
    return;
  }

  if (!distinct) {
    // Some keys are overlapping, so we need to proceed like the CUSTOM
    // strategy. We could be more efficient if we reused |right_changes| instead
    // of re-computing the diff inside |ConflictResolverClient|.
    delegated_merge_ = std::make_unique<ConflictResolverClient>(
        storage_, manager_, conflict_resolver_, std::move(left_),
        std::move(right_), std::move(ancestor_),
        [weak_this = weak_factory_.GetWeakPtr()] {
          if (weak_this) {
            weak_this->Done();
          }
        });

    delegated_merge_->Start();
    return;
  }

  // Here, we reuse the diff we computed before to create the merge commit. As
  // StartMergeCommit uses the left commit (first parameter) as its base, we
  // only have to apply the right diff to it and we are done.
  storage::Status s =
      storage_->StartMergeCommit(left_->GetId(), right_->GetId(), &journal_);
  if (s != storage::Status::OK) {
    FTL_LOG(ERROR) << "Unable to start merge commit: " << s;
    Done();
    return;
  }
  for (const storage::EntryChange& change : *right_changes) {
    if (change.deleted) {
      journal_->Delete(change.entry.key);
    } else {
      journal_->Put(change.entry.key, change.entry.object_id,
                    change.entry.priority);
    }
  }
  storage_->CommitJournal(
      std::move(journal_), [weak_this = weak_factory_.GetWeakPtr()](
                               storage::Status status,
                               std::unique_ptr<const storage::Commit>) {
        if (status != storage::Status::OK) {
          FTL_LOG(ERROR) << "Unable to commit merge journal: " << status;
        }
        if (weak_this) {
          weak_this->journal_.reset();
          weak_this->Done();
        }
      });
}

void AutoMergeStrategy::AutoMerger::Cancel() {
  cancelled_ = true;
  delegated_merge_->Cancel();
}

void AutoMergeStrategy::AutoMerger::Done() {
  if (journal_) {
    storage_->RollbackJournal(std::move(journal_));
    journal_.reset();
  }
  delegated_merge_.reset();
  auto on_done = std::move(on_done_);
  on_done_ = nullptr;
  on_done();
}

AutoMergeStrategy::AutoMergeStrategy(ConflictResolverPtr conflict_resolver)
    : conflict_resolver_(std::move(conflict_resolver)) {
  conflict_resolver_.set_connection_error_handler([this]() {
    // If a merge is in progress, it must be terminated.
    if (in_progress_merge_) {
      // The actual cleanup of in_progress_merge_ will happen in its on_done
      // callback.
      in_progress_merge_->Cancel();
    }
    if (on_error_) {
      // It is safe to call |on_error_| because the error handler waits for the
      // merges to finish before deleting this object.
      on_error_();
    }
  });
}

AutoMergeStrategy::~AutoMergeStrategy() {}

void AutoMergeStrategy::SetOnError(ftl::Closure on_error) {
  on_error_ = std::move(on_error);
}

void AutoMergeStrategy::Merge(storage::PageStorage* storage,
                              PageManager* page_manager,
                              std::unique_ptr<const storage::Commit> head_1,
                              std::unique_ptr<const storage::Commit> head_2,
                              std::unique_ptr<const storage::Commit> ancestor,
                              ftl::Closure on_done) {
  FTL_DCHECK(head_1->GetTimestamp() <= head_2->GetTimestamp());
  FTL_DCHECK(!in_progress_merge_);

  in_progress_merge_ = std::make_unique<AutoMergeStrategy::AutoMerger>(
      storage, page_manager, conflict_resolver_.get(), std::move(head_2),
      std::move(head_1), std::move(ancestor),
      [ this, on_done = std::move(on_done) ] {
        in_progress_merge_.reset();
        on_done();
      });

  in_progress_merge_->Start();
}

void AutoMergeStrategy::Cancel() {
  if (in_progress_merge_) {
    in_progress_merge_->Cancel();
  }
}

}  // namespace ledger

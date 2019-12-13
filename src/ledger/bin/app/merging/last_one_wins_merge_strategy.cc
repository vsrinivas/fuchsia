// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/merging/last_one_wins_merge_strategy.h"

#include <lib/fit/function.h>

#include <memory>
#include <string>

#include "src/ledger/bin/app/active_page_manager.h"
#include "src/ledger/bin/app/page_utils.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/memory/weak_ptr.h"
#include "src/lib/callback/scoped_callback.h"

namespace ledger {

class LastOneWinsMergeStrategy::LastOneWinsMerger {
 public:
  LastOneWinsMerger(storage::PageStorage* storage, std::unique_ptr<const storage::Commit> left,
                    std::unique_ptr<const storage::Commit> right,
                    std::unique_ptr<const storage::Commit> ancestor,
                    fit::function<void(Status)> callback);
  ~LastOneWinsMerger();

  void Start();
  void Cancel();

 private:
  void Done(Status status);
  void BuildAndCommitJournal();

  storage::PageStorage* const storage_;

  std::unique_ptr<const storage::Commit> const left_;
  std::unique_ptr<const storage::Commit> const right_;
  std::unique_ptr<const storage::Commit> const ancestor_;

  fit::function<void(Status)> callback_;

  std::unique_ptr<storage::Journal> journal_;
  bool cancelled_ = false;

  // This must be the last member of the class.
  WeakPtrFactory<LastOneWinsMerger> weak_factory_;
};

LastOneWinsMergeStrategy::LastOneWinsMerger::LastOneWinsMerger(
    storage::PageStorage* storage, std::unique_ptr<const storage::Commit> left,
    std::unique_ptr<const storage::Commit> right, std::unique_ptr<const storage::Commit> ancestor,
    fit::function<void(Status)> callback)
    : storage_(storage),
      left_(std::move(left)),
      right_(std::move(right)),
      ancestor_(std::move(ancestor)),
      callback_(std::move(callback)),
      weak_factory_(this) {
  LEDGER_DCHECK(callback_);
}

LastOneWinsMergeStrategy::LastOneWinsMerger::~LastOneWinsMerger() = default;

void LastOneWinsMergeStrategy::LastOneWinsMerger::Start() {
  journal_ = storage_->StartMergeCommit(left_->Clone(), right_->Clone());
  BuildAndCommitJournal();
}

void LastOneWinsMergeStrategy::LastOneWinsMerger::Cancel() {
  cancelled_ = true;
  if (journal_) {
    journal_.reset();
  }
}

void LastOneWinsMergeStrategy::LastOneWinsMerger::Done(Status status) {
  auto callback = std::move(callback_);
  callback_ = nullptr;
  callback(status);
}

void LastOneWinsMergeStrategy::LastOneWinsMerger::BuildAndCommitJournal() {
  auto on_next = [weak_this = weak_factory_.GetWeakPtr()](storage::EntryChange change) {
    if (!weak_this || weak_this->cancelled_) {
      // No need to call Done, as it will be called in the on_done callback.
      return false;
    }
    const std::string& key = change.entry.key;
    if (change.deleted) {
      weak_this->journal_->Delete(key);
    } else {
      weak_this->journal_->Put(key, change.entry.object_identifier, change.entry.priority);
    }
    return true;
  };

  auto on_diff_done = [weak_this = weak_factory_.GetWeakPtr()](Status s) {
    if (!weak_this) {
      return;
    }
    if (weak_this->cancelled_) {
      weak_this->Done(Status::INTERNAL_ERROR);
      return;
    }
    if (s != Status::OK) {
      LEDGER_LOG(ERROR) << "Unable to create diff for merging: " << s;
      weak_this->Done(s);
      return;
    }

    weak_this->storage_->CommitJournal(
        std::move(weak_this->journal_),
        [weak_this](Status s, std::unique_ptr<const storage::Commit>) {
          if (s != Status::OK) {
            LEDGER_LOG(ERROR) << "Unable to commit merge journal: " << s;
          }
          if (weak_this) {
            weak_this->Done(s);
          }
        });
  };
  storage_->GetCommitContentsDiff(*(ancestor_), *(right_), "", std::move(on_next),
                                  std::move(on_diff_done));
}

LastOneWinsMergeStrategy::LastOneWinsMergeStrategy() = default;

LastOneWinsMergeStrategy::~LastOneWinsMergeStrategy() = default;

void LastOneWinsMergeStrategy::SetOnError(fit::function<void()> /*on_error*/) {}

void LastOneWinsMergeStrategy::Merge(storage::PageStorage* storage,
                                     ActivePageManager* /*page_manager*/,
                                     std::unique_ptr<const storage::Commit> head_1,
                                     std::unique_ptr<const storage::Commit> head_2,
                                     std::unique_ptr<const storage::Commit> ancestor,
                                     fit::function<void(Status)> callback) {
  LEDGER_DCHECK(!in_progress_merge_);
  LEDGER_DCHECK(storage::Commit::TimestampOrdered(head_1, head_2));

  in_progress_merge_ = std::make_unique<LastOneWinsMergeStrategy::LastOneWinsMerger>(
      storage, std::move(head_1), std::move(head_2), std::move(ancestor),
      [this, callback = std::move(callback)](Status status) {
        in_progress_merge_.reset();
        callback(status);
      });

  in_progress_merge_->Start();
}

void LastOneWinsMergeStrategy::Cancel() {
  if (in_progress_merge_) {
    in_progress_merge_->Cancel();
  }
}

}  // namespace ledger

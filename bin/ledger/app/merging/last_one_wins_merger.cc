// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/merging/last_one_wins_merger.h"

#include <memory>

#include "apps/ledger/src/callback/cancellable_helper.h"
#include "lib/ftl/functional/closure.h"

namespace ledger {
namespace {
class Merger : public callback::Cancellable {
 public:
  Merger(storage::PageStorage* storage,
         std::unique_ptr<const storage::Commit> left,
         std::unique_ptr<const storage::Commit> right,
         std::unique_ptr<const storage::Commit> ancestor);
  ~Merger();

  static ftl::RefPtr<Merger> Create(
      storage::PageStorage* storage,
      std::unique_ptr<const storage::Commit> left,
      std::unique_ptr<const storage::Commit> right,
      std::unique_ptr<const storage::Commit> ancestor);

  void Start();

  // Cancellable
  void Cancel() override;
  bool IsDone() override;
  void SetOnDone(ftl::Closure callback) override;

 private:
  void OnDiffReady();
  void Done();

  ftl::Closure on_done_;
  storage::PageStorage* const storage_;

  std::unique_ptr<const storage::Commit> const left_;
  std::unique_ptr<const storage::Commit> const right_;
  std::unique_ptr<storage::Iterator<const storage::EntryChange>> right_changes_;

  std::unique_ptr<const storage::Commit> const ancestor_;
  std::unique_ptr<storage::CommitContents> ancestor_contents_;

  std::unique_ptr<storage::Journal> journal_;
  bool is_done_ = false;
  bool cancelled_ = false;
};

Merger::Merger(storage::PageStorage* storage,
               std::unique_ptr<const storage::Commit> left,
               std::unique_ptr<const storage::Commit> right,
               std::unique_ptr<const storage::Commit> ancestor)
    : storage_(storage),
      left_(std::move(left)),
      right_(std::move(right)),
      ancestor_(std::move(ancestor)) {}

Merger::~Merger() {}

ftl::RefPtr<Merger> Merger::Create(
    storage::PageStorage* storage,
    std::unique_ptr<const storage::Commit> left,
    std::unique_ptr<const storage::Commit> right,
    std::unique_ptr<const storage::Commit> ancestor) {
  return ftl::AdoptRef(new Merger(storage, std::move(left), std::move(right),
                                  std::move(ancestor)));
}

void Merger::Start() {
  std::unique_ptr<storage::CommitContents> right_contents =
      right_->GetContents();
  ancestor_contents_ = ancestor_->GetContents();
  ancestor_contents_->diff(
      std::move(right_contents),
      [this](storage::Status status,
             std::unique_ptr<storage::Iterator<const storage::EntryChange>>
                 right_changes) mutable {
        if (status != storage::Status::OK) {
          FTL_LOG(ERROR) << "Unable to create diff for merging: " << status;
          Done();
          return;
        }
        right_changes_ = std::move(right_changes);
        OnDiffReady();
      });
}

void Merger::OnDiffReady() {
  if (cancelled_) {
    return;
  }
  storage::Status s =
      storage_->StartMergeCommit(left_->GetId(), right_->GetId(), &journal_);
  FTL_DCHECK(s == storage::Status::OK);

  while (right_changes_->Valid()) {
    const std::string& key = (*right_changes_)->entry.key;
    if ((*right_changes_)->deleted) {
      storage::Status s = journal_->Delete(key);
      if (s != storage::Status::OK) {
        FTL_LOG(ERROR) << "Error while merging commits: " << s;
      }
    } else {
      storage::Status s = journal_->Put(key, (*right_changes_)->entry.object_id,
                                        (*right_changes_)->entry.priority);
      if (s != storage::Status::OK) {
        FTL_LOG(ERROR) << "Error while merging commits: " << s;
      }
    }
    right_changes_->Next();
  }
  journal_->Commit(
      [this](storage::Status status, const storage::CommitId& commit_id) {
        if (status != storage::Status::OK) {
          FTL_LOG(ERROR) << "Unable to commit merge journal: " << status;
        }
        Done();
      });
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

LastOneWinsMerger::LastOneWinsMerger(storage::PageStorage* storage)
    : storage_(storage) {}

LastOneWinsMerger::~LastOneWinsMerger() {}

ftl::RefPtr<callback::Cancellable> LastOneWinsMerger::Merge(
    std::unique_ptr<const storage::Commit> head_1,
    std::unique_ptr<const storage::Commit> head_2,
    std::unique_ptr<const storage::Commit> ancestor) {
  if (head_1->GetTimestamp() > head_2->GetTimestamp()) {
    // Order commits by their timestamps. Then we know that head_2 overwrites
    // head_1 under this merging strategy.
    head_1.swap(head_2);
  }
  ftl::RefPtr<Merger> merger = Merger::Create(
      storage_, std::move(head_1), std::move(head_2), std::move(ancestor));

  merger->Start();
  return merger;
}

}  // namespace ledger

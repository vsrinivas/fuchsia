// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_MERGING_MERGE_RESOLVER_H_
#define PERIDOT_BIN_LEDGER_APP_MERGING_MERGE_RESOLVER_H_

#include <vector>

#include <lib/fit/function.h>

#include "lib/backoff/backoff.h"
#include "lib/callback/scoped_task_runner.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/bin/ledger/coroutine/coroutine.h"
#include "peridot/bin/ledger/environment/environment.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"

namespace ledger {
class PageManager;
class MergeStrategy;

// MergeResolver watches a page and resolves conflicts as they appear using the
// provided merge strategy.
class MergeResolver : public storage::CommitWatcher {
 public:
  MergeResolver(fit::closure on_destroyed, Environment* environment,
                storage::PageStorage* storage,
                std::unique_ptr<backoff::Backoff> backoff);
  ~MergeResolver() override;

  void set_on_empty(fit::closure on_empty_callback);

  // Returns true if no merge is currently in progress. Note that returning
  // true, does not mean that there are no pending conflicts.
  bool IsEmpty();

  // Returns true if a merge is pending or in progress. A merge is pending when
  // a merge is currently processed (|IsEmpty| returns false), but also when
  // checking for conflict, or when in backoff delay between merges.
  bool HasUnfinishedMerges();

  // Changes the current merge strategy. Any pending merge will be cancelled.
  void SetMergeStrategy(std::unique_ptr<MergeStrategy> strategy);

  void SetPageManager(PageManager* page_manager);

  // Adds an action to perform when all the pending conflicts are resolved
  // (once).
  void RegisterNoConflictCallback(
      fit::function<void(ConflictResolutionWaitStatus)> callback);

 private:
  // DelayedStatus allows us to avoid merge storms (several devices battling
  // to merge branches but not agreeing). We use the following algorithm:
  // - Old (local or originally remote) changes are always merged right away.
  // Local changes do not pose any risk risk of storm, as you cannot storm with
  // yourself.
  // - When a remote change arrives, that is a merge of two merges, then we are
  // at risk of a merge storm. In that case, we delay.
  // - If we receive any new commit while we are delaying, these are not merged
  // right away; they are only merged after the delay.
  // - Once the delay is finished, we merge everything we know. Upload will not
  // happen until we finish merging all branches, so we don't risk amplifying a
  // storm while merging.
  // - If, after that, we still need to do a merge of a merge from remote
  // commits, then we delay again, but more (exponential backoff).
  // - We reset this backoff delay to its initial value once we see a non
  // merge-of-a-merge commit.
  enum class DelayedStatus {
    // Whatever the commits, we won't delay merging. Used for local commits.
    DONT_DELAY,
    // May delay
    MAY_DELAY,
  };

  // storage::CommitWatcher:
  void OnNewCommits(
      const std::vector<std::unique_ptr<const storage::Commit>>& commits,
      storage::ChangeSource source) override;

  void PostCheckConflicts(DelayedStatus delayed_status);
  void CheckConflicts(DelayedStatus delayed_status);
  void ResolveConflicts(DelayedStatus delayed_status,
                        std::vector<storage::CommitId> heads);

  coroutine::CoroutineService* coroutine_service_;
  storage::PageStorage* const storage_;
  std::unique_ptr<backoff::Backoff> backoff_;
  PageManager* page_manager_ = nullptr;
  std::unique_ptr<MergeStrategy> strategy_;
  std::unique_ptr<MergeStrategy> next_strategy_;
  bool has_next_strategy_ = false;
  // TODO(LE-384): Convert the fields below into a single enum to track the
  // state of this class.
  bool merge_in_progress_ = false;
  // True between the time we commit a merge and we check if there are more
  // conflicts. It is used to report to conflict callbacks (see
  // |no_conflict_callbacks_|) whether a conflict has been merged while waiting.
  bool has_merged_ = false;
  // Counts the number of currently pending |CheckConflict| tasks posted on the
  // run loop. We use a counter instead of a single flag as multiple
  // |CheckConflict| tasks could be pending at the same time.
  int check_conflicts_task_count_ = 0;
  bool check_conflicts_in_progress_ = false;
  bool in_delay_ = false;
  fit::closure on_empty_callback_;
  fit::closure on_destroyed_;
  std::vector<fit::function<void(ConflictResolutionWaitStatus)>>
      no_conflict_callbacks_;

  // ScopedTaskRunner must be the last member of the class.
  callback::ScopedTaskRunner task_runner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MergeResolver);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_MERGING_MERGE_RESOLVER_H_

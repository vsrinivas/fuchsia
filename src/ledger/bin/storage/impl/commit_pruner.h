// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_COMMIT_PRUNER_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_COMMIT_PRUNER_H_

#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/storage/impl/live_commit_tracker.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/coroutine/coroutine_manager.h"

namespace storage {

// Commit pruner computes which commits should be removed from the page storage.
class CommitPruner {
 public:
  class CommitPrunerDelegate {
   public:
    virtual ~CommitPrunerDelegate() = default;
    // Finds the commit with the given |commit_id| and calls the given |callback| with the result.
    // |PageStorage| must outlive any |Commit| obtained through it.
    virtual void GetCommit(CommitIdView commit_id,
                           fit::function<void(Status, std::unique_ptr<const Commit>)> callback) = 0;

    // Deletes the provided commits from local storage.
    virtual Status DeleteCommits(coroutine::CoroutineHandler* handler,
                                 std::vector<std::unique_ptr<const Commit>> commits) = 0;

    // Sets the new clock.
    virtual Status SetClock(coroutine::CoroutineHandler* handler, const Clock& clock) = 0;
  };
  CommitPruner(ledger::Environment* environment, CommitPrunerDelegate* delegate,
               LiveCommitTracker* commit_tracker, CommitPruningPolicy policy);
  ~CommitPruner();

  // Schedule a pruning cycle. If no pruning cycle is in progress, a task is posted to start pruning
  // immediately. Otherwise, a cycle will start when the current cycle stops. Only one cycle may be
  // scheduled at a time.
  void SchedulePruning();

  // Registers |self_id| as the device ID of this device, and |clock| as the current clock value.
  void LoadClock(clocks::DeviceId self_id, Clock clock);

 private:
  // Finds the latest unique common ancestor among the live commits, as given by the
  // LiveCommitTracker.
  Status FindLatestUniqueCommonAncestorSync(coroutine::CoroutineHandler* handler,
                                            std::unique_ptr<const storage::Commit>* result);
  // Returns all locally-known ancestors of a commit.
  Status GetAllAncestors(coroutine::CoroutineHandler* handler,
                         std::unique_ptr<const storage::Commit> base,
                         std::vector<std::unique_ptr<const storage::Commit>>* result);

  // Performs a pruning cycle. Only one pruning cycle may be run at a time.
  void Prune();
  Status SynchronousPrune(coroutine::CoroutineHandler* handler);

  ledger::Environment* environment_;
  // ID of this device for the page.
  clocks::DeviceId self_id_;
  // Full clock of the page, as known by this device.
  Clock clock_;

  CommitPrunerDelegate* const delegate_;
  LiveCommitTracker* const commit_tracker_;

  // Policy for pruning commits. By default, we don't prune.
  CommitPruningPolicy const policy_;

  enum class PruningState {
    // Pruning can start immediately.
    IDLE,
    // A pruning cycle is in progress.
    PRUNING,
    // A pruning cycle is in progress, and a new pruning cycle should be run once it completes.
    PRUNING_AND_SCHEDULED,
  };

  PruningState state_ = PruningState::IDLE;

  coroutine::CoroutineManager coroutine_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CommitPruner);
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_COMMIT_PRUNER_H_

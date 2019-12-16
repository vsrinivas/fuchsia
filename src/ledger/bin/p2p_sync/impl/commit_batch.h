// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_P2P_SYNC_IMPL_COMMIT_BATCH_H_
#define SRC_LEDGER_BIN_P2P_SYNC_IMPL_COMMIT_BATCH_H_

#include <lib/fit/function.h>

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "src/ledger/bin/p2p_provider/public/types.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/lib/memory/weak_ptr.h"

namespace p2p_sync {

// CommitBatch holds all commits that should be added together in PageStorage.
class CommitBatch {
 public:
  // Delegate is used by |CommitBatch| to request missing commits.
  class Delegate {
   public:
    // Request missing commits from this batch. Commits will be added later
    // through CommitBatch::AddBatch.
    virtual void RequestCommits(const p2p_provider::P2PClientId& device,
                                std::vector<storage::CommitId> ids) = 0;
  };

  CommitBatch(p2p_provider::P2PClientId device, Delegate* delegate, storage::PageStorage* storage);
  CommitBatch(const CommitBatch&) = delete;
  const CommitBatch& operator=(const CommitBatch&) = delete;

  // Registers a callback to be called when the batch processing is completed,
  // either through success or an unrecoverable error. Part of the
  // ledger::AutoCleanable* client API.
  void SetOnDiscardable(fit::closure on_discardable);

  bool IsDiscardable() const;

  // Adds the provided commits to this batch.
  // This method will attempt to add the whole batch to |PageStorage|, and may
  // request additional commits through the |Delegate| if needed.
  void AddToBatch(std::vector<storage::PageStorage::CommitIdAndBytes> new_commits);

  // Marks the peer as ready: commits may now be added to the storage.
  //
  // We have to wait until a peer is marked as "interested" to add the commits it sent us:
  // otherwise, we will try to request the objects referenced by the commits, but we will not
  // request them from the peer that sent us the commits, so it is possible they are not found and
  // adding the commits fails.
  void MarkPeerReady();

 private:
  // Adds the commits to local storage if ready. This is only valid to call once all parents that
  // are not in storage are present in the commits map.
  void AddCommits();

  p2p_provider::P2PClientId const device_;
  Delegate* const delegate_;
  storage::PageStorage* const storage_;
  bool peer_is_ready_ = false;
  // Map from commit ids of commits to be added to commit data and generation.
  std::map<storage::CommitId, std::pair<std::string, uint64_t>> commits_;

  // The set of missing commits that have been requested from the peer.
  std::set<storage::CommitId> requested_commits_;

  bool discardable_ = false;
  fit::closure on_discardable_;

  // This must be the last member of the class.
  ledger::WeakPtrFactory<CommitBatch> weak_factory_;
};

}  // namespace p2p_sync

#endif  // SRC_LEDGER_BIN_P2P_SYNC_IMPL_COMMIT_BATCH_H_

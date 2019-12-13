// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_COMMIT_FACTORY_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_COMMIT_FACTORY_H_

#include <lib/fit/function.h>

#include <memory>
#include <string>
#include <vector>

#include "peridot/lib/rng/random.h"
#include "src/ledger/bin/encryption/public/encryption_service.h"
#include "src/ledger/bin/storage/impl/live_commit_tracker.h"
#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/memory/weak_ptr.h"
#include "src/lib/timekeeper/clock.h"

namespace storage {

// Factory for new commits, that keeps track of live commits.
class CommitFactory : public LiveCommitTracker {
 public:
  CommitFactory(ObjectIdentifierFactory* object_identifier_factory);
  ~CommitFactory() override;

  // Factory method for creating a |Commit| object given its storage representation. If the format
  // is incorrect, |nullptr| will be returned.
  Status FromStorageBytes(CommitId id, std::string storage_bytes,
                          std::unique_ptr<const Commit>* commit);

  // Factory method for creating a |Commit| object from its components.
  std::unique_ptr<const Commit> FromContentAndParents(
      timekeeper::Clock* clock, rng::Random* random, ObjectIdentifier root_node_identifier,
      std::vector<std::unique_ptr<const Commit>> parent_commits);

  // Factory method for creating an empty |Commit| object, i.e. without parents and with empty
  // contents.
  void Empty(PageStorage* page_storage,
             fit::function<void(Status, std::unique_ptr<const Commit>)> callback);

  // Adds these commits to the list of current heads. In |GetHeads()| the heads
  // will be returned ordered by their timestamp, which is the |zx::time_utc|
  // element of each pair.
  void AddHeads(std::vector<std::unique_ptr<const Commit>> heads);

  // Removes these commits from the set of live heads.
  void RemoveHeads(const std::vector<CommitId>& commit_id);

  // Returns the current heads of a page, ordered by their associated time.
  std::vector<std::unique_ptr<const Commit>> GetHeads() const;

  // Adds a mapping from an unsynced commit with id |commit_id| to the identifiers it requires to be
  // alive.
  void AddCommitDependencies(CommitIdView commit_id,
                             std::vector<ObjectIdentifier> root_identifiers);

  // Removes the associated identifiers that were requested to be maintained alive for |commit_id|.
  // It is not an error if the entry is not present.
  void RemoveCommitDependencies(CommitIdView commit_id);

  // Returns the set of live root identifiers.
  std::set<ObjectIdentifier> GetLiveRootIdentifiers() const;

  // LiveCommitTracker:
  std::vector<std::unique_ptr<const Commit>> GetLiveCommits() const override;

 private:
  class CommitImpl;

  // CommitComparator orders commits by timestamp and ID.
  class CommitComparator {
    using is_transparent = std::true_type;

   public:
    bool operator()(const std::unique_ptr<const Commit>& left,
                    const std::unique_ptr<const Commit>& right) const;
    bool operator()(const Commit* left, const Commit* right) const;
  };

  // Registers a currently-untracked commit to be tracked.
  void RegisterCommit(Commit* commit);

  // Unregisters a currently tracked commit.
  void UnregisterCommit(Commit* commit);

  ObjectIdentifierFactory* object_identifier_factory_;

  // Set of currently live (in-memory) commits from the page tracked by this object.
  std::set<const Commit*> live_commits_;
  // Set of the current heads of the page tracked by this object.
  std::set<std::unique_ptr<const Commit>, CommitComparator> heads_;

  // When syncing commits, the diff between the commit's root identifier and its base parent is
  // computed and sent to the cloud. |live_root_identifiers_| maps the ids of unsynced commits to
  // their root identifier as well as that of their base commit, thus making sure these contents are
  // not garbage collected.
  std::map<CommitId, std::vector<ObjectIdentifier>> live_root_identifiers_;

  // This must be the last member of the class.
  ledger::WeakPtrFactory<CommitFactory> weak_factory_;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_COMMIT_FACTORY_H_

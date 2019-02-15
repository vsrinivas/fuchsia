// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_FAKE_FAKE_PAGE_STORAGE_H_
#define PERIDOT_BIN_LEDGER_STORAGE_FAKE_FAKE_PAGE_STORAGE_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include <lib/fit/function.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/strings/string_view.h>

#include "peridot/bin/ledger/encryption/fake/fake_encryption_service.h"
#include "peridot/bin/ledger/environment/environment.h"
#include "peridot/bin/ledger/storage/fake/fake_journal_delegate.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/storage/testing/page_storage_empty_impl.h"

namespace storage {
namespace fake {

// The delay for which tasks are posted by the FakePageStorage methods
// GetCommit() and GetPiece().
constexpr zx::duration kFakePageStorageDelay = zx::msec(5);

class FakePageStorage : public PageStorageEmptyImpl {
 public:
  FakePageStorage(ledger::Environment* environment, PageId page_id);
  ~FakePageStorage() override;

  // PageStorage:
  PageId GetId() override;
  void GetHeadCommitIds(
      fit::function<void(Status, std::vector<CommitId>)> callback) override;
  void GetMergeCommitIds(
      CommitIdView parent1_id, CommitIdView parent2_id,
      fit::function<void(Status, std::vector<CommitId>)> callback) override;
  void GetCommit(CommitIdView commit_id,
                 fit::function<void(Status, std::unique_ptr<const Commit>)>
                     callback) override;
  std::unique_ptr<Journal> StartCommit(const CommitId& commit_id,
                                       JournalType journal_type) override;
  std::unique_ptr<Journal> StartMergeCommit(const CommitId& left,
                                            const CommitId& right) override;
  void CommitJournal(
      std::unique_ptr<Journal> journal,
      fit::function<void(Status, std::unique_ptr<const storage::Commit>)>
          callback) override;
  void RollbackJournal(std::unique_ptr<Journal> journal,
                       fit::function<void(Status)> callback) override;
  Status AddCommitWatcher(CommitWatcher* watcher) override;
  Status RemoveCommitWatcher(CommitWatcher* watcher) override;
  void IsSynced(fit::function<void(Status, bool)> callback) override;
  void AddObjectFromLocal(
      ObjectType object_type, std::unique_ptr<DataSource> data_source,
      fit::function<void(Status, ObjectIdentifier)> callback) override;
  void GetObjectPart(
      ObjectIdentifier object_identifier, int64_t offset, int64_t max_size,
      Location location,
      fit::function<void(Status, fsl::SizedVmo)> callback) override;
  void GetObject(ObjectIdentifier object_identifier, Location location,
                 fit::function<void(Status, std::unique_ptr<const Object>)>
                     callback) override;
  void GetPiece(ObjectIdentifier object_identifier,
                fit::function<void(Status, std::unique_ptr<const Object>)>
                    callback) override;
  void GetCommitContents(const Commit& commit, std::string min_key,
                         fit::function<bool(Entry)> on_next,
                         fit::function<void(Status)> on_done) override;
  void GetEntryFromCommit(const Commit& commit, std::string key,
                          fit::function<void(Status, Entry)> callback) override;

  // For testing:
  void set_autocommit(bool autocommit) { autocommit_ = autocommit; }
  void set_synced(bool is_synced) { is_synced_ = is_synced; }

  const std::map<std::string, std::unique_ptr<FakeJournalDelegate>>&
  GetJournals() const;

  const std::map<ObjectIdentifier, std::string>& GetObjects() const;

  // Deletes this object from the fake local storage, but keeps it in its
  // "network" storage.
  void DeleteObjectFromLocal(const ObjectIdentifier& object_identifier);

  // If set to true, no commit notification is sent to the commit watchers.
  void SetDropCommitNotifications(bool drop);

 protected:
  // Returns an ObjectDigest (for use in the object identifier returned by
  // AddObjectFromLocal).
  // By default, fake object digests are invalid to ensure external clients do
  // not rely implicitly on the internal encoding. Specific tests can override
  // this method if they need valid object digests instead.
  virtual ObjectDigest FakeDigest(fxl::StringView value) const;

  PageId page_id_;

 private:
  void SendNextObject();

  bool autocommit_ = true;
  bool drop_commit_notifications_ = false;
  bool is_synced_ = false;

  ledger::Environment* const environment_;
  std::map<std::string, std::unique_ptr<FakeJournalDelegate>> journals_;
  std::map<ObjectIdentifier, std::string> objects_;
  std::set<CommitId> heads_;
  std::map<std::pair<CommitId, CommitId>, std::vector<CommitId>> merges_;
  std::set<CommitWatcher*> watchers_;
  std::vector<fit::closure> object_requests_;
  encryption::FakeEncryptionService encryption_service_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakePageStorage);
};

}  // namespace fake
}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_FAKE_FAKE_PAGE_STORAGE_H_

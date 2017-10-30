// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_FAKE_FAKE_PAGE_STORAGE_H_
#define PERIDOT_BIN_LEDGER_STORAGE_FAKE_FAKE_PAGE_STORAGE_H_

#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"
#include "peridot/bin/ledger/storage/fake/fake_journal_delegate.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/storage/test/page_storage_empty_impl.h"

namespace storage {
namespace fake {

class FakePageStorage : public test::PageStorageEmptyImpl {
 public:
  explicit FakePageStorage(PageId page_id);
  FakePageStorage(fsl::MessageLoop* message_loop, PageId page_id);
  ~FakePageStorage() override;

  // PageStorage:
  PageId GetId() override;
  void GetHeadCommitIds(
      std::function<void(Status, std::vector<CommitId>)> callback) override;
  void GetCommit(CommitIdView commit_id,
                 std::function<void(Status, std::unique_ptr<const Commit>)>
                     callback) override;
  void StartCommit(
      const CommitId& commit_id,
      JournalType journal_type,
      std::function<void(Status, std::unique_ptr<Journal>)> callback) override;
  void StartMergeCommit(
      const CommitId& left,
      const CommitId& right,
      std::function<void(Status, std::unique_ptr<Journal>)> callback) override;
  void CommitJournal(
      std::unique_ptr<Journal> journal,
      std::function<void(Status, std::unique_ptr<const storage::Commit>)>
          callback) override;
  void RollbackJournal(std::unique_ptr<Journal> journal,
                       std::function<void(Status)> callback) override;
  Status AddCommitWatcher(CommitWatcher* watcher) override;
  Status RemoveCommitWatcher(CommitWatcher* watcher) override;
  void AddObjectFromLocal(
      std::unique_ptr<DataSource> data_source,
      std::function<void(Status, ObjectDigest)> callback) override;
  void GetObject(ObjectDigestView object_digest,
                 Location location,
                 std::function<void(Status, std::unique_ptr<const Object>)>
                     callback) override;
  void GetPiece(ObjectDigestView object_digest,
                std::function<void(Status, std::unique_ptr<const Object>)>
                    callback) override;
  void GetCommitContents(const Commit& commit,
                         std::string min_key,
                         std::function<bool(Entry)> on_next,
                         std::function<void(Status)> on_done) override;
  void GetEntryFromCommit(const Commit& commit,
                          std::string key,
                          std::function<void(Status, Entry)> callback) override;

  // For testing:
  void set_autocommit(bool autocommit) { autocommit_ = autocommit; }
  const std::map<std::string, std::unique_ptr<FakeJournalDelegate>>&
  GetJournals() const;
  const std::map<ObjectDigest, std::string, convert::StringViewComparator>&
  GetObjects() const;
  // Deletes this object from the fake local storage, but keeps it in its
  // "network" storage.
  void DeleteObjectFromLocal(const ObjectDigest& object_digest);
  // If set to true, no commit notification is sent to the commit watchers.
  void SetDropCommitNotifications(bool drop);

 private:
  void SendNextObject();

  bool autocommit_ = true;
  bool drop_commit_notifications_ = false;

  std::default_random_engine rng_;
  std::map<std::string, std::unique_ptr<FakeJournalDelegate>> journals_;
  std::map<ObjectDigest, std::string, convert::StringViewComparator> objects_;
  std::set<CommitId> heads_;
  std::set<CommitWatcher*> watchers_;
  std::vector<fxl::Closure> object_requests_;
  fsl::MessageLoop* message_loop_;
  PageId page_id_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakePageStorage);
};

}  // namespace fake
}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_FAKE_FAKE_PAGE_STORAGE_H_

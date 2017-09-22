// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_FAKE_FAKE_PAGE_STORAGE_H_
#define APPS_LEDGER_SRC_STORAGE_FAKE_FAKE_PAGE_STORAGE_H_

#include <map>
#include <random>
#include <string>
#include <vector>

#include "apps/ledger/src/storage/fake/fake_journal_delegate.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "apps/ledger/src/storage/test/page_storage_empty_impl.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"

namespace storage {
namespace fake {

class FakePageStorage : public test::PageStorageEmptyImpl {
 public:
  explicit FakePageStorage(PageId page_id);
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
      std::function<void(Status, ObjectId)> callback) override;
  void GetObject(ObjectIdView object_id,
                 Location location,
                 std::function<void(Status, std::unique_ptr<const Object>)>
                     callback) override;
  void GetPiece(ObjectIdView object_id,
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
  const std::map<ObjectId, std::string, convert::StringViewComparator>&
  GetObjects() const;
  // Deletes this object from the fake local storage, but keeps it in its
  // "network" storage.
  void DeleteObjectFromLocal(const ObjectId& object_id);

 private:
  void SendNextObject();

  bool autocommit_ = true;

  std::default_random_engine rng_;
  std::map<std::string, std::unique_ptr<FakeJournalDelegate>> journals_;
  std::map<ObjectId, std::string, convert::StringViewComparator> objects_;
  std::vector<fxl::Closure> object_requests_;
  PageId page_id_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakePageStorage);
};

}  // namespace fake
}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_FAKE_FAKE_PAGE_STORAGE_H_

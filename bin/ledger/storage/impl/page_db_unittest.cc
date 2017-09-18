// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/page_db.h"

#include <algorithm>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "apps/ledger/src/glue/crypto/rand.h"
#include "apps/ledger/src/storage/impl/commit_impl.h"
#include "apps/ledger/src/storage/impl/commit_random_impl.h"
#include "apps/ledger/src/storage/impl/journal_impl.h"
#include "apps/ledger/src/storage/impl/page_db_impl.h"
#include "apps/ledger/src/storage/impl/page_storage_impl.h"
#include "apps/ledger/src/storage/impl/storage_test_utils.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "apps/ledger/src/test/test_with_coroutines.h"
#include "gtest/gtest.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/macros.h"

namespace storage {
namespace {

using coroutine::CoroutineHandler;

void ExpectChangesEqual(const EntryChange& expected, const EntryChange& found) {
  EXPECT_EQ(expected.deleted, found.deleted);
  EXPECT_EQ(expected.entry.key, found.entry.key);
  if (!expected.deleted) {
    // If the entry is deleted, object_id and priority are not valid.
    EXPECT_EQ(expected.entry.object_id, found.entry.object_id);
    EXPECT_EQ(expected.entry.priority, found.entry.priority);
  }
}

class PageDbTest : public ::test::TestWithCoroutines {
 public:
  PageDbTest()
      : page_storage_(&coroutine_service_, tmp_dir_.path(), "page_id"),
        page_db_(tmp_dir_.path()) {}

  ~PageDbTest() override {}

  // Test:
  void SetUp() override {
    std::srand(0);

    Status status;
    page_storage_.Init(callback::Capture(MakeQuitTask(), &status));
    ASSERT_FALSE(RunLoopWithTimeout());
    ASSERT_EQ(Status::OK, status);

    ASSERT_EQ(Status::OK, page_db_.Init());
  }

 protected:
  files::ScopedTempDir tmp_dir_;
  PageStorageImpl page_storage_;
  PageDbImpl page_db_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageDbTest);
};

TEST_F(PageDbTest, HeadCommits) {
  EXPECT_TRUE(RunInCoroutine([&](CoroutineHandler* handler) {
    std::vector<CommitId> heads;
    EXPECT_EQ(Status::OK, page_db_.GetHeads(handler, &heads));
    EXPECT_TRUE(heads.empty());

    CommitId cid = RandomCommitId();
    EXPECT_EQ(Status::OK, page_db_.AddHead(handler, cid, glue::RandUint64()));
    EXPECT_EQ(Status::OK, page_db_.GetHeads(handler, &heads));
    EXPECT_EQ(1u, heads.size());
    EXPECT_EQ(cid, heads[0]);

    EXPECT_EQ(Status::OK, page_db_.RemoveHead(handler, cid));
    EXPECT_EQ(Status::OK, page_db_.GetHeads(handler, &heads));
    EXPECT_TRUE(heads.empty());
  }));
}

TEST_F(PageDbTest, OrderHeadCommitsByTimestamp) {
  EXPECT_TRUE(RunInCoroutine([&](CoroutineHandler* handler) {
    std::vector<int64_t> timestamps = {std::numeric_limits<int64_t>::min(),
                                       std::numeric_limits<int64_t>::max(), 0};

    for (size_t i = 0; i < 10; ++i) {
      int64_t ts;
      do {
        ts = glue::RandUint64();
      } while (std::find(timestamps.begin(), timestamps.end(), ts) !=
               timestamps.end());
      timestamps.push_back(ts);
    }

    auto sorted_timestamps = timestamps;
    std::sort(sorted_timestamps.begin(), sorted_timestamps.end());
    auto random_ordered_timestamps = timestamps;
    auto rng = std::default_random_engine(42);
    std::shuffle(random_ordered_timestamps.begin(),
                 random_ordered_timestamps.end(), rng);

    std::map<int64_t, CommitId> commits;
    for (auto ts : random_ordered_timestamps) {
      commits[ts] = RandomCommitId();
      EXPECT_EQ(Status::OK, page_db_.AddHead(handler, commits[ts], ts));
    }

    std::vector<CommitId> heads;
    EXPECT_EQ(Status::OK, page_db_.GetHeads(handler, &heads));
    EXPECT_EQ(timestamps.size(), heads.size());

    for (size_t i = 0; i < heads.size(); ++i) {
      EXPECT_EQ(commits[sorted_timestamps[i]], heads[i]);
    }
  }));
}

TEST_F(PageDbTest, Commits) {
  EXPECT_TRUE(RunInCoroutine([&](CoroutineHandler* handler) {
    std::vector<std::unique_ptr<const Commit>> parents;
    parents.emplace_back(new test::CommitRandomImpl());

    std::unique_ptr<Commit> stored_commit;
    std::string storage_bytes;
    std::unique_ptr<Commit> commit = CommitImpl::FromContentAndParents(
        &page_storage_, RandomObjectId(), std::move(parents));

    EXPECT_EQ(Status::NOT_FOUND, page_db_.GetCommitStorageBytes(
                                     handler, commit->GetId(), &storage_bytes));

    EXPECT_EQ(Status::OK,
              page_db_.AddCommitStorageBytes(handler, commit->GetId(),
                                             commit->GetStorageBytes()));
    EXPECT_EQ(Status::OK, page_db_.GetCommitStorageBytes(
                              handler, commit->GetId(), &storage_bytes));
    EXPECT_EQ(storage_bytes, commit->GetStorageBytes());

    EXPECT_EQ(Status::OK, page_db_.RemoveCommit(handler, commit->GetId()));
    EXPECT_EQ(Status::NOT_FOUND, page_db_.GetCommitStorageBytes(
                                     handler, commit->GetId(), &storage_bytes));
  }));
}

TEST_F(PageDbTest, Journals) {
  EXPECT_TRUE(RunInCoroutine([&](CoroutineHandler* handler) {
    CommitId commit_id = RandomCommitId();

    JournalId implicit_journal_id;
    JournalId explicit_journal_id;
    std::unique_ptr<Journal> explicit_journal;
    EXPECT_EQ(Status::OK,
              page_db_.CreateJournalId(handler, JournalType::IMPLICIT,
                                       commit_id, &implicit_journal_id));
    EXPECT_EQ(Status::OK,
              page_db_.CreateJournalId(handler, JournalType::EXPLICIT,
                                       commit_id, &explicit_journal_id));

    EXPECT_EQ(Status::OK, page_db_.RemoveExplicitJournals(handler));

    // Removing explicit journals should not affect the implicit ones.
    std::vector<JournalId> journal_ids;
    EXPECT_EQ(Status::OK,
              page_db_.GetImplicitJournalIds(handler, &journal_ids));
    ASSERT_EQ(1u, journal_ids.size());
    EXPECT_EQ(implicit_journal_id, journal_ids[0]);

    CommitId found_base_id;
    EXPECT_EQ(Status::OK, page_db_.GetBaseCommitForJournal(
                              handler, journal_ids[0], &found_base_id));
    EXPECT_EQ(commit_id, found_base_id);
    EXPECT_EQ(Status::OK, page_db_.RemoveJournal(journal_ids[0]));
    EXPECT_EQ(Status::NOT_FOUND, page_db_.GetBaseCommitForJournal(
                                     handler, journal_ids[0], &found_base_id));
    EXPECT_EQ(Status::OK,
              page_db_.GetImplicitJournalIds(handler, &journal_ids));
    EXPECT_EQ(0u, journal_ids.size());
  }));
}

TEST_F(PageDbTest, JournalEntries) {
  EXPECT_TRUE(RunInCoroutine([&](CoroutineHandler* handler) {
    CommitId commit_id = RandomCommitId();

    JournalId journal_id;
    EXPECT_EQ(Status::OK,
              page_db_.CreateJournalId(handler, JournalType::IMPLICIT,
                                       commit_id, &journal_id));
    EXPECT_EQ(Status::OK,
              page_db_.AddJournalEntry(journal_id, "add-key-1", "value1",
                                       KeyPriority::LAZY));
    EXPECT_EQ(Status::OK,
              page_db_.AddJournalEntry(journal_id, "add-key-2", "value2",
                                       KeyPriority::EAGER));
    EXPECT_EQ(Status::OK,
              page_db_.AddJournalEntry(journal_id, "add-key-1", "value3",
                                       KeyPriority::LAZY));
    EXPECT_EQ(Status::OK,
              page_db_.RemoveJournalEntry(journal_id, "remove-key"));

    EntryChange expected_changes[] = {
        NewEntryChange("add-key-1", "value3", KeyPriority::LAZY),
        NewEntryChange("add-key-2", "value2", KeyPriority::EAGER),
        NewRemoveEntryChange("remove-key"),
    };
    std::unique_ptr<Iterator<const EntryChange>> entries;
    EXPECT_EQ(Status::OK, page_db_.GetJournalEntries(journal_id, &entries));
    for (const auto& expected_change : expected_changes) {
      EXPECT_TRUE(entries->Valid());
      ExpectChangesEqual(expected_change, **entries);
      entries->Next();
    }
    EXPECT_FALSE(entries->Valid());
    EXPECT_EQ(Status::OK, entries->GetStatus());
  }));
}

TEST_F(PageDbTest, ObjectStorage) {
  EXPECT_TRUE(RunInCoroutine([&](CoroutineHandler* handler) {
    ObjectId object_id = RandomObjectId();
    std::string content = RandomString(32 * 1024);
    std::unique_ptr<const Object> object;
    PageDbObjectStatus object_status;

    EXPECT_EQ(Status::NOT_FOUND, page_db_.ReadObject(object_id, &object));
    ASSERT_EQ(Status::OK,
              page_db_.WriteObject(handler, object_id,
                                   DataSource::DataChunk::Create(content),
                                   PageDbObjectStatus::TRANSIENT));
    page_db_.GetObjectStatus(object_id, &object_status);
    EXPECT_EQ(PageDbObjectStatus::TRANSIENT, object_status);
    ASSERT_EQ(Status::OK, page_db_.ReadObject(object_id, &object));
    fxl::StringView object_content;
    EXPECT_EQ(Status::OK, object->GetData(&object_content));
    EXPECT_EQ(content, object_content);
    EXPECT_EQ(Status::OK, page_db_.DeleteObject(handler, object_id));
    EXPECT_EQ(Status::NOT_FOUND, page_db_.ReadObject(object_id, &object));
  }));
}

TEST_F(PageDbTest, UnsyncedCommits) {
  EXPECT_TRUE(RunInCoroutine([&](CoroutineHandler* handler) {
    CommitId commit_id = RandomCommitId();
    std::vector<CommitId> commit_ids;
    EXPECT_EQ(Status::OK, page_db_.GetUnsyncedCommitIds(handler, &commit_ids));
    EXPECT_TRUE(commit_ids.empty());

    EXPECT_EQ(Status::OK, page_db_.MarkCommitIdUnsynced(handler, commit_id, 0));
    EXPECT_EQ(Status::OK, page_db_.GetUnsyncedCommitIds(handler, &commit_ids));
    EXPECT_EQ(1u, commit_ids.size());
    EXPECT_EQ(commit_id, commit_ids[0]);
    bool is_synced;
    EXPECT_EQ(Status::OK,
              page_db_.IsCommitSynced(handler, commit_id, &is_synced));
    EXPECT_FALSE(is_synced);

    EXPECT_EQ(Status::OK, page_db_.MarkCommitIdSynced(handler, commit_id));
    EXPECT_EQ(Status::OK, page_db_.GetUnsyncedCommitIds(handler, &commit_ids));
    EXPECT_TRUE(commit_ids.empty());
    EXPECT_EQ(Status::OK,
              page_db_.IsCommitSynced(handler, commit_id, &is_synced));
    EXPECT_TRUE(is_synced);
  }));
}

TEST_F(PageDbTest, OrderUnsyncedCommitsByTimestamp) {
  EXPECT_TRUE(RunInCoroutine([&](CoroutineHandler* handler) {
    CommitId commit_ids[] = {RandomCommitId(), RandomCommitId(),
                             RandomCommitId()};
    // Add three unsynced commits with timestamps 200, 300 and 100.
    EXPECT_EQ(Status::OK,
              page_db_.MarkCommitIdUnsynced(handler, commit_ids[0], 200));
    EXPECT_EQ(Status::OK,
              page_db_.MarkCommitIdUnsynced(handler, commit_ids[1], 300));
    EXPECT_EQ(Status::OK,
              page_db_.MarkCommitIdUnsynced(handler, commit_ids[2], 100));

    // The result should be ordered by the given timestamps.
    std::vector<CommitId> found_ids;
    EXPECT_EQ(Status::OK, page_db_.GetUnsyncedCommitIds(handler, &found_ids));
    EXPECT_EQ(3u, found_ids.size());
    EXPECT_EQ(found_ids[0], commit_ids[2]);
    EXPECT_EQ(found_ids[1], commit_ids[0]);
    EXPECT_EQ(found_ids[2], commit_ids[1]);
  }));
}

TEST_F(PageDbTest, UnsyncedPieces) {
  EXPECT_TRUE(RunInCoroutine([&](CoroutineHandler* handler) {
    ObjectId object_id = RandomObjectId();
    std::vector<ObjectId> object_ids;
    EXPECT_EQ(Status::OK, page_db_.GetUnsyncedPieces(&object_ids));
    EXPECT_TRUE(object_ids.empty());

    EXPECT_EQ(Status::OK,
              page_db_.WriteObject(handler, object_id,
                                   DataSource::DataChunk::Create(""),
                                   PageDbObjectStatus::LOCAL));
    EXPECT_EQ(Status::OK, page_db_.SetObjectStatus(handler, object_id,
                                                   PageDbObjectStatus::LOCAL));
    EXPECT_EQ(Status::OK, page_db_.GetUnsyncedPieces(&object_ids));
    EXPECT_EQ(1u, object_ids.size());
    EXPECT_EQ(object_id, object_ids[0]);
    PageDbObjectStatus object_status;
    EXPECT_EQ(Status::OK, page_db_.GetObjectStatus(object_id, &object_status));
    EXPECT_EQ(PageDbObjectStatus::LOCAL, object_status);

    EXPECT_EQ(Status::OK, page_db_.SetObjectStatus(handler, object_id,
                                                   PageDbObjectStatus::SYNCED));
    EXPECT_EQ(Status::OK, page_db_.GetUnsyncedPieces(&object_ids));
    EXPECT_TRUE(object_ids.empty());
    EXPECT_EQ(Status::OK, page_db_.GetObjectStatus(object_id, &object_status));
    EXPECT_EQ(PageDbObjectStatus::SYNCED, object_status);
  }));
}

TEST_F(PageDbTest, Batch) {
  EXPECT_TRUE(RunInCoroutine([&](CoroutineHandler* handler) {
    std::unique_ptr<PageDb::Batch> batch = page_db_.StartBatch();

    ObjectId object_id = RandomObjectId();
    EXPECT_EQ(Status::OK, batch->WriteObject(handler, object_id,
                                             DataSource::DataChunk::Create(""),
                                             PageDbObjectStatus::LOCAL));

    std::vector<ObjectId> object_ids;
    EXPECT_EQ(Status::OK, page_db_.GetUnsyncedPieces(&object_ids));
    EXPECT_TRUE(object_ids.empty());

    EXPECT_EQ(Status::OK, batch->Execute());

    EXPECT_EQ(Status::OK, page_db_.GetUnsyncedPieces(&object_ids));
    EXPECT_EQ(1u, object_ids.size());
    EXPECT_EQ(object_id, object_ids[0]);
  }));
}

TEST_F(PageDbTest, PageDbObjectStatus) {
  EXPECT_TRUE(RunInCoroutine([&](CoroutineHandler* handler) {
    ObjectId object_id = RandomObjectId();
    PageDbObjectStatus object_status;

    ASSERT_EQ(Status::OK, page_db_.GetObjectStatus(object_id, &object_status));
    EXPECT_EQ(PageDbObjectStatus::UNKNOWN, object_status);

    PageDbObjectStatus initial_statuses[] = {PageDbObjectStatus::TRANSIENT,
                                             PageDbObjectStatus::LOCAL,
                                             PageDbObjectStatus::SYNCED};
    PageDbObjectStatus next_statuses[] = {PageDbObjectStatus::LOCAL,
                                          PageDbObjectStatus::SYNCED};
    for (auto initial_status : initial_statuses) {
      for (auto next_status : next_statuses) {
        ASSERT_EQ(Status::OK, page_db_.DeleteObject(handler, object_id));
        ASSERT_EQ(Status::OK,
                  page_db_.WriteObject(handler, object_id,
                                       DataSource::DataChunk::Create(""),
                                       initial_status));
        ASSERT_EQ(Status::OK,
                  page_db_.GetObjectStatus(object_id, &object_status));
        EXPECT_EQ(initial_status, object_status);
        ASSERT_EQ(Status::OK,
                  page_db_.SetObjectStatus(handler, object_id, next_status));

        PageDbObjectStatus expected_status =
            std::max(initial_status, next_status);
        ASSERT_EQ(Status::OK,
                  page_db_.GetObjectStatus(object_id, &object_status));
        EXPECT_EQ(expected_status, object_status);
      }
    }
  }));
}

TEST_F(PageDbTest, SyncMetadata) {
  EXPECT_TRUE(RunInCoroutine([&](CoroutineHandler* handler) {
    std::vector<std::pair<fxl::StringView, fxl::StringView>> keys_and_values = {
        {"foo1", "foo2"}, {"bar1", " bar2 "}};
    for (auto key_and_value : keys_and_values) {
      auto key = key_and_value.first;
      auto value = key_and_value.second;
      std::string returned_value;
      EXPECT_EQ(Status::NOT_FOUND,
                page_db_.GetSyncMetadata(key, &returned_value));

      EXPECT_EQ(Status::OK, page_db_.SetSyncMetadata(handler, key, value));
      EXPECT_EQ(Status::OK, page_db_.GetSyncMetadata(key, &returned_value));
      EXPECT_EQ(value, returned_value);
    }
  }));
}

}  // namespace
}  // namespace storage

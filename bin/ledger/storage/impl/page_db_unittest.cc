// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/page_db.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <lib/async/cpp/task.h>
#include <lib/callback/set_when_called.h>
#include <lib/fxl/macros.h>
#include <lib/zx/time.h>

#include "gtest/gtest.h"
#include "peridot/bin/ledger/encryption/fake/fake_encryption_service.h"
#include "peridot/bin/ledger/storage/impl/commit_impl.h"
#include "peridot/bin/ledger/storage/impl/commit_random_impl.h"
#include "peridot/bin/ledger/storage/impl/journal_impl.h"
#include "peridot/bin/ledger/storage/impl/leveldb.h"
#include "peridot/bin/ledger/storage/impl/page_db_impl.h"
#include "peridot/bin/ledger/storage/impl/page_storage_impl.h"
#include "peridot/bin/ledger/storage/impl/storage_test_utils.h"
#include "peridot/bin/ledger/storage/public/constants.h"
#include "peridot/bin/ledger/testing/test_with_environment.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"

namespace storage {
namespace {

using coroutine::CoroutineHandler;

void ExpectChangesEqual(const EntryChange& expected, const EntryChange& found) {
  EXPECT_EQ(expected.deleted, found.deleted);
  EXPECT_EQ(expected.entry.key, found.entry.key);
  if (!expected.deleted) {
    // If the entry is deleted, object_identifier and priority are not valid.
    EXPECT_EQ(expected.entry, found.entry);
  }
}

std::unique_ptr<LevelDb> GetLevelDb(async_dispatcher_t* dispatcher,
                                    ledger::DetachedPath db_path) {
  auto db = std::make_unique<LevelDb>(dispatcher, std::move(db_path));
  EXPECT_EQ(Status::OK, db->Init());
  return db;
}

class PageDbTest : public ledger::TestWithEnvironment {
 public:
  PageDbTest()
      : encryption_service_(dispatcher()),
        base_path(tmpfs_.root_fd()),
        page_storage_(&environment_, &encryption_service_,
                      GetLevelDb(dispatcher(), base_path.SubPath("storage")),
                      "page_id"),
        page_db_(&environment_,
                 GetLevelDb(dispatcher(), base_path.SubPath("page_db"))) {}

  ~PageDbTest() override {}

  // Test:
  void SetUp() override {
    std::srand(0);

    Status status;
    bool called;
    page_storage_.Init(
        callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    ASSERT_EQ(Status::OK, status);
  }

 protected:
  scoped_tmpfs::ScopedTmpFS tmpfs_;
  encryption::FakeEncryptionService encryption_service_;
  ledger::DetachedPath base_path;
  PageStorageImpl page_storage_;
  PageDbImpl page_db_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageDbTest);
};

TEST_F(PageDbTest, HeadCommits) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    std::vector<CommitId> heads;
    EXPECT_EQ(Status::OK, page_db_.GetHeads(handler, &heads));
    EXPECT_TRUE(heads.empty());

    CommitId cid = RandomCommitId(environment_.random());
    EXPECT_EQ(Status::OK,
              page_db_.AddHead(handler, cid,
                               environment_.random()->Draw<zx::time_utc>()));
    EXPECT_EQ(Status::OK, page_db_.GetHeads(handler, &heads));
    EXPECT_EQ(1u, heads.size());
    EXPECT_EQ(cid, heads[0]);

    EXPECT_EQ(Status::OK, page_db_.RemoveHead(handler, cid));
    EXPECT_EQ(Status::OK, page_db_.GetHeads(handler, &heads));
    EXPECT_TRUE(heads.empty());
  });
}

TEST_F(PageDbTest, OrderHeadCommitsByTimestamp) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    std::vector<zx::time_utc> timestamps = {zx::time_utc::infinite_past(),
                                            zx::time_utc::infinite(),
                                            zx::time_utc()};

    for (size_t i = 0; i < 10; ++i) {
      zx::time_utc ts;
      do {
        ts = environment_.random()->Draw<zx::time_utc>();
      } while (std::find(timestamps.begin(), timestamps.end(), ts) !=
               timestamps.end());
      timestamps.push_back(ts);
    }

    auto sorted_timestamps = timestamps;
    std::sort(sorted_timestamps.begin(), sorted_timestamps.end());
    auto random_ordered_timestamps = timestamps;
    auto rng = environment_.random()->NewBitGenerator<uint64_t>();
    std::shuffle(random_ordered_timestamps.begin(),
                 random_ordered_timestamps.end(), rng);

    std::map<zx::time_utc, CommitId> commits;
    for (auto ts : random_ordered_timestamps) {
      commits[ts] = RandomCommitId(environment_.random());
      EXPECT_EQ(Status::OK, page_db_.AddHead(handler, commits[ts], ts));
    }

    std::vector<CommitId> heads;
    EXPECT_EQ(Status::OK, page_db_.GetHeads(handler, &heads));
    EXPECT_EQ(timestamps.size(), heads.size());

    for (size_t i = 0; i < heads.size(); ++i) {
      EXPECT_EQ(commits[sorted_timestamps[i]], heads[i]);
    }
  });
}

TEST_F(PageDbTest, Commits) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    std::vector<std::unique_ptr<const Commit>> parents;
    parents.emplace_back(
        std::make_unique<CommitRandomImpl>(environment_.random()));

    std::unique_ptr<const Commit> commit = CommitImpl::FromContentAndParents(
        environment_.clock(), &page_storage_,
        RandomObjectIdentifier(environment_.random()), std::move(parents));

    std::string storage_bytes;
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
  });
}

TEST_F(PageDbTest, Journals) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    CommitId commit_id = RandomCommitId(environment_.random());

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
    EXPECT_EQ(Status::OK, page_db_.RemoveJournal(handler, journal_ids[0]));
    EXPECT_EQ(Status::NOT_FOUND, page_db_.GetBaseCommitForJournal(
                                     handler, journal_ids[0], &found_base_id));
    EXPECT_EQ(Status::OK,
              page_db_.GetImplicitJournalIds(handler, &journal_ids));
    EXPECT_EQ(0u, journal_ids.size());
  });
}

TEST_F(PageDbTest, JournalEntries) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    CommitId commit_id = RandomCommitId(environment_.random());

    JournalId journal_id;
    EXPECT_EQ(Status::OK,
              page_db_.CreateJournalId(handler, JournalType::IMPLICIT,
                                       commit_id, &journal_id));
    EXPECT_EQ(Status::OK,
              page_db_.AddJournalEntry(
                  handler, journal_id, "add-key-1",
                  encryption_service_.MakeObjectIdentifier("value1"),
                  KeyPriority::LAZY));
    EXPECT_EQ(Status::OK,
              page_db_.AddJournalEntry(
                  handler, journal_id, "add-key-2",
                  encryption_service_.MakeObjectIdentifier("value2"),
                  KeyPriority::EAGER));
    EXPECT_EQ(Status::OK,
              page_db_.AddJournalEntry(
                  handler, journal_id, "add-key-1",
                  encryption_service_.MakeObjectIdentifier("value3"),
                  KeyPriority::LAZY));
    EXPECT_EQ(Status::OK,
              page_db_.RemoveJournalEntry(handler, journal_id, "remove-key"));

    EntryChange expected_changes[] = {
        NewEntryChange("add-key-1", "value3", KeyPriority::LAZY),
        NewEntryChange("add-key-2", "value2", KeyPriority::EAGER),
        NewRemoveEntryChange("remove-key"),
    };
    std::unique_ptr<Iterator<const EntryChange>> entries;
    JournalContainsClearOperation contains_clear_operation;
    EXPECT_EQ(Status::OK,
              page_db_.GetJournalEntries(handler, journal_id, &entries,
                                         &contains_clear_operation));
    for (const auto& expected_change : expected_changes) {
      EXPECT_TRUE(entries->Valid());
      ExpectChangesEqual(expected_change, **entries);
      entries->Next();
    }
    EXPECT_FALSE(entries->Valid());
    EXPECT_EQ(JournalContainsClearOperation::NO, contains_clear_operation);
    EXPECT_EQ(Status::OK, entries->GetStatus());
  });
}

TEST_F(PageDbTest, JournalEntriesWithClear) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    CommitId commit_id = RandomCommitId(environment_.random());

    JournalId journal_id;
    EXPECT_EQ(Status::OK,
              page_db_.CreateJournalId(handler, JournalType::IMPLICIT,
                                       commit_id, &journal_id));
    EXPECT_EQ(Status::OK,
              page_db_.AddJournalEntry(
                  handler, journal_id, "add-key-1",
                  encryption_service_.MakeObjectIdentifier("value1"),
                  KeyPriority::LAZY));
    EXPECT_EQ(Status::OK, page_db_.EmptyJournalAndMarkContainsClearOperation(
                              handler, journal_id));
    EXPECT_EQ(Status::OK,
              page_db_.AddJournalEntry(
                  handler, journal_id, "add-key-2",
                  encryption_service_.MakeObjectIdentifier("value2"),
                  KeyPriority::EAGER));
    EXPECT_EQ(Status::OK,
              page_db_.RemoveJournalEntry(handler, journal_id, "remove-key"));

    EntryChange expected_changes[] = {
        NewEntryChange("add-key-2", "value2", KeyPriority::EAGER),
        NewRemoveEntryChange("remove-key"),
    };
    std::unique_ptr<Iterator<const EntryChange>> entries;
    JournalContainsClearOperation contains_clear_operation;
    EXPECT_EQ(Status::OK,
              page_db_.GetJournalEntries(handler, journal_id, &entries,
                                         &contains_clear_operation));
    for (const auto& expected_change : expected_changes) {
      EXPECT_TRUE(entries->Valid());
      ExpectChangesEqual(expected_change, **entries);
      entries->Next();
    }
    EXPECT_FALSE(entries->Valid());
    EXPECT_EQ(JournalContainsClearOperation::YES, contains_clear_operation);
    EXPECT_EQ(Status::OK, entries->GetStatus());
  });
}

TEST_F(PageDbTest, ObjectStorage) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    ObjectIdentifier object_identifier =
        RandomObjectIdentifier(environment_.random());
    std::string content = RandomString(environment_.random(), 32 * 1024);
    std::unique_ptr<const Object> object;
    PageDbObjectStatus object_status;

    EXPECT_EQ(Status::NOT_FOUND,
              page_db_.ReadObject(handler, object_identifier, &object));
    ASSERT_EQ(Status::OK,
              page_db_.WriteObject(handler, object_identifier,
                                   DataSource::DataChunk::Create(content),
                                   PageDbObjectStatus::TRANSIENT));
    page_db_.GetObjectStatus(handler, object_identifier, &object_status);
    EXPECT_EQ(PageDbObjectStatus::TRANSIENT, object_status);
    ASSERT_EQ(Status::OK,
              page_db_.ReadObject(handler, object_identifier, &object));
    fxl::StringView object_content;
    EXPECT_EQ(Status::OK, object->GetData(&object_content));
    EXPECT_EQ(content, object_content);
  });
}

TEST_F(PageDbTest, UnsyncedCommits) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    CommitId commit_id = RandomCommitId(environment_.random());
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
  });
}

TEST_F(PageDbTest, OrderUnsyncedCommitsByTimestamp) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    CommitId commit_ids[] = {RandomCommitId(environment_.random()),
                             RandomCommitId(environment_.random()),
                             RandomCommitId(environment_.random())};
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
  });
}

TEST_F(PageDbTest, UnsyncedPieces) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    auto object_identifier = RandomObjectIdentifier(environment_.random());
    std::vector<ObjectIdentifier> object_identifiers;
    EXPECT_EQ(Status::OK,
              page_db_.GetUnsyncedPieces(handler, &object_identifiers));
    EXPECT_TRUE(object_identifiers.empty());

    EXPECT_EQ(Status::OK,
              page_db_.WriteObject(handler, object_identifier,
                                   DataSource::DataChunk::Create(""),
                                   PageDbObjectStatus::LOCAL));
    EXPECT_EQ(Status::OK, page_db_.SetObjectStatus(handler, object_identifier,
                                                   PageDbObjectStatus::LOCAL));
    EXPECT_EQ(Status::OK,
              page_db_.GetUnsyncedPieces(handler, &object_identifiers));
    EXPECT_EQ(1u, object_identifiers.size());
    EXPECT_EQ(object_identifier, object_identifiers[0]);
    PageDbObjectStatus object_status;
    EXPECT_EQ(Status::OK, page_db_.GetObjectStatus(handler, object_identifier,
                                                   &object_status));
    EXPECT_EQ(PageDbObjectStatus::LOCAL, object_status);

    EXPECT_EQ(Status::OK, page_db_.SetObjectStatus(handler, object_identifier,
                                                   PageDbObjectStatus::SYNCED));
    EXPECT_EQ(Status::OK,
              page_db_.GetUnsyncedPieces(handler, &object_identifiers));
    EXPECT_TRUE(object_identifiers.empty());
    EXPECT_EQ(Status::OK, page_db_.GetObjectStatus(handler, object_identifier,
                                                   &object_status));
    EXPECT_EQ(PageDbObjectStatus::SYNCED, object_status);
  });
}

TEST_F(PageDbTest, Batch) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    std::unique_ptr<PageDb::Batch> batch;
    ASSERT_EQ(Status::OK, page_db_.StartBatch(handler, &batch));
    ASSERT_TRUE(batch);

    auto object_identifier = RandomObjectIdentifier(environment_.random());
    EXPECT_EQ(Status::OK, batch->WriteObject(handler, object_identifier,
                                             DataSource::DataChunk::Create(""),
                                             PageDbObjectStatus::LOCAL));

    std::vector<ObjectIdentifier> object_identifiers;
    EXPECT_EQ(Status::OK,
              page_db_.GetUnsyncedPieces(handler, &object_identifiers));
    EXPECT_TRUE(object_identifiers.empty());

    EXPECT_EQ(Status::OK, batch->Execute(handler));

    EXPECT_EQ(Status::OK,
              page_db_.GetUnsyncedPieces(handler, &object_identifiers));
    EXPECT_EQ(1u, object_identifiers.size());
    EXPECT_EQ(object_identifier, object_identifiers[0]);
  });
}

TEST_F(PageDbTest, PageDbObjectStatus) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    PageDbObjectStatus initial_statuses[] = {PageDbObjectStatus::TRANSIENT,
                                             PageDbObjectStatus::LOCAL,
                                             PageDbObjectStatus::SYNCED};
    PageDbObjectStatus next_statuses[] = {PageDbObjectStatus::LOCAL,
                                          PageDbObjectStatus::SYNCED};
    for (auto initial_status : initial_statuses) {
      for (auto next_status : next_statuses) {
        auto object_identifier = RandomObjectIdentifier(environment_.random());
        PageDbObjectStatus object_status;
        ASSERT_EQ(Status::OK, page_db_.GetObjectStatus(
                                  handler, object_identifier, &object_status));
        EXPECT_EQ(PageDbObjectStatus::UNKNOWN, object_status);
        ASSERT_EQ(Status::OK,
                  page_db_.WriteObject(handler, object_identifier,
                                       DataSource::DataChunk::Create(""),
                                       initial_status));
        ASSERT_EQ(Status::OK, page_db_.GetObjectStatus(
                                  handler, object_identifier, &object_status));
        EXPECT_EQ(initial_status, object_status);
        ASSERT_EQ(Status::OK, page_db_.SetObjectStatus(
                                  handler, object_identifier, next_status));

        PageDbObjectStatus expected_status =
            std::max(initial_status, next_status);
        ASSERT_EQ(Status::OK, page_db_.GetObjectStatus(
                                  handler, object_identifier, &object_status));
        EXPECT_EQ(expected_status, object_status);
      }
    }
  });
}

TEST_F(PageDbTest, SyncMetadata) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    std::vector<std::pair<fxl::StringView, fxl::StringView>> keys_and_values = {
        {"foo1", "foo2"}, {"bar1", " bar2 "}};
    for (const auto& key_and_value : keys_and_values) {
      auto key = key_and_value.first;
      auto value = key_and_value.second;
      std::string returned_value;
      EXPECT_EQ(Status::NOT_FOUND,
                page_db_.GetSyncMetadata(handler, key, &returned_value));

      EXPECT_EQ(Status::OK, page_db_.SetSyncMetadata(handler, key, value));
      EXPECT_EQ(Status::OK,
                page_db_.GetSyncMetadata(handler, key, &returned_value));
      EXPECT_EQ(value, returned_value);
    }
  });
}

TEST_F(PageDbTest, PageIsOnline) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    bool page_is_online;

    // Check that the initial state is not online.
    page_db_.IsPageOnline(handler, &page_is_online);
    EXPECT_FALSE(page_is_online);

    // Mark page as online and check it was updated.
    EXPECT_EQ(Status::OK, page_db_.MarkPageOnline(handler));
    page_db_.IsPageOnline(handler, &page_is_online);
    EXPECT_TRUE(page_is_online);
  });
}

// This test reproduces the crash of LE-451. The crash is due to a subtle
// ordering of coroutine execution that is exactly reproduced here.
TEST_F(PageDbTest, LE_451_ReproductionTest) {
  auto id = RandomObjectIdentifier(environment_.random());
  RunInCoroutine([&](CoroutineHandler* handler) {
    EXPECT_EQ(Status::OK, page_db_.WriteObject(
                              handler, id, DataSource::DataChunk::Create(""),
                              PageDbObjectStatus::LOCAL));
  });
  CoroutineHandler* handler1 = nullptr;
  CoroutineHandler* handler2 = nullptr;
  environment_.coroutine_service()->StartCoroutine(
      [&](CoroutineHandler* handler) {
        handler1 = handler;
        std::unique_ptr<PageDb::Batch> batch;
        EXPECT_EQ(Status::OK, page_db_.StartBatch(handler, &batch));
        EXPECT_EQ(Status::OK, batch->SetObjectStatus(
                                  handler, id, PageDbObjectStatus::SYNCED));
        if (handler->Yield() == coroutine::ContinuationStatus::INTERRUPTED) {
          return;
        }
        EXPECT_EQ(Status::OK, batch->Execute(handler));
        handler1 = nullptr;
      });
  environment_.coroutine_service()->StartCoroutine(
      [&](CoroutineHandler* handler) {
        handler2 = handler;
        std::unique_ptr<PageDb::Batch> batch;
        EXPECT_EQ(Status::OK, page_db_.StartBatch(handler, &batch));
        if (handler->Yield() == coroutine::ContinuationStatus::INTERRUPTED) {
          return;
        }
        EXPECT_EQ(Status::OK, batch->SetObjectStatus(
                                  handler, id, PageDbObjectStatus::LOCAL));
        EXPECT_EQ(Status::OK, batch->Execute(handler));
        handler2 = nullptr;
      });
  ASSERT_TRUE(handler1);
  ASSERT_TRUE(handler2);

  // Reach the 2 yield points.
  RunLoopUntilIdle();

  // Posting a task at this level ensures that the right interleaving between
  // reading and writing object status happens.
  async::PostTask(dispatcher(),
                  [&] { handler1->Resume(coroutine::ContinuationStatus::OK); });
  handler2->Resume(coroutine::ContinuationStatus::OK);

  // Finish the test.
  RunLoopUntilIdle();

  // Ensures both coroutines are terminated.
  ASSERT_FALSE(handler1);
  ASSERT_FALSE(handler2);
}

}  // namespace
}  // namespace storage

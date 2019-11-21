// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/public/db_unittest.h"

#include <lib/async/cpp/task.h>

#include "gmock/gmock.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {
namespace {
using ::coroutine::CoroutineHandler;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Pair;

//////////////////// Basic tests ///////////////////////////

TEST_P(DbTest, PutGet) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    std::unique_ptr<Db::Batch> batch;
    EXPECT_EQ(db_->StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->Put(handler, "key", "value"), Status::OK);
    EXPECT_EQ(batch->Execute(handler), Status::OK);

    std::string value;
    EXPECT_EQ(db_->Get(handler, "key", &value), Status::OK);
    EXPECT_EQ(value, "value");
  });
}

TEST_P(DbTest, HasKey) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    std::unique_ptr<Db::Batch> batch;
    EXPECT_EQ(db_->StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->Put(handler, "key", "value"), Status::OK);
    EXPECT_EQ(batch->Execute(handler), Status::OK);

    EXPECT_EQ(db_->HasKey(handler, "key"), Status::OK);
    EXPECT_EQ(db_->HasKey(handler, "key2"), Status::INTERNAL_NOT_FOUND);
  });
}

TEST_P(DbTest, HasPrefix) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    std::unique_ptr<Db::Batch> batch;
    EXPECT_EQ(db_->StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->Put(handler, "key", "value"), Status::OK);
    EXPECT_EQ(batch->Execute(handler), Status::OK);

    EXPECT_EQ(db_->HasPrefix(handler, ""), Status::OK);
    EXPECT_EQ(db_->HasPrefix(handler, "k"), Status::OK);
    EXPECT_EQ(db_->HasPrefix(handler, "ke"), Status::OK);
    EXPECT_EQ(db_->HasPrefix(handler, "key"), Status::OK);
    EXPECT_EQ(db_->HasPrefix(handler, "key2"), Status::INTERNAL_NOT_FOUND);
  });
}

TEST_P(DbTest, IteratorOperatesOnSnapshot) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    std::unique_ptr<Db::Batch> batch;
    // Add some keys.
    EXPECT_EQ(db_->StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->Put(handler, "before_key1", "value"), Status::OK);
    EXPECT_EQ(batch->Put(handler, "key1", "value1"), Status::OK);
    EXPECT_EQ(batch->Put(handler, "key2", "value2"), Status::OK);
    EXPECT_EQ(batch->Put(handler, "key3", "value3"), Status::OK);
    EXPECT_EQ(batch->Put(handler, "so_far_away_after_key3", "value"), Status::OK);
    EXPECT_EQ(batch->Execute(handler), Status::OK);

    std::unique_ptr<
        Iterator<const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>>
        iterator;
    // Start iterating over key1, key2 and key3.
    ASSERT_EQ(db_->GetIteratorAtPrefix(handler, "key", &iterator), Status::OK);
    EXPECT_EQ(iterator->GetStatus(), Status::OK);
    ASSERT_TRUE(iterator->Valid());
    EXPECT_THAT(**iterator, Pair(absl::string_view("key1"), absl::string_view("value1")));
    ASSERT_TRUE(iterator->Next().Valid());

    // Delete key1, key2 and key3.
    EXPECT_EQ(db_->HasPrefix(handler, "key"), Status::OK);
    EXPECT_EQ(db_->StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->Delete(handler, "key1"), Status::OK);
    EXPECT_EQ(batch->Delete(handler, "key2"), Status::OK);
    EXPECT_EQ(batch->Delete(handler, "key3"), Status::OK);
    EXPECT_EQ(batch->Execute(handler), Status::OK);
    EXPECT_EQ(db_->HasPrefix(handler, "key"), Status::INTERNAL_NOT_FOUND);

    // Continue the iteration. The iterator operates on a snapshot and is not invalidated by the
    // deletion.
    ASSERT_TRUE(iterator->Valid());
    EXPECT_THAT(**iterator, Pair(absl::string_view("key2"), absl::string_view("value2")));

    // Add key4.
    EXPECT_EQ(db_->StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->Put(handler, "key4", "value4"), Status::OK);
    EXPECT_EQ(batch->Execute(handler), Status::OK);
    EXPECT_EQ(db_->HasKey(handler, "key4"), Status::OK);

    // Complete the iteration. The iterator operates on a snapshot and does not see the insertion.
    ASSERT_TRUE(iterator->Next().Valid());
    EXPECT_THAT(**iterator, Pair(absl::string_view("key3"), absl::string_view("value3")));
    EXPECT_FALSE(iterator->Next().Valid());
  });
}

}  // namespace

void DbTest::PutEntry(const std::string& key, const std::string& value) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    std::unique_ptr<Db::Batch> batch;
    EXPECT_EQ(db_->StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->Put(handler, key, value), Status::OK);
    EXPECT_EQ(batch->Execute(handler), Status::OK);
  });
}

//////////////////// Read/Write ordering tests ///////////////////////////

void DbTest::RunReadWriteTest(fit::function<void(CoroutineHandler*)> do_read,
                              fit::function<void(CoroutineHandler*, Db::Batch*)> do_write) {
  CoroutineHandler* handler1 = nullptr;
  CoroutineHandler* handler2 = nullptr;
  bool read_issued = false;
  environment_.coroutine_service()->StartCoroutine([&](CoroutineHandler* handler) {
    handler1 = handler;
    if (handler->Yield() == coroutine::ContinuationStatus::INTERRUPTED) {
      return;
    }
    read_issued = true;
    do_read(handler);
    handler1 = nullptr;
  });
  environment_.coroutine_service()->StartCoroutine([&](CoroutineHandler* handler) {
    handler2 = handler;
    std::unique_ptr<Db::Batch> batch;
    EXPECT_EQ(db_->StartBatch(handler, &batch), Status::OK);
    do_write(handler, batch.get());
    if (handler->Yield() == coroutine::ContinuationStatus::INTERRUPTED) {
      return;
    }
    ASSERT_TRUE(read_issued);
    EXPECT_EQ(batch->Execute(handler), Status::OK);
    handler2 = nullptr;
  });
  ASSERT_TRUE(handler1);
  ASSERT_TRUE(handler2);

  // Reach the 2 yield points.
  RunLoopUntilIdle();

  // Posting a task at this level ensures that the read is issued before the write.
  async::PostTask(dispatcher(), [&] { handler2->Resume(coroutine::ContinuationStatus::OK); });
  handler1->Resume(coroutine::ContinuationStatus::OK);

  // Finish the test.
  RunLoopUntilIdle();

  // Ensure both coroutines are terminated.
  ASSERT_FALSE(handler1);
  ASSERT_FALSE(handler2);
}

namespace {

//////////////////// Read/Write ordering tests with write = put ///////////////////////////

TEST_P(DbTest, GetPutOrdering) {
  RunReadWriteTest(
      [this](CoroutineHandler* handler) {
        std::string value;
        EXPECT_EQ(db_->Get(handler, "key", &value), Status::INTERNAL_NOT_FOUND);
      },
      [](CoroutineHandler* handler, Db::Batch* batch) {
        EXPECT_EQ(batch->Put(handler, "key", "value"), Status::OK);
      });
}

TEST_P(DbTest, HasKeyPutOrdering) {
  RunReadWriteTest(
      [this](CoroutineHandler* handler) {
        EXPECT_EQ(db_->HasKey(handler, "key"), Status::INTERNAL_NOT_FOUND);
      },
      [](CoroutineHandler* handler, Db::Batch* batch) {
        EXPECT_EQ(batch->Put(handler, "key", "value"), Status::OK);
      });
}

TEST_P(DbTest, HasPrefixPutOrdering) {
  RunReadWriteTest(
      [this](CoroutineHandler* handler) {
        EXPECT_EQ(db_->HasPrefix(handler, "key"), Status::INTERNAL_NOT_FOUND);
      },
      [](CoroutineHandler* handler, Db::Batch* batch) {
        EXPECT_EQ(batch->Put(handler, "key", "value"), Status::OK);
      });
}

TEST_P(DbTest, GetObjectPutOrdering) {
  RunReadWriteTest(
      [this](CoroutineHandler* handler) {
        std::unique_ptr<const Piece> piece;
        EXPECT_EQ(db_->GetObject(handler, "key", ObjectIdentifier(), &piece),
                  Status::INTERNAL_NOT_FOUND);
      },
      [](CoroutineHandler* handler, Db::Batch* batch) {
        EXPECT_EQ(batch->Put(handler, "key", "value"), Status::OK);
      });
}

TEST_P(DbTest, GetByPrefixPutOrdering) {
  RunReadWriteTest(
      [this](CoroutineHandler* handler) {
        std::vector<std::string> key_suffixes;
        ASSERT_EQ(db_->GetByPrefix(handler, "key", &key_suffixes), Status::OK);
        EXPECT_THAT(key_suffixes, IsEmpty());
      },
      [](CoroutineHandler* handler, Db::Batch* batch) {
        EXPECT_EQ(batch->Put(handler, "key1", "value"), Status::OK);
      });
}

TEST_P(DbTest, GetEntriesByPrefixPutOrdering) {
  RunReadWriteTest(
      [this](CoroutineHandler* handler) {
        std::vector<std::pair<std::string, std::string>> entries;
        ASSERT_EQ(db_->GetEntriesByPrefix(handler, "key", &entries), Status::OK);
        EXPECT_THAT(entries, IsEmpty());
      },
      [](CoroutineHandler* handler, Db::Batch* batch) {
        EXPECT_EQ(batch->Put(handler, "key1", "value1"), Status::OK);
      });
}

TEST_P(DbTest, GetIteratorAtPrefixPutOrdering) {
  RunReadWriteTest(
      [this](CoroutineHandler* handler) {
        std::unique_ptr<
            Iterator<const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>>
            iterator;
        ASSERT_EQ(db_->GetIteratorAtPrefix(handler, "key", &iterator), Status::OK);
        EXPECT_EQ(iterator->GetStatus(), Status::OK);
        EXPECT_FALSE(iterator->Valid());
      },
      [](CoroutineHandler* handler, Db::Batch* batch) {
        EXPECT_EQ(batch->Put(handler, "key", "value"), Status::OK);
      });
}

//////////////////// Read/Write ordering tests with write = delete ///////////////////////////

TEST_P(DbTest, GetDeleteOrdering) {
  PutEntry("key", "value");
  RunReadWriteTest(
      [this](CoroutineHandler* handler) {
        std::string value;
        ASSERT_EQ(db_->Get(handler, "key", &value), Status::OK);
        EXPECT_EQ(value, "value");
      },
      [](CoroutineHandler* handler, Db::Batch* batch) {
        EXPECT_EQ(batch->Delete(handler, "key"), Status::OK);
      });
}

TEST_P(DbTest, HasKeyDeleteOrdering) {
  PutEntry("key", "value");
  RunReadWriteTest(
      [this](CoroutineHandler* handler) { EXPECT_EQ(db_->HasKey(handler, "key"), Status::OK); },
      [](CoroutineHandler* handler, Db::Batch* batch) {
        EXPECT_EQ(batch->Delete(handler, "key"), Status::OK);
      });
}

TEST_P(DbTest, HasPrefixDeleteOrdering) {
  PutEntry("key1", "value");
  RunReadWriteTest(
      [this](CoroutineHandler* handler) { EXPECT_EQ(db_->HasPrefix(handler, "key"), Status::OK); },
      [](CoroutineHandler* handler, Db::Batch* batch) {
        EXPECT_EQ(batch->Delete(handler, "key1"), Status::OK);
      });
}

TEST_P(DbTest, GetObjectDeleteOrdering) {
  PutEntry("key", "value");
  RunReadWriteTest(
      [this](CoroutineHandler* handler) {
        std::unique_ptr<const Piece> piece;
        ASSERT_EQ(db_->GetObject(handler, "key",
                                 ObjectIdentifier(1u, ObjectDigest("digest"), nullptr), &piece),
                  Status::OK);
        EXPECT_EQ(piece->GetIdentifier(), ObjectIdentifier(1u, ObjectDigest("digest"), nullptr));
        EXPECT_EQ(piece->GetData(), "value");
      },
      [](CoroutineHandler* handler, Db::Batch* batch) {
        EXPECT_EQ(batch->Delete(handler, "key"), Status::OK);
      });
}

TEST_P(DbTest, GetByPrefixDeleteOrdering) {
  PutEntry("key1", "value");
  PutEntry("key2", "value");
  RunReadWriteTest(
      [this](CoroutineHandler* handler) {
        std::vector<std::string> key_suffixes;
        ASSERT_EQ(db_->GetByPrefix(handler, "key", &key_suffixes), Status::OK);
        EXPECT_THAT(key_suffixes, ElementsAre("1", "2"));
      },
      [](CoroutineHandler* handler, Db::Batch* batch) {
        EXPECT_EQ(batch->Delete(handler, "key1"), Status::OK);
      });
}

TEST_P(DbTest, GetEntriesByPrefixDeleteOrdering) {
  PutEntry("key1", "value1");
  PutEntry("key2", "value2");
  RunReadWriteTest(
      [this](CoroutineHandler* handler) {
        std::vector<std::pair<std::string, std::string>> entries;
        ASSERT_EQ(db_->GetEntriesByPrefix(handler, "key", &entries), Status::OK);
        EXPECT_THAT(entries, ElementsAre(Pair("1", "value1"), Pair("2", "value2")));
      },
      [](CoroutineHandler* handler, Db::Batch* batch) {
        EXPECT_EQ(batch->Delete(handler, "key1"), Status::OK);
      });
}

TEST_P(DbTest, GetIteratorAtPrefixDeleteOrdering) {
  PutEntry("key1", "value1");
  PutEntry("key2", "value2");
  RunReadWriteTest(
      [this](CoroutineHandler* handler) {
        std::unique_ptr<
            Iterator<const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>>
            iterator;
        ASSERT_EQ(db_->GetIteratorAtPrefix(handler, "key", &iterator), Status::OK);
        EXPECT_EQ(iterator->GetStatus(), Status::OK);
        ASSERT_TRUE(iterator->Valid());
        EXPECT_THAT(**iterator, Pair(absl::string_view("key1"), absl::string_view("value1")));
        ASSERT_TRUE(iterator->Next().Valid());
        EXPECT_THAT(**iterator, Pair(absl::string_view("key2"), absl::string_view("value2")));
        EXPECT_FALSE(iterator->Next().Valid());
      },
      [](CoroutineHandler* handler, Db::Batch* batch) {
        EXPECT_EQ(batch->Delete(handler, "key1"), Status::OK);
        EXPECT_EQ(batch->Delete(handler, "key2"), Status::OK);
      });
}

}  // namespace

//////////////////// Write/Read ordering tests ///////////////////////////

void DbTest::RunWriteReadTest(fit::function<void(CoroutineHandler*, Db::Batch*)> do_write,
                              fit::function<void(CoroutineHandler*)> do_read) {
  CoroutineHandler* handler1 = nullptr;
  CoroutineHandler* handler2 = nullptr;
  bool write_issued = false;
  environment_.coroutine_service()->StartCoroutine([&](CoroutineHandler* handler) {
    handler1 = handler;
    std::unique_ptr<Db::Batch> batch;
    EXPECT_EQ(db_->StartBatch(handler, &batch), Status::OK);
    do_write(handler, batch.get());
    if (handler->Yield() == coroutine::ContinuationStatus::INTERRUPTED) {
      return;
    }
    write_issued = true;
    EXPECT_EQ(batch->Execute(handler), Status::OK);
    handler1 = nullptr;
  });
  environment_.coroutine_service()->StartCoroutine([&](CoroutineHandler* handler) {
    handler2 = handler;
    if (handler->Yield() == coroutine::ContinuationStatus::INTERRUPTED) {
      return;
    }
    ASSERT_TRUE(write_issued);
    do_read(handler);
    handler2 = nullptr;
  });
  ASSERT_TRUE(handler1);
  ASSERT_TRUE(handler2);

  // Reach the 2 yield points.
  RunLoopUntilIdle();

  // Posting a task at this level ensures that the write is issued before the read.
  async::PostTask(dispatcher(), [&] { handler2->Resume(coroutine::ContinuationStatus::OK); });
  handler1->Resume(coroutine::ContinuationStatus::OK);

  // Finish the test.
  RunLoopUntilIdle();

  // Ensure both coroutines are terminated.
  ASSERT_FALSE(handler1);
  ASSERT_FALSE(handler2);
}

namespace {

//////////////////// Write/Read ordering tests with write = put ///////////////////////////

TEST_P(DbTest, PutGetOrdering) {
  RunWriteReadTest(
      [](CoroutineHandler* handler, Db::Batch* batch) {
        EXPECT_EQ(batch->Put(handler, "key", "value"), Status::OK);
      },
      [this](CoroutineHandler* handler) {
        std::string value;
        ASSERT_EQ(db_->Get(handler, "key", &value), Status::OK);
        EXPECT_EQ(value, "value");
      });
}

TEST_P(DbTest, PutHasKeyOrdering) {
  RunWriteReadTest(
      [](CoroutineHandler* handler, Db::Batch* batch) {
        EXPECT_EQ(batch->Put(handler, "key", "value"), Status::OK);
      },
      [this](CoroutineHandler* handler) { EXPECT_EQ(db_->HasKey(handler, "key"), Status::OK); });
}

TEST_P(DbTest, PutHasPrefixOrdering) {
  RunWriteReadTest(
      [](CoroutineHandler* handler, Db::Batch* batch) {
        EXPECT_EQ(batch->Put(handler, "key", "value"), Status::OK);
      },
      [this](CoroutineHandler* handler) { EXPECT_EQ(db_->HasPrefix(handler, "key"), Status::OK); });
}

TEST_P(DbTest, PutGetObjectOrdering) {
  RunWriteReadTest(
      [](CoroutineHandler* handler, Db::Batch* batch) {
        EXPECT_EQ(batch->Put(handler, "key", "value"), Status::OK);
      },
      [this](CoroutineHandler* handler) {
        std::unique_ptr<const Piece> piece;
        ASSERT_EQ(db_->GetObject(handler, "key",
                                 ObjectIdentifier(1u, ObjectDigest("digest"), nullptr), &piece),
                  Status::OK);
        EXPECT_EQ(piece->GetIdentifier(), ObjectIdentifier(1u, ObjectDigest("digest"), nullptr));
        EXPECT_EQ(piece->GetData(), "value");
      });
}

TEST_P(DbTest, PutGetByPrefixOrdering) {
  RunWriteReadTest(
      [](CoroutineHandler* handler, Db::Batch* batch) {
        EXPECT_EQ(batch->Put(handler, "key1", "value"), Status::OK);
        EXPECT_EQ(batch->Put(handler, "key2", "value"), Status::OK);
      },
      [this](CoroutineHandler* handler) {
        std::vector<std::string> key_suffixes;
        ASSERT_EQ(db_->GetByPrefix(handler, "key", &key_suffixes), Status::OK);
        EXPECT_THAT(key_suffixes, ElementsAre("1", "2"));
      });
}

TEST_P(DbTest, PutGetEntriesByPrefixOrdering) {
  RunWriteReadTest(
      [](CoroutineHandler* handler, Db::Batch* batch) {
        EXPECT_EQ(batch->Put(handler, "key1", "value1"), Status::OK);
        EXPECT_EQ(batch->Put(handler, "key2", "value2"), Status::OK);
      },
      [this](CoroutineHandler* handler) {
        std::vector<std::pair<std::string, std::string>> entries;
        ASSERT_EQ(db_->GetEntriesByPrefix(handler, "key", &entries), Status::OK);
        EXPECT_THAT(entries, ElementsAre(Pair("1", "value1"), Pair("2", "value2")));
      });
}

TEST_P(DbTest, PutGetIteratorAtPrefixOrdering) {
  RunWriteReadTest(
      [](CoroutineHandler* handler, Db::Batch* batch) {
        EXPECT_EQ(batch->Put(handler, "key1", "value"), Status::OK);
      },
      [this](CoroutineHandler* handler) {
        std::unique_ptr<
            Iterator<const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>>
            iterator;
        ASSERT_EQ(db_->GetIteratorAtPrefix(handler, "key", &iterator), Status::OK);
        EXPECT_EQ(iterator->GetStatus(), Status::OK);
        ASSERT_TRUE(iterator->Valid());
        EXPECT_THAT(**iterator, Pair(absl::string_view("key1"), absl::string_view("value")));
        EXPECT_FALSE(iterator->Next().Valid());
      });
}

//////////////////// Write/Read ordering tests with write = delete ///////////////////////////

TEST_P(DbTest, DeleteGetOrdering) {
  PutEntry("key", "value");
  RunWriteReadTest([](CoroutineHandler* handler,
                      Db::Batch* batch) { EXPECT_EQ(batch->Delete(handler, "key"), Status::OK); },
                   [this](CoroutineHandler* handler) {
                     std::string value;
                     EXPECT_EQ(db_->Get(handler, "key", &value), Status::INTERNAL_NOT_FOUND);
                   });
}

TEST_P(DbTest, DeleteHasKeyOrdering) {
  PutEntry("key", "value");
  RunWriteReadTest([](CoroutineHandler* handler,
                      Db::Batch* batch) { EXPECT_EQ(batch->Delete(handler, "key"), Status::OK); },
                   [this](CoroutineHandler* handler) {
                     EXPECT_EQ(db_->HasKey(handler, "key"), Status::INTERNAL_NOT_FOUND);
                   });
}

TEST_P(DbTest, DeleteHasPrefixOrdering) {
  PutEntry("key", "value");
  RunWriteReadTest([](CoroutineHandler* handler,
                      Db::Batch* batch) { EXPECT_EQ(batch->Delete(handler, "key"), Status::OK); },
                   [this](CoroutineHandler* handler) {
                     EXPECT_EQ(db_->HasPrefix(handler, "key"), Status::INTERNAL_NOT_FOUND);
                   });
}

TEST_P(DbTest, DeleteGetObjectOrdering) {
  PutEntry("key", "value");
  RunWriteReadTest([](CoroutineHandler* handler,
                      Db::Batch* batch) { EXPECT_EQ(batch->Delete(handler, "key"), Status::OK); },
                   [this](CoroutineHandler* handler) {
                     std::unique_ptr<const Piece> piece;
                     EXPECT_EQ(db_->GetObject(handler, "key", ObjectIdentifier(), &piece),
                               Status::INTERNAL_NOT_FOUND);
                   });
}

TEST_P(DbTest, DeleteGetByPrefixOrdering) {
  PutEntry("key1", "value");
  PutEntry("key2", "value");
  RunWriteReadTest([](CoroutineHandler* handler,
                      Db::Batch* batch) { EXPECT_EQ(batch->Delete(handler, "key1"), Status::OK); },
                   [this](CoroutineHandler* handler) {
                     std::vector<std::string> key_suffixes;
                     ASSERT_EQ(db_->GetByPrefix(handler, "key", &key_suffixes), Status::OK);
                     EXPECT_THAT(key_suffixes, ElementsAre("2"));
                   });
}

TEST_P(DbTest, DeleteGetEntriesByPrefixOrdering) {
  PutEntry("key1", "value1");
  PutEntry("key2", "value2");
  RunWriteReadTest([](CoroutineHandler* handler,
                      Db::Batch* batch) { EXPECT_EQ(batch->Delete(handler, "key1"), Status::OK); },
                   [this](CoroutineHandler* handler) {
                     std::vector<std::pair<std::string, std::string>> entries;
                     ASSERT_EQ(db_->GetEntriesByPrefix(handler, "key", &entries), Status::OK);
                     EXPECT_THAT(entries, ElementsAre(Pair("2", "value2")));
                   });
}

TEST_P(DbTest, DeleteGetIteratorAtPrefixOrdering) {
  PutEntry("key", "value");
  RunWriteReadTest(
      [](CoroutineHandler* handler, Db::Batch* batch) {
        EXPECT_EQ(batch->Delete(handler, "key"), Status::OK);
      },
      [this](CoroutineHandler* handler) {
        std::unique_ptr<
            Iterator<const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>>
            iterator;
        ASSERT_EQ(db_->GetIteratorAtPrefix(handler, "key", &iterator), Status::OK);
        EXPECT_EQ(iterator->GetStatus(), Status::OK);
        EXPECT_FALSE(iterator->Valid());
      });
}

}  // namespace
}  // namespace storage

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/page_impl.h"

#include <algorithm>
#include <map>
#include <memory>

#include <lib/backoff/exponential_backoff.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fsl/socket/strings.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/strings/string_printf.h>
#include <lib/gtest/test_loop_fixture.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/app/fidl/serialization_size.h"
#include "peridot/bin/ledger/app/merging/merge_resolver.h"
#include "peridot/bin/ledger/app/page_manager.h"
#include "peridot/bin/ledger/coroutine/coroutine_impl.h"
#include "peridot/bin/ledger/storage/fake/fake_journal.h"
#include "peridot/bin/ledger/storage/fake/fake_journal_delegate.h"
#include "peridot/bin/ledger/storage/fake/fake_page_storage.h"
#include "peridot/bin/ledger/storage/testing/storage_matcher.h"
#include "peridot/bin/ledger/testing/test_with_environment.h"
#include "peridot/lib/convert/convert.h"

using testing::Contains;
using testing::ElementsAre;
using testing::IsEmpty;
using testing::Key;
using testing::Not;
using testing::Pair;
using testing::SizeIs;

namespace ledger {
namespace {

std::string ToString(const fuchsia::mem::BufferPtr& vmo) {
  std::string value;
  bool status = fsl::StringFromVmo(*vmo, &value);
  FXL_DCHECK(status);
  return value;
}

class PageImplTest : public TestWithEnvironment {
 public:
  PageImplTest() {}
  ~PageImplTest() override {}

 protected:
  // ApplicationTestBase:
  void SetUp() override {
    ::testing::Test::SetUp();
    page_id1_ = storage::PageId(kPageIdSize, 'a');
    auto fake_storage =
        std::make_unique<storage::fake::FakePageStorage>(page_id1_);
    fake_storage_ = fake_storage.get();
    auto resolver = std::make_unique<MergeResolver>(
        [] {}, &environment_, fake_storage_,
        std::make_unique<backoff::ExponentialBackoff>(zx::sec(0), 1u,
                                                      zx::sec(0)));
    resolver_ = resolver.get();

    manager_ = std::make_unique<PageManager>(
        &environment_, std::move(fake_storage), nullptr, std::move(resolver),
        PageManager::PageStorageState::NEEDS_SYNC);
    bool called;
    Status status;
    auto delaying_facade =
        std::make_unique<PageDelayingFacade>(page_id1_, page_ptr_.NewRequest());
    manager_->AddPageDelayingFacade(
        std::move(delaying_facade),
        callback::Capture(callback::SetWhenCalled(&called), &status));
    EXPECT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    DrainLoop();
  }

  // Run the message loop until there is nothing left to dispatch.
  void DrainLoop() {
    RunLoopRepeatedlyFor(storage::fake::kFakePageStorageDelay);
  }

  void CommitFirstPendingJournal(
      const std::map<std::string,
                     std::unique_ptr<storage::fake::FakeJournalDelegate>>&
          journals) {
    for (const auto& journal_pair : journals) {
      const auto& journal = journal_pair.second;
      if (!journal->IsCommitted() && !journal->IsRolledBack()) {
        journal->ResolvePendingCommit(storage::Status::OK);
        return;
      }
    }
  }

  storage::ObjectIdentifier AddObjectToStorage(std::string value_string) {
    bool called;
    storage::Status status;
    storage::ObjectIdentifier object_identifier;
    fake_storage_->AddObjectFromLocal(
        storage::DataSource::Create(std::move(value_string)),
        callback::Capture(callback::SetWhenCalled(&called), &status,
                          &object_identifier));
    DrainLoop();
    EXPECT_TRUE(called);
    EXPECT_EQ(storage::Status::OK, status);
    return object_identifier;
  }

  std::unique_ptr<const storage::Object> AddObject(const std::string& value) {
    storage::ObjectIdentifier object_identifier = AddObjectToStorage(value);

    bool called;
    storage::Status status;
    std::unique_ptr<const storage::Object> object;
    fake_storage_->GetObject(
        object_identifier, storage::PageStorage::Location::LOCAL,
        callback::Capture(callback::SetWhenCalled(&called), &status, &object));
    DrainLoop();
    EXPECT_TRUE(called);
    EXPECT_EQ(storage::Status::OK, status);
    return object;
  }

  std::string GetKey(size_t index, size_t min_key_size = 0u) {
    std::string result = fxl::StringPrintf("key %04" PRIuMAX, index);
    result.resize(std::max(result.size(), min_key_size));
    return result;
  }

  std::string GetValue(size_t index, size_t min_value_size = 0u) {
    std::string result = fxl::StringPrintf("val %zu", index);
    result.resize(std::max(result.size(), min_value_size));
    return result;
  }

  void AddEntries(int entry_count, size_t min_key_size = 0u,
                  size_t min_value_size = 0u) {
    FXL_DCHECK(entry_count <= 10000);
    bool called;
    Status status;
    page_ptr_->StartTransaction(
        callback::Capture(callback::SetWhenCalled(&called), &status));
    DrainLoop();
    EXPECT_TRUE(called);
    EXPECT_EQ(Status::OK, status);

    for (int i = 0; i < entry_count; ++i) {
      page_ptr_->Put(
          convert::ToArray(GetKey(i, min_key_size)),
          convert::ToArray(GetValue(i, min_value_size)),
          callback::Capture(callback::SetWhenCalled(&called), &status));
      DrainLoop();
      EXPECT_TRUE(called);
      EXPECT_EQ(Status::OK, status);
    }
    page_ptr_->Commit(
        callback::Capture(callback::SetWhenCalled(&called), &status));
    DrainLoop();
    EXPECT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
  }

  PageSnapshotPtr GetSnapshot(
      fidl::VectorPtr<uint8_t> prefix = fidl::VectorPtr<uint8_t>::New(0)) {
    bool called;
    Status status;
    PageSnapshotPtr snapshot;
    page_ptr_->GetSnapshot(
        snapshot.NewRequest(), std::move(prefix), nullptr,
        callback::Capture(callback::SetWhenCalled(&called), &status));
    DrainLoop();
    EXPECT_TRUE(called);
    EXPECT_EQ(status, Status::OK);
    return snapshot;
  }

  storage::PageId page_id1_;
  storage::fake::FakePageStorage* fake_storage_;
  std::unique_ptr<PageManager> manager_;
  MergeResolver* resolver_;

  PagePtr page_ptr_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageImplTest);
};

TEST_F(PageImplTest, GetId) {
  bool called;
  ledger::PageId page_id;
  page_ptr_->GetId(
      callback::Capture(callback::SetWhenCalled(&called), &page_id));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(page_id1_, convert::ToString(page_id.id));
}

TEST_F(PageImplTest, PutNoTransaction) {
  std::string key("some_key");
  std::string value("a small value");
  bool called;
  Status status;
  page_ptr_->Put(convert::ToArray(key), convert::ToArray(value),
                 callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  auto objects = fake_storage_->GetObjects();
  EXPECT_EQ(1u, objects.size());
  storage::ObjectIdentifier object_identifier = objects.begin()->first;
  std::string actual_value = objects.begin()->second;
  EXPECT_EQ(value, actual_value);

  const std::map<std::string,
                 std::unique_ptr<storage::fake::FakeJournalDelegate>>&
      journals = fake_storage_->GetJournals();
  EXPECT_EQ(1u, journals.size());
  auto it = journals.begin();
  EXPECT_TRUE(it->second->IsCommitted());
  EXPECT_EQ(1u, it->second->GetData().size());
  storage::Entry entry = it->second->GetData().at(key);
  EXPECT_EQ(object_identifier, entry.object_identifier);
  EXPECT_EQ(storage::KeyPriority::EAGER, entry.priority);
}

TEST_F(PageImplTest, PutReferenceNoTransaction) {
  std::string object_data("some_data");
  fsl::SizedVmo vmo;
  ASSERT_TRUE(fsl::VmoFromString(object_data, &vmo));

  bool called;
  Status status;
  ReferencePtr reference;
  page_ptr_->CreateReferenceFromBuffer(
      std::move(vmo).ToTransport(),
      callback::Capture(callback::SetWhenCalled(&called), &status, &reference));
  DrainLoop();

  ASSERT_TRUE(called);
  ASSERT_EQ(Status::OK, status);

  std::string key("some_key");
  page_ptr_->PutReference(
      convert::ToArray(key), std::move(*reference), Priority::LAZY,
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();

  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  auto objects = fake_storage_->GetObjects();
  // No object should have been added.
  EXPECT_EQ(1u, objects.size());

  const std::map<std::string,
                 std::unique_ptr<storage::fake::FakeJournalDelegate>>&
      journals = fake_storage_->GetJournals();
  EXPECT_EQ(1u, journals.size());
  auto it = journals.begin();
  EXPECT_TRUE(it->second->IsCommitted());
  EXPECT_EQ(1u, it->second->GetData().size());
  storage::Entry entry = it->second->GetData().at(key);
  std::unique_ptr<const storage::Object> object = AddObject(object_data);
  EXPECT_EQ(object->GetIdentifier().object_digest,
            entry.object_identifier.object_digest);
  EXPECT_EQ(storage::KeyPriority::LAZY, entry.priority);
}

TEST_F(PageImplTest, PutUnknownReference) {
  std::string key("some_key");
  ReferencePtr reference = Reference::New();
  reference->opaque_id = convert::ToArray("12345678");

  bool called;
  Status status;
  page_ptr_->PutReference(
      convert::ToArray(key), std::move(*reference), Priority::LAZY,
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::REFERENCE_NOT_FOUND, status);
  auto objects = fake_storage_->GetObjects();
  // No object should have been added.
  EXPECT_EQ(0u, objects.size());

  const std::map<std::string,
                 std::unique_ptr<storage::fake::FakeJournalDelegate>>&
      journals = fake_storage_->GetJournals();
  EXPECT_EQ(0u, journals.size());
}

TEST_F(PageImplTest, PutKeyTooLarge) {
  std::string value("a small value");

  zx::channel writer, reader;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &writer, &reader));
  page_ptr_.Bind(std::move(writer));

  // Key too large; message doesn't go through, failing on validation.
  const size_t key_size = kMaxKeySize + 1;
  std::string key = GetKey(1, key_size);
  page_ptr_->Put(convert::ToArray(key), convert::ToArray(value),
                 [](Status status) {});
  zx_status_t status = reader.read(0, nullptr, 0, nullptr, nullptr, 0, nullptr);
  DrainLoop();
  EXPECT_EQ(ZX_ERR_SHOULD_WAIT, status);

  // With a smaller key, message goes through.
  key = GetKey(1, kMaxKeySize);
  page_ptr_->Put(convert::ToArray(key), convert::ToArray(value),
                 [](Status status) {});
  status = reader.read(0, nullptr, 0, nullptr, nullptr, 0, nullptr);
  DrainLoop();
  EXPECT_EQ(ZX_ERR_BUFFER_TOO_SMALL, status);
}

TEST_F(PageImplTest, PutReferenceKeyTooLarge) {
  std::string object_data("some_data");
  fsl::SizedVmo vmo;
  ASSERT_TRUE(fsl::VmoFromString(object_data, &vmo));

  bool called;
  Status reference_status;
  ReferencePtr reference;
  page_ptr_->CreateReferenceFromBuffer(
      std::move(vmo).ToTransport(),
      callback::Capture(callback::SetWhenCalled(&called), &reference_status,
                        &reference));
  DrainLoop();
  ASSERT_EQ(Status::OK, reference_status);

  zx::channel writer, reader;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &writer, &reader));
  page_ptr_.Bind(std::move(writer));

  // Key too large; message doesn't go through, failing on validation.
  const size_t key_size = kMaxKeySize + 1;
  std::string key = GetKey(1, key_size);
  page_ptr_->PutReference(convert::ToArray(key), fidl::Clone(*reference),
                          Priority::EAGER, [](Status status) {});
  zx_status_t status = reader.read(0, nullptr, 0, nullptr, nullptr, 0, nullptr);
  DrainLoop();
  EXPECT_EQ(ZX_ERR_SHOULD_WAIT, status);

  // With a smaller key, message goes through.
  key = GetKey(1, kMaxKeySize);
  page_ptr_->PutReference(convert::ToArray(key), std::move(*reference),
                          Priority::EAGER, [](Status status) {});
  status = reader.read(0, nullptr, 0, nullptr, nullptr, 0, nullptr);
  DrainLoop();
  EXPECT_EQ(ZX_ERR_BUFFER_TOO_SMALL, status);
}

TEST_F(PageImplTest, DeleteNoTransaction) {
  std::string key("some_key");

  bool called;
  Status status;
  page_ptr_->Delete(
      convert::ToArray(key),
      callback::Capture(callback::SetWhenCalled(&called), &status));

  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  auto objects = fake_storage_->GetObjects();
  // No object should have been added.
  EXPECT_EQ(0u, objects.size());

  const std::map<std::string,
                 std::unique_ptr<storage::fake::FakeJournalDelegate>>&
      journals = fake_storage_->GetJournals();
  EXPECT_EQ(1u, journals.size());
  auto it = journals.begin();
  EXPECT_TRUE(it->second->IsCommitted());
  EXPECT_THAT(it->second->GetData(), IsEmpty());
}

TEST_F(PageImplTest, ClearNoTransaction) {
  bool called;
  Status status;
  page_ptr_->Clear(
      callback::Capture(callback::SetWhenCalled(&called), &status));

  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  auto objects = fake_storage_->GetObjects();
  // No object should have been added.
  EXPECT_THAT(objects, IsEmpty());

  const std::map<std::string,
                 std::unique_ptr<storage::fake::FakeJournalDelegate>>&
      journals = fake_storage_->GetJournals();
  EXPECT_EQ(1u, journals.size());
  auto it = journals.begin();
  EXPECT_TRUE(it->second->IsCommitted());
  EXPECT_THAT(it->second->GetData(), IsEmpty());
}

TEST_F(PageImplTest, TransactionCommit) {
  std::string key1("some_key1");
  storage::ObjectDigest object_digest1;
  std::string value("a small value");

  std::string key2("some_key2");
  std::string value2("another value");

  fsl::SizedVmo vmo;
  ASSERT_TRUE(fsl::VmoFromString(value2, &vmo));

  bool called;
  Status status;
  ReferencePtr reference;
  page_ptr_->CreateReferenceFromBuffer(
      std::move(vmo).ToTransport(),
      callback::Capture(callback::SetWhenCalled(&called), &status, &reference));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(Status::OK, status);

  // Sequence of operations:
  //  - StartTransaction
  //  - Put
  //  - PutReference
  //  - Delete
  //  - Commit
  page_ptr_->StartTransaction(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  page_ptr_->Put(convert::ToArray(key1), convert::ToArray(value),
                 callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();

  {
    EXPECT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    auto objects = fake_storage_->GetObjects();
    EXPECT_EQ(2u, objects.size());
    // Objects are ordered by a randomly assigned object id, so we can't know
    // the correct possition of the value in the map.
    bool object_found = false;
    for (const auto& object : objects) {
      if (object.second == value) {
        object_found = true;
        object_digest1 = object.first.object_digest;
        break;
      }
    }
    EXPECT_TRUE(object_found);

    // No finished commit yet.
    const std::map<std::string,
                   std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    auto it = journals.begin();
    EXPECT_FALSE(it->second->IsCommitted());
    EXPECT_EQ(1u, it->second->GetData().size());
    storage::Entry entry = it->second->GetData().at(key1);
    EXPECT_EQ(object_digest1, entry.object_identifier.object_digest);
    EXPECT_EQ(storage::KeyPriority::EAGER, entry.priority);
  }

  page_ptr_->PutReference(
      convert::ToArray(key2), std::move(*reference), Priority::LAZY,
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();

  {
    EXPECT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(2u, fake_storage_->GetObjects().size());

    // No finished commit yet, with now two entries.
    const std::map<std::string,
                   std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    auto it = journals.begin();
    EXPECT_FALSE(it->second->IsCommitted());
    EXPECT_EQ(2u, it->second->GetData().size());
    storage::Entry entry = it->second->GetData().at(key2);
    EXPECT_EQ(AddObject(value2)->GetIdentifier().object_digest,
              entry.object_identifier.object_digest);
    EXPECT_EQ(storage::KeyPriority::LAZY, entry.priority);
  }

  page_ptr_->Delete(
      convert::ToArray(key2),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();

  {
    EXPECT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(2u, fake_storage_->GetObjects().size());

    // No finished commit yet, with the second entry deleted.
    const std::map<std::string,
                   std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    auto it = journals.begin();
    EXPECT_FALSE(it->second->IsCommitted());
    EXPECT_EQ(1u, it->second->GetData().size());
    EXPECT_THAT(it->second->GetData(), Not(Contains(Key(key2))));
  }

  page_ptr_->Commit(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();

  {
    EXPECT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(2u, fake_storage_->GetObjects().size());

    const std::map<std::string,
                   std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    auto it = journals.begin();
    EXPECT_TRUE(it->second->IsCommitted());
    EXPECT_EQ(1u, it->second->GetData().size());
  }
}

TEST_F(PageImplTest, TransactionClearCommit) {
  std::string key1("some_key1");
  std::string value1("a small value");

  std::string key2("some_key2");
  std::string value2("another value");
  storage::ObjectDigest object_digest2;

  bool called;
  Status status;

  // Sequence of operations:
  //  - Put key1
  //  - StartTransaction
  //  - Clear
  //  - Put key2
  //  - Commit

  page_ptr_->Put(convert::ToArray(key1), convert::ToArray(value1),
                 callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  page_ptr_->StartTransaction(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();

  {
    EXPECT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
  }

  const std::map<std::string,
                 std::unique_ptr<storage::fake::FakeJournalDelegate>>&
      journals = fake_storage_->GetJournals();
  EXPECT_EQ(2u, journals.size());
  const auto& journal_it = std::find_if(
      journals.begin(), journals.end(),
      [](const auto& pair) { return !pair.second->IsCommitted(); });
  EXPECT_NE(journals.end(), journal_it);
  const auto& journal = journal_it->second;

  {
    EXPECT_FALSE(journal->IsCommitted());
    EXPECT_THAT(journal->GetData(), SizeIs(1));
  }

  page_ptr_->Clear(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();

  {
    EXPECT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(1u, fake_storage_->GetObjects().size());

    EXPECT_FALSE(journal->IsCommitted());
    EXPECT_THAT(journal->GetData(), IsEmpty());
  }

  page_ptr_->Put(convert::ToArray(key2), convert::ToArray(value2),
                 callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();

  {
    EXPECT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    auto objects = fake_storage_->GetObjects();
    EXPECT_EQ(2u, objects.size());
    bool object_found = false;
    for (const auto& object : objects) {
      if (object.second == value2) {
        object_found = true;
        object_digest2 = object.first.object_digest;
        break;
      }
    }
    EXPECT_TRUE(object_found);

    // No finished commit yet.
    const std::map<std::string,
                   std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_THAT(journals, SizeIs(2));
    EXPECT_FALSE(journal->IsCommitted());
    EXPECT_THAT(journal->GetData(),
                ElementsAre(Pair(
                    key2, storage::EntryMatches(
                              {key2, storage::DigestMatches(object_digest2),
                               storage::KeyPriority::EAGER}))));
  }

  page_ptr_->Commit(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();

  {
    EXPECT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(2u, fake_storage_->GetObjects().size());

    const std::map<std::string,
                   std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_THAT(journals, SizeIs(2));
    EXPECT_TRUE(journal->IsCommitted());
    EXPECT_THAT(journal->GetData(),
                ElementsAre(Pair(
                    key2, storage::EntryMatches(
                              {key2, storage::DigestMatches(object_digest2),
                               storage::KeyPriority::EAGER}))));
  }
}

TEST_F(PageImplTest, TransactionRollback) {
  // Sequence of operations:
  //  - StartTransaction
  //  - Rollback
  bool called;
  Status status;
  page_ptr_->StartTransaction(
      [](Status status) { EXPECT_EQ(Status::OK, status); });
  page_ptr_->Rollback(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(0u, fake_storage_->GetObjects().size());

  // Only one journal, rollbacked.
  const std::map<std::string,
                 std::unique_ptr<storage::fake::FakeJournalDelegate>>&
      journals = fake_storage_->GetJournals();
  EXPECT_EQ(1u, journals.size());
  auto it = journals.begin();
  EXPECT_TRUE(it->second->IsRolledBack());
  EXPECT_EQ(0u, it->second->GetData().size());
}

TEST_F(PageImplTest, NoTwoTransactions) {
  // Sequence of operations:
  //  - StartTransaction
  //  - StartTransaction
  page_ptr_->StartTransaction(
      [](Status status) { EXPECT_EQ(Status::OK, status); });
  bool called;
  Status status;
  page_ptr_->StartTransaction(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::TRANSACTION_ALREADY_IN_PROGRESS, status);
}

TEST_F(PageImplTest, NoTransactionCommit) {
  // Sequence of operations:
  //  - Commit
  bool called;
  Status status;
  page_ptr_->Commit(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::NO_TRANSACTION_IN_PROGRESS, status);
}

TEST_F(PageImplTest, NoTransactionRollback) {
  // Sequence of operations:
  //  - Rollback
  bool called;
  Status status;
  page_ptr_->Rollback(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::NO_TRANSACTION_IN_PROGRESS, status);
}

TEST_F(PageImplTest, CreateReferenceFromSocket) {
  ASSERT_EQ(0u, fake_storage_->GetObjects().size());

  std::string value("a small value");
  bool called;
  Status status;
  ReferencePtr reference;
  page_ptr_->CreateReferenceFromSocket(
      value.size(), fsl::WriteStringToSocket(value),
      callback::Capture(callback::SetWhenCalled(&called), &status, &reference));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  ASSERT_EQ(1u, fake_storage_->GetObjects().size());
  ASSERT_EQ(value, fake_storage_->GetObjects().begin()->second);
}

TEST_F(PageImplTest, CreateReferenceFromBuffer) {
  ASSERT_EQ(0u, fake_storage_->GetObjects().size());

  std::string value("a small value");
  fsl::SizedVmo vmo;
  ASSERT_TRUE(fsl::VmoFromString(value, &vmo));

  bool called;
  Status status;
  ReferencePtr reference;
  page_ptr_->CreateReferenceFromBuffer(
      std::move(vmo).ToTransport(),
      callback::Capture(callback::SetWhenCalled(&called), &status, &reference));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  ASSERT_EQ(1u, fake_storage_->GetObjects().size());
  ASSERT_EQ(value, fake_storage_->GetObjects().begin()->second);
}

TEST_F(PageImplTest, PutGetSnapshotGetEntries) {
  std::string eager_key("a_key");
  std::string eager_value("an eager value");
  std::string lazy_key("another_key");
  std::string lazy_value("a lazy value");

  bool called;
  Status status;
  page_ptr_->Put(convert::ToArray(eager_key), convert::ToArray(eager_value),
                 callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  status = Status::UNKNOWN_ERROR;
  page_ptr_->PutWithPriority(
      convert::ToArray(lazy_key), convert::ToArray(lazy_value), Priority::LAZY,
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  PageSnapshotPtr snapshot = GetSnapshot();

  fidl::VectorPtr<Entry> actual_entries;
  std::unique_ptr<Token> next_token;
  snapshot->GetEntries(
      fidl::VectorPtr<uint8_t>::New(0), nullptr,
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &actual_entries, &next_token));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(next_token);
  ASSERT_EQ(2u, actual_entries->size());
  EXPECT_EQ(eager_key, convert::ExtendedStringView(actual_entries->at(0).key));
  EXPECT_EQ(eager_value, ToString(actual_entries->at(0).value));
  EXPECT_EQ(Priority::EAGER, actual_entries->at(0).priority);

  EXPECT_EQ(lazy_key, convert::ExtendedStringView(actual_entries->at(1).key));
  EXPECT_EQ(lazy_value, ToString(actual_entries->at(1).value));
  EXPECT_EQ(Priority::LAZY, actual_entries->at(1).priority);
}

TEST_F(PageImplTest, PutGetSnapshotGetEntriesInline) {
  std::string eager_key("a_key");
  std::string eager_value("an eager value");
  std::string lazy_key("another_key");
  std::string lazy_value("a lazy value");

  bool called;
  Status status;
  page_ptr_->Put(convert::ToArray(eager_key), convert::ToArray(eager_value),
                 callback::Capture(callback::SetWhenCalled(&called), &status));

  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  page_ptr_->PutWithPriority(
      convert::ToArray(lazy_key), convert::ToArray(lazy_value), Priority::LAZY,
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  PageSnapshotPtr snapshot = GetSnapshot();

  std::unique_ptr<Token> next_token;
  fidl::VectorPtr<InlinedEntry> actual_entries;
  snapshot->GetEntriesInline(
      fidl::VectorPtr<uint8_t>::New(0), nullptr,
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &actual_entries, &next_token));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(next_token);

  ASSERT_EQ(2u, actual_entries->size());
  EXPECT_EQ(eager_key, convert::ExtendedStringView(actual_entries->at(0).key));
  EXPECT_TRUE(actual_entries->at(0).inlined_value);
  EXPECT_EQ(eager_value,
            convert::ToString(actual_entries->at(0).inlined_value->value));
  EXPECT_EQ(Priority::EAGER, actual_entries->at(0).priority);

  EXPECT_EQ(lazy_key, convert::ExtendedStringView(actual_entries->at(1).key));
  EXPECT_TRUE(actual_entries->at(1).inlined_value);
  EXPECT_EQ(lazy_value,
            convert::ToString(actual_entries->at(1).inlined_value->value));
  EXPECT_EQ(Priority::LAZY, actual_entries->at(1).priority);
}

TEST_F(PageImplTest, PutGetSnapshotGetEntriesWithTokenForSize) {
  const size_t min_key_size = kMaxKeySize;
  // Put enough entries to ensure pagination of the result.
  // The number of entries in a Page is bounded by the maximum number of
  // handles, and the size of a fidl message (which cannot exceed
  // |kMaxInlineDataSize|), so we put one entry more than that.
  const size_t entry_count =
      std::min(fidl_serialization::kMaxMessageHandles,
               (fidl_serialization::kMaxInlineDataSize -
                fidl_serialization::kVectorHeaderSize) /
                   fidl_serialization::GetEntrySize(min_key_size)) +
      1;
  AddEntries(entry_count, min_key_size);
  PageSnapshotPtr snapshot = GetSnapshot();

  // Call GetEntries and find a partial result.
  bool called;
  Status status;
  fidl::VectorPtr<Entry> actual_entries;
  std::unique_ptr<Token> actual_next_token;
  snapshot->GetEntries(
      fidl::VectorPtr<uint8_t>::New(0), nullptr,
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &actual_entries, &actual_next_token));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::PARTIAL_RESULT, status);
  EXPECT_TRUE(actual_next_token);

  // Call GetEntries with the previous token and receive the remaining results.
  fidl::VectorPtr<Entry> actual_next_entries;
  snapshot->GetEntries(
      fidl::VectorPtr<uint8_t>::New(0), std::move(actual_next_token),
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &actual_next_entries, &actual_next_token));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(actual_next_token);

  for (auto& entry : actual_next_entries.take()) {
    actual_entries.push_back(std::move(entry));
  }
  EXPECT_EQ(static_cast<size_t>(entry_count), actual_entries->size());

  // Check that the correct values of the keys are all present in the result and
  // in the correct order.
  for (int i = 0; i < static_cast<int>(actual_entries->size()); ++i) {
    ASSERT_EQ(GetKey(i, min_key_size),
              convert::ToString(actual_entries->at(i).key));
    ASSERT_EQ(GetValue(i, 0), ToString(actual_entries->at(i).value));
  }
}

TEST_F(PageImplTest, PutGetSnapshotGetEntriesInlineWithTokenForSize) {
  const size_t entry_count = 20;
  const size_t min_value_size =
      fidl_serialization::kMaxInlineDataSize * 3 / 2 / entry_count;
  AddEntries(entry_count, 0, min_value_size);
  PageSnapshotPtr snapshot = GetSnapshot();

  // Call GetEntries and find a partial result.
  bool called;
  Status status;
  fidl::VectorPtr<InlinedEntry> actual_entries;
  std::unique_ptr<Token> actual_next_token;
  snapshot->GetEntriesInline(
      fidl::VectorPtr<uint8_t>::New(0), nullptr,
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &actual_entries, &actual_next_token));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::PARTIAL_RESULT, status);
  EXPECT_TRUE(actual_next_token);

  // Call GetEntries with the previous token and receive the remaining results.
  fidl::VectorPtr<InlinedEntry> actual_entries2;
  std::unique_ptr<Token> actual_next_token2;
  snapshot->GetEntriesInline(
      fidl::VectorPtr<uint8_t>::New(0), std::move(actual_next_token),
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &actual_entries2, &actual_next_token2));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(actual_next_token2);
  for (auto& entry : actual_entries2.take()) {
    actual_entries.push_back(std::move(entry));
  }
  EXPECT_EQ(static_cast<size_t>(entry_count), actual_entries->size());

  // Check that the correct values of the keys are all present in the result and
  // in the correct order.
  for (int i = 0; i < static_cast<int>(actual_entries->size()); ++i) {
    ASSERT_EQ(GetKey(i, 0), convert::ToString(actual_entries->at(i).key));
    ASSERT_TRUE(actual_entries->at(i).inlined_value);
    ASSERT_EQ(GetValue(i, min_value_size),
              convert::ToString(actual_entries->at(i).inlined_value->value));
  }
}

TEST_F(PageImplTest, PutGetSnapshotGetEntriesInlineWithTokenForEntryCount) {
  const size_t min_key_size = 8;
  const size_t min_value_size = 1;
  // Approximate size of the entry: takes into account size of the pointers for
  // key, object and entry itself; enum size for Priority and size of the header
  // for the InlinedEntry struct.
  const size_t min_entry_size =
      fidl_serialization::Align(fidl_serialization::kPriorityEnumSize) +
      fidl_serialization::GetByteVectorSize(min_key_size) +
      fidl_serialization::GetByteVectorSize(min_value_size);
  // Put enough inlined entries to cause pagination based on size of the
  // message.
  const size_t entry_count =
      fidl_serialization::kMaxInlineDataSize * 3 / 2 / min_entry_size;
  AddEntries(entry_count, 0, min_value_size);
  PageSnapshotPtr snapshot = GetSnapshot();

  // Call GetEntries and find a partial result.
  bool called;
  Status status;
  fidl::VectorPtr<InlinedEntry> actual_entries;
  std::unique_ptr<Token> actual_next_token;
  snapshot->GetEntriesInline(
      fidl::VectorPtr<uint8_t>::New(0), nullptr,
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &actual_entries, &actual_next_token));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::PARTIAL_RESULT, status);
  EXPECT_TRUE(actual_next_token);

  // Call GetEntries with the previous token and receive the remaining results.
  fidl::VectorPtr<InlinedEntry> actual_entries2;
  std::unique_ptr<Token> actual_next_token2;
  snapshot->GetEntriesInline(
      fidl::VectorPtr<uint8_t>::New(0), std::move(actual_next_token),
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &actual_entries2, &actual_next_token2));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(actual_next_token2);
  for (auto& entry : actual_entries2.take()) {
    actual_entries.push_back(std::move(entry));
  }
  EXPECT_EQ(static_cast<size_t>(entry_count), actual_entries->size());

  // Check that the correct values of the keys are all present in the result and
  // in the correct order.
  for (int i = 0; i < static_cast<int>(actual_entries->size()); ++i) {
    ASSERT_EQ(GetKey(i, 0), convert::ToString(actual_entries->at(i).key));
    ASSERT_TRUE(actual_entries->at(i).inlined_value);
    ASSERT_EQ(GetValue(i, min_value_size),
              convert::ToString(actual_entries->at(i).inlined_value->value));
  }
}

TEST_F(PageImplTest, PutGetSnapshotGetEntriesWithTokenForHandles) {
  const size_t entry_count = 100;
  AddEntries(entry_count);
  PageSnapshotPtr snapshot = GetSnapshot();

  // Call GetEntries and find a partial result.
  bool called;
  Status status;
  fidl::VectorPtr<Entry> actual_entries;
  std::unique_ptr<Token> actual_next_token;
  snapshot->GetEntries(
      fidl::VectorPtr<uint8_t>::New(0), nullptr,
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &actual_entries, &actual_next_token));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::PARTIAL_RESULT, status);
  EXPECT_TRUE(actual_next_token);

  // Call GetEntries with the previous token and receive the remaining results.
  fidl::VectorPtr<Entry> actual_next_entries;
  snapshot->GetEntries(
      fidl::VectorPtr<uint8_t>::New(0), std::move(actual_next_token),
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &actual_next_entries, &actual_next_token));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(actual_next_token);
  for (auto& entry : actual_next_entries.take()) {
    actual_entries.push_back(std::move(entry));
  }
  EXPECT_EQ(static_cast<size_t>(entry_count), actual_entries->size());

  // Check that the correct values of the keys are all present in the result and
  // in the correct order.
  for (int i = 0; i < static_cast<int>(actual_entries->size()); ++i) {
    ASSERT_EQ(GetKey(i), convert::ToString(actual_entries->at(i).key));
    ASSERT_EQ(GetValue(i, 0), ToString(actual_entries->at(i).value));
  }
}

TEST_F(PageImplTest, PutGetSnapshotGetEntriesWithFetch) {
  std::string eager_key("a_key");
  std::string eager_value("an eager value");
  std::string lazy_key("another_key");
  std::string lazy_value("a lazy value");

  bool called;
  Status status;
  page_ptr_->PutWithPriority(
      convert::ToArray(lazy_key), convert::ToArray(lazy_value), Priority::LAZY,
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  storage::ObjectIdentifier lazy_object_identifier =
      fake_storage_->GetObjects().begin()->first;

  status = Status::UNKNOWN_ERROR;
  page_ptr_->Put(convert::ToArray(eager_key), convert::ToArray(eager_value),
                 callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  fake_storage_->DeleteObjectFromLocal(lazy_object_identifier);

  PageSnapshotPtr snapshot = GetSnapshot();

  fidl::VectorPtr<Entry> actual_entries;
  std::unique_ptr<Token> actual_next_token;
  snapshot->GetEntries(
      fidl::VectorPtr<uint8_t>::New(0), nullptr,
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &actual_entries, &actual_next_token));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(actual_next_token);
  ASSERT_EQ(2u, actual_entries->size());
  EXPECT_EQ(eager_key, convert::ExtendedStringView(actual_entries->at(0).key));
  EXPECT_EQ(eager_value, ToString(actual_entries->at(0).value));
  EXPECT_EQ(Priority::EAGER, actual_entries->at(0).priority);

  EXPECT_EQ(lazy_key, convert::ExtendedStringView(actual_entries->at(1).key));
  EXPECT_FALSE(actual_entries->at(1).value);
  EXPECT_EQ(Priority::LAZY, actual_entries->at(1).priority);
}

TEST_F(PageImplTest, PutGetSnapshotGetEntriesWithPrefix) {
  std::string eager_key("001-a_key");
  std::string eager_value("an eager value");
  std::string lazy_key("002-another_key");
  std::string lazy_value("a lazy value");

  bool called;
  Status status;
  page_ptr_->Put(convert::ToArray(eager_key), convert::ToArray(eager_value),
                 callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  status = Status::UNKNOWN_ERROR;
  page_ptr_->PutWithPriority(
      convert::ToArray(lazy_key), convert::ToArray(lazy_value), Priority::LAZY,
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  PageSnapshotPtr snapshot = GetSnapshot(convert::ToArray("001"));
  fidl::VectorPtr<Entry> actual_entries;
  std::unique_ptr<Token> actual_next_token;
  snapshot->GetEntries(
      fidl::VectorPtr<uint8_t>::New(0), nullptr,
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &actual_entries, &actual_next_token));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(actual_next_token);
  ASSERT_EQ(1u, actual_entries->size());
  EXPECT_EQ(eager_key, convert::ExtendedStringView(actual_entries->at(0).key));

  snapshot = GetSnapshot(convert::ToArray("00"));
  snapshot->GetEntries(
      fidl::VectorPtr<uint8_t>::New(0), nullptr,
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &actual_entries, &actual_next_token));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  ASSERT_EQ(2u, actual_entries->size());
  EXPECT_EQ(eager_key, convert::ExtendedStringView(actual_entries->at(0).key));
  EXPECT_EQ(lazy_key, convert::ExtendedStringView(actual_entries->at(1).key));
}

TEST_F(PageImplTest, PutGetSnapshotGetEntriesWithStart) {
  std::string eager_key("001-a_key");
  std::string eager_value("an eager value");
  std::string lazy_key("002-another_key");
  std::string lazy_value("a lazy value");

  bool called;
  Status status;
  page_ptr_->Put(convert::ToArray(eager_key), convert::ToArray(eager_value),
                 callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  status = Status::UNKNOWN_ERROR;
  page_ptr_->PutWithPriority(
      convert::ToArray(lazy_key), convert::ToArray(lazy_value), Priority::LAZY,
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  PageSnapshotPtr snapshot = GetSnapshot();
  fidl::VectorPtr<Entry> actual_entries;
  std::unique_ptr<Token> actual_next_token;
  snapshot->GetEntries(
      convert::ToArray("002"), nullptr,
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &actual_entries, &actual_next_token));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(actual_next_token);
  ASSERT_EQ(1u, actual_entries->size());
  EXPECT_EQ(lazy_key, convert::ExtendedStringView(actual_entries->at(0).key));

  snapshot->GetEntries(
      convert::ToArray("001"), nullptr,
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &actual_entries, &actual_next_token));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(actual_next_token);
  ASSERT_EQ(2u, actual_entries->size());
  EXPECT_EQ(eager_key, convert::ExtendedStringView(actual_entries->at(0).key));
  EXPECT_EQ(lazy_key, convert::ExtendedStringView(actual_entries->at(1).key));
}

TEST_F(PageImplTest, PutGetSnapshotGetKeys) {
  std::string key1("some_key");
  std::string value1("a small value");
  std::string key2("some_key2");
  std::string value2("another value");

  bool called;
  Status status;
  page_ptr_->StartTransaction(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  page_ptr_->Put(convert::ToArray(key1), convert::ToArray(value1),
                 callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  page_ptr_->Put(convert::ToArray(key2), convert::ToArray(value2),
                 callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  page_ptr_->Commit(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  PageSnapshotPtr snapshot = GetSnapshot();

  fidl::VectorPtr<fidl::VectorPtr<uint8_t>> actual_keys;
  std::unique_ptr<Token> actual_next_token;
  snapshot->GetKeys(fidl::VectorPtr<uint8_t>::New(0), nullptr,
                    callback::Capture(callback::SetWhenCalled(&called), &status,
                                      &actual_keys, &actual_next_token));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(actual_next_token);
  EXPECT_EQ(key1, convert::ExtendedStringView(actual_keys->at(0)));
  EXPECT_EQ(key2, convert::ExtendedStringView(actual_keys->at(1)));
}

TEST_F(PageImplTest, PutGetSnapshotGetKeysWithToken) {
  const size_t min_key_size = kMaxKeySize;
  const size_t key_count =
      fidl_serialization::kMaxInlineDataSize /
          fidl_serialization::GetByteVectorSize(min_key_size) +
      1;
  AddEntries(key_count, min_key_size);
  PageSnapshotPtr snapshot = GetSnapshot();

  // Call GetKeys and find a partial result.
  bool called;
  Status status;
  fidl::VectorPtr<fidl::VectorPtr<uint8_t>> actual_keys;
  std::unique_ptr<Token> actual_next_token;
  snapshot->GetKeys(fidl::VectorPtr<uint8_t>::New(0), nullptr,
                    callback::Capture(callback::SetWhenCalled(&called), &status,
                                      &actual_keys, &actual_next_token));

  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::PARTIAL_RESULT, status);
  EXPECT_TRUE(actual_next_token);

  // Call GetKeys with the previous token and receive the remaining results.
  fidl::VectorPtr<fidl::VectorPtr<uint8_t>> actual_next_keys;
  snapshot->GetKeys(fidl::VectorPtr<uint8_t>::New(0),
                    std::move(actual_next_token),
                    callback::Capture(callback::SetWhenCalled(&called), &status,
                                      &actual_next_keys, &actual_next_token));

  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(actual_next_token);
  for (auto& key : actual_next_keys.take()) {
    actual_keys.push_back(std::move(key));
  }
  EXPECT_EQ(static_cast<size_t>(key_count), actual_keys->size());

  // Check that the correct values of the keys are all present in the result and
  // in the correct order.
  for (size_t i = 0; i < actual_keys->size(); ++i) {
    ASSERT_EQ(GetKey(i, min_key_size), convert::ToString(actual_keys->at(i)));
  }
}

TEST_F(PageImplTest, PutGetSnapshotGetKeysWithPrefix) {
  std::string key1("001-some_key");
  std::string value1("a small value");
  std::string key2("002-some_key2");
  std::string value2("another value");

  bool called;
  Status status;
  page_ptr_->StartTransaction(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  page_ptr_->Put(convert::ToArray(key1), convert::ToArray(value1),
                 callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  page_ptr_->Put(convert::ToArray(key2), convert::ToArray(value2),
                 callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  page_ptr_->Commit(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  PageSnapshotPtr snapshot = GetSnapshot(convert::ToArray("001"));

  fidl::VectorPtr<fidl::VectorPtr<uint8_t>> actual_keys;
  std::unique_ptr<Token> actual_next_token;
  snapshot->GetKeys(fidl::VectorPtr<uint8_t>::New(0), nullptr,
                    callback::Capture(callback::SetWhenCalled(&called), &status,
                                      &actual_keys, &actual_next_token));

  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(actual_next_token);
  EXPECT_EQ(1u, actual_keys->size());
  EXPECT_EQ(key1, convert::ExtendedStringView(actual_keys->at(0)));

  snapshot = GetSnapshot(convert::ToArray("00"));
  snapshot->GetKeys(fidl::VectorPtr<uint8_t>::New(0), nullptr,
                    callback::Capture(callback::SetWhenCalled(&called), &status,
                                      &actual_keys, &actual_next_token));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(actual_next_token);
  EXPECT_EQ(2u, actual_keys->size());
  EXPECT_EQ(key1, convert::ExtendedStringView(actual_keys->at(0)));
  EXPECT_EQ(key2, convert::ExtendedStringView(actual_keys->at(1)));
}

TEST_F(PageImplTest, PutGetSnapshotGetKeysWithStart) {
  std::string key1("001-some_key");
  std::string value1("a small value");
  std::string key2("002-some_key2");
  std::string value2("another value");

  bool called;
  Status status;
  page_ptr_->StartTransaction(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  page_ptr_->Put(convert::ToArray(key1), convert::ToArray(value1),
                 callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  page_ptr_->Put(convert::ToArray(key2), convert::ToArray(value2),
                 callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  page_ptr_->Commit(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  PageSnapshotPtr snapshot = GetSnapshot();

  fidl::VectorPtr<fidl::VectorPtr<uint8_t>> actual_keys;
  std::unique_ptr<Token> actual_next_token;
  snapshot->GetKeys(convert::ToArray("002"), nullptr,
                    callback::Capture(callback::SetWhenCalled(&called), &status,
                                      &actual_keys, &actual_next_token));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(actual_next_token);
  EXPECT_EQ(1u, actual_keys->size());
  EXPECT_EQ(key2, convert::ExtendedStringView(actual_keys->at(0)));

  snapshot = GetSnapshot();
  snapshot->GetKeys(convert::ToArray("001"), nullptr,
                    callback::Capture(callback::SetWhenCalled(&called), &status,
                                      &actual_keys, &actual_next_token));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(actual_next_token);
  EXPECT_EQ(2u, actual_keys->size());
  EXPECT_EQ(key1, convert::ExtendedStringView(actual_keys->at(0)));
  EXPECT_EQ(key2, convert::ExtendedStringView(actual_keys->at(1)));
}

TEST_F(PageImplTest, SnapshotGetSmall) {
  std::string key("some_key");
  std::string value("a small value");

  bool called;
  Status status;
  page_ptr_->Put(convert::ToArray(key), convert::ToArray(value),
                 callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  PageSnapshotPtr snapshot = GetSnapshot();

  fuchsia::mem::BufferPtr actual_value;
  snapshot->Get(convert::ToArray(key),
                callback::Capture(callback::SetWhenCalled(&called), &status,
                                  &actual_value));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(value, ToString(actual_value));

  std::unique_ptr<InlinedValue> actual_inlined_value;
  snapshot->GetInline(convert::ToArray(key),
                      callback::Capture(callback::SetWhenCalled(&called),
                                        &status, &actual_inlined_value));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(actual_inlined_value);
  EXPECT_EQ(value, convert::ToString(actual_inlined_value->value));
}

TEST_F(PageImplTest, SnapshotGetLarge) {
  std::string value_string(fidl_serialization::kMaxInlineDataSize + 1, 'a');
  fsl::SizedVmo vmo;
  ASSERT_TRUE(fsl::VmoFromString(value_string, &vmo));

  bool called;
  Status status;
  ReferencePtr reference;
  page_ptr_->CreateReferenceFromBuffer(
      std::move(vmo).ToTransport(),
      callback::Capture(callback::SetWhenCalled(&called), &status, &reference));
  DrainLoop();

  ASSERT_TRUE(called);
  ASSERT_EQ(Status::OK, status);

  std::string key("some_key");
  page_ptr_->PutReference(
      convert::ToArray(key), std::move(*reference), Priority::EAGER,
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  PageSnapshotPtr snapshot = GetSnapshot();

  fuchsia::mem::BufferPtr actual_value;
  snapshot->Get(convert::ExtendedStringView(key).ToArray(),
                callback::Capture(callback::SetWhenCalled(&called), &status,
                                  &actual_value));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  EXPECT_EQ(value_string, ToString(actual_value));

  std::unique_ptr<InlinedValue> inlined_value;
  snapshot->GetInline(convert::ToArray(key),
                      callback::Capture(callback::SetWhenCalled(&called),
                                        &status, &inlined_value));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::VALUE_TOO_LARGE, status);
  EXPECT_FALSE(inlined_value);
}

TEST_F(PageImplTest, SnapshotGetNeedsFetch) {
  std::string key("some_key");
  std::string value("a small value");

  bool called;
  Status status;
  page_ptr_->PutWithPriority(
      convert::ToArray(key), convert::ToArray(value), Priority::LAZY,
      ::callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  storage::ObjectIdentifier lazy_object_identifier =
      fake_storage_->GetObjects().begin()->first;
  fake_storage_->DeleteObjectFromLocal(lazy_object_identifier);

  PageSnapshotPtr snapshot = GetSnapshot();

  fuchsia::mem::BufferPtr actual_value;
  snapshot->Get(convert::ToArray(key),
                ::callback::Capture(callback::SetWhenCalled(&called), &status,
                                    &actual_value));
  DrainLoop();

  EXPECT_TRUE(called);
  EXPECT_EQ(Status::NEEDS_FETCH, status);
  EXPECT_FALSE(actual_value);

  std::unique_ptr<InlinedValue> actual_inlined_value;
  snapshot->GetInline(convert::ToArray(key),
                      ::callback::Capture(callback::SetWhenCalled(&called),
                                          &status, &actual_inlined_value));
  DrainLoop();

  EXPECT_TRUE(called);
  EXPECT_EQ(Status::NEEDS_FETCH, status);
  EXPECT_FALSE(actual_inlined_value);
}

TEST_F(PageImplTest, SnapshotFetchPartial) {
  std::string key("some_key");
  std::string value("a small value");

  bool called;
  Status status;
  page_ptr_->Put(convert::ToArray(key), convert::ToArray(value),
                 callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  PageSnapshotPtr snapshot = GetSnapshot();

  fuchsia::mem::BufferPtr buffer;
  snapshot->FetchPartial(
      convert::ToArray(key), 2, 5,
      callback::Capture(callback::SetWhenCalled(&called), &status, &buffer));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  std::string content;
  EXPECT_TRUE(fsl::StringFromVmo(*buffer, &content));
  EXPECT_EQ("small", content);
}

TEST_F(PageImplTest, ParallelPut) {
  bool called;
  Status status;
  PagePtr page_ptr2;
  auto delaying_facade =
      std::make_unique<PageDelayingFacade>(page_id1_, page_ptr2.NewRequest());
  manager_->AddPageDelayingFacade(
      std::move(delaying_facade),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(Status::OK, status);

  std::string key("some_key");
  std::string value1("a small value");
  std::string value2("another value");

  PageSnapshotPtr snapshot1;
  PageSnapshotPtr snapshot2;

  page_ptr_->StartTransaction(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  page_ptr_->Put(convert::ToArray(key), convert::ToArray(value1),
                 callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  page_ptr2->StartTransaction(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  page_ptr2->Put(convert::ToArray(key), convert::ToArray(value2),
                 callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  page_ptr_->Commit(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  page_ptr2->Commit(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  page_ptr_->GetSnapshot(
      snapshot1.NewRequest(), fidl::VectorPtr<uint8_t>::New(0), nullptr,
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  page_ptr2->GetSnapshot(
      snapshot2.NewRequest(), fidl::VectorPtr<uint8_t>::New(0), nullptr,
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  fuchsia::mem::BufferPtr actual_value1;
  snapshot1->Get(convert::ToArray(key),
                 callback::Capture(callback::SetWhenCalled(&called), &status,
                                   &actual_value1));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  fuchsia::mem::BufferPtr actual_value2;
  snapshot2->Get(convert::ToArray(key),
                 callback::Capture(callback::SetWhenCalled(&called), &status,
                                   &actual_value2));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  // The two snapshots should have different contents.
  EXPECT_EQ(value1, ToString(actual_value1));
  EXPECT_EQ(value2, ToString(actual_value2));
}

TEST_F(PageImplTest, SerializedOperations) {
  fake_storage_->set_autocommit(false);

  std::string key("some_key");
  std::string value1("a value");
  std::string value2("a second value");
  std::string value3("a third value");

  bool called[7] = {false, false, false, false, false, false, false};
  Status statuses[7] = {Status::UNKNOWN_ERROR, Status::UNKNOWN_ERROR,
                        Status::UNKNOWN_ERROR, Status::UNKNOWN_ERROR,
                        Status::UNKNOWN_ERROR, Status::UNKNOWN_ERROR,
                        Status::UNKNOWN_ERROR};

  page_ptr_->Put(
      convert::ToArray(key), convert::ToArray(value1),
      callback::Capture(callback::SetWhenCalled(&called[0]), &statuses[0]));
  page_ptr_->Clear(
      callback::Capture(callback::SetWhenCalled(&called[1]), &statuses[1]));
  page_ptr_->Put(
      convert::ToArray(key), convert::ToArray(value2),
      callback::Capture(callback::SetWhenCalled(&called[2]), &statuses[2]));
  page_ptr_->Delete(
      convert::ToArray(key),
      callback::Capture(callback::SetWhenCalled(&called[3]), &statuses[3]));
  page_ptr_->StartTransaction(
      callback::Capture(callback::SetWhenCalled(&called[4]), &statuses[4]));
  page_ptr_->Put(
      convert::ToArray(key), convert::ToArray(value3),
      callback::Capture(callback::SetWhenCalled(&called[5]), &statuses[5]));
  page_ptr_->Commit(
      callback::Capture(callback::SetWhenCalled(&called[6]), &statuses[6]));

  // 4 first operations need to be serialized and blocked on commits.
  for (size_t i = 0; i < 4; ++i) {
    // Callbacks are blocked until operation commits.
    DrainLoop();
    EXPECT_FALSE(called[i]);

    // The commit queue contains the new commit.
    ASSERT_EQ(i + 1, fake_storage_->GetJournals().size());
    CommitFirstPendingJournal(fake_storage_->GetJournals());

    // The operation can now succeed.
    DrainLoop();
    EXPECT_TRUE(called[i]);
    EXPECT_EQ(Status::OK, statuses[i]);
  }

  // Neither StartTransaction, nor Put in a transaction should now be blocked.
  DrainLoop();
  for (size_t i = 4; i < 6; ++i) {
    EXPECT_TRUE(called[i]);
    EXPECT_EQ(Status::OK, statuses[i]);
  }

  // But committing the transaction should still be blocked.
  DrainLoop();
  EXPECT_FALSE(called[6]);
  EXPECT_NE(Status::OK, statuses[6]);

  // Unblocking the transaction commit.
  CommitFirstPendingJournal(fake_storage_->GetJournals());
  // The operation can now succeed.
  DrainLoop();
  EXPECT_TRUE(called[6]);
  EXPECT_EQ(Status::OK, statuses[6]);
}

TEST_F(PageImplTest, WaitForConflictResolutionNoConflicts) {
  bool called;
  ConflictResolutionWaitStatus status;
  page_ptr_->WaitForConflictResolution(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  ASSERT_TRUE(called);
  EXPECT_EQ(ConflictResolutionWaitStatus::NO_CONFLICTS, status);
  EXPECT_TRUE(resolver_->IsEmpty());

  // Special case: no changes from the previous call; event OnEmpty is not
  // triggered, but WaitForConflictResolution should return right away, as there
  // are no pending merges.
  page_ptr_->WaitForConflictResolution(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  ASSERT_TRUE(called);
  EXPECT_EQ(ConflictResolutionWaitStatus::NO_CONFLICTS, status);
  EXPECT_TRUE(resolver_->IsEmpty());
}

}  // namespace
}  // namespace ledger

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/page_impl.h"

#include <algorithm>
#include <map>
#include <memory>

#include "apps/ledger/src/app/constants.h"
#include "apps/ledger/src/app/fidl/serialization_size.h"
#include "apps/ledger/src/app/merging/merge_resolver.h"
#include "apps/ledger/src/app/page_manager.h"
#include "apps/ledger/src/backoff/exponential_backoff.h"
#include "apps/ledger/src/callback/capture.h"
#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/coroutine/coroutine_impl.h"
#include "apps/ledger/src/environment/environment.h"
#include "apps/ledger/src/storage/fake/fake_journal.h"
#include "apps/ledger/src/storage/fake/fake_journal_delegate.h"
#include "apps/ledger/src/storage/fake/fake_page_storage.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/mtl/socket/strings.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/vmo/strings.h"

namespace ledger {
namespace {

std::string ToString(const mx::vmo& vmo) {
  std::string value;
  bool status = mtl::StringFromVmo(vmo, &value);
  FXL_DCHECK(status);
  return value;
}

class PageImplTest : public test::TestWithMessageLoop {
 public:
  PageImplTest() : environment_(message_loop_.task_runner(), nullptr) {}
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
        std::make_unique<backoff::ExponentialBackoff>(
            fxl::TimeDelta::FromSeconds(0), 1u,
            fxl::TimeDelta::FromSeconds(0)));

    manager_ = std::make_unique<PageManager>(
        &environment_, std::move(fake_storage), nullptr, std::move(resolver),
        PageManager::PageStorageState::NEW);
    Status status;
    manager_->BindPage(page_ptr_.NewRequest(),
                       callback::Capture(MakeQuitTask(), &status));
    EXPECT_EQ(Status::OK, status);
    EXPECT_FALSE(RunLoopWithTimeout());
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

  storage::ObjectId AddObjectToStorage(std::string value_string) {
    storage::Status status;
    storage::ObjectId object_id;
    fake_storage_->AddObjectFromLocal(
        storage::DataSource::Create(std::move(value_string)),
        callback::Capture(MakeQuitTask(), &status, &object_id));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(storage::Status::OK, status);
    return object_id;
  }

  std::unique_ptr<const storage::Object> AddObject(const std::string& value) {
    storage::ObjectId object_id = AddObjectToStorage(value);

    storage::Status status;
    std::unique_ptr<const storage::Object> object;
    fake_storage_->GetObject(
        object_id, storage::PageStorage::Location::LOCAL,
        callback::Capture(MakeQuitTask(), &status, &object));
    EXPECT_FALSE(RunLoopWithTimeout());
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

  void AddEntries(int entry_count,
                  size_t min_key_size = 0u,
                  size_t min_value_size = 0u) {
    FXL_DCHECK(entry_count <= 10000);
    auto callback_statusok = [this](Status status) {
      EXPECT_EQ(Status::OK, status);
      message_loop_.PostQuitTask();
    };
    page_ptr_->StartTransaction(callback_statusok);
    EXPECT_FALSE(RunLoopWithTimeout());

    for (int i = 0; i < entry_count; ++i) {
      page_ptr_->Put(convert::ToArray(GetKey(i, min_key_size)),
                     convert::ToArray(GetValue(i, min_value_size)),
                     callback_statusok);
      EXPECT_FALSE(RunLoopWithTimeout());
    }
    page_ptr_->Commit(callback_statusok);
    EXPECT_FALSE(RunLoopWithTimeout());
  }

  PageSnapshotPtr GetSnapshot(fidl::Array<uint8_t> prefix = nullptr) {
    auto callback_getsnapshot = [this](Status status) {
      EXPECT_EQ(Status::OK, status);
      message_loop_.PostQuitTask();
    };
    PageSnapshotPtr snapshot;
    page_ptr_->GetSnapshot(snapshot.NewRequest(), std::move(prefix), nullptr,
                           callback_getsnapshot);
    EXPECT_FALSE(RunLoopWithTimeout());
    return snapshot;
  }

  storage::PageId page_id1_;
  storage::fake::FakePageStorage* fake_storage_;
  std::unique_ptr<PageManager> manager_;

  PagePtr page_ptr_;
  Environment environment_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageImplTest);
};

TEST_F(PageImplTest, GetId) {
  page_ptr_->GetId([this](fidl::Array<uint8_t> page_id) {
    EXPECT_EQ(page_id1_, convert::ToString(page_id));
    message_loop_.PostQuitTask();
  });
  EXPECT_FALSE(RunLoopWithTimeout());
}

TEST_F(PageImplTest, PutNoTransaction) {
  std::string key("some_key");
  std::string value("a small value");
  auto callback = [this, &key, &value](Status status) {
    EXPECT_EQ(Status::OK, status);
    auto objects = fake_storage_->GetObjects();
    EXPECT_EQ(1u, objects.size());
    storage::ObjectId object_id = objects.begin()->first;
    std::string actual_value = objects.begin()->second;
    EXPECT_EQ(value, actual_value);

    const std::map<std::string,
                   std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    auto it = journals.begin();
    EXPECT_TRUE(it->second->IsCommitted());
    EXPECT_EQ(1u, it->second->GetData().size());
    storage::fake::FakeJournalDelegate::Entry entry =
        it->second->GetData().at(key);
    EXPECT_EQ(object_id, entry.value);
    EXPECT_FALSE(entry.deleted);
    EXPECT_EQ(storage::KeyPriority::EAGER, entry.priority);
    message_loop_.PostQuitTask();
  };
  page_ptr_->Put(convert::ToArray(key), convert::ToArray(value), callback);
  EXPECT_FALSE(RunLoopWithTimeout());
}

TEST_F(PageImplTest, PutReferenceNoTransaction) {
  std::string key("some_key");
  std::string object_data("some_data");
  std::unique_ptr<const storage::Object> object = AddObject(object_data);

  storage::ObjectId object_id = object->GetId();
  ReferencePtr reference = Reference::New();
  reference->opaque_id = convert::ToArray(object_id);

  auto callback = [this, &key, &object_id](Status status) {
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
    storage::fake::FakeJournalDelegate::Entry entry =
        it->second->GetData().at(key);
    EXPECT_EQ(object_id, entry.value);
    EXPECT_FALSE(entry.deleted);
    EXPECT_EQ(storage::KeyPriority::LAZY, entry.priority);
    message_loop_.PostQuitTask();
  };
  page_ptr_->PutReference(convert::ToArray(key), std::move(reference),
                          Priority::LAZY, callback);
  EXPECT_FALSE(RunLoopWithTimeout());
}

TEST_F(PageImplTest, PutUnknownReference) {
  std::string key("some_key");
  storage::ObjectId object_id("unknown_id");
  ReferencePtr reference = Reference::New();
  reference->opaque_id = convert::ToArray(object_id);

  auto callback = [this](Status status) {
    EXPECT_EQ(Status::REFERENCE_NOT_FOUND, status);
    auto objects = fake_storage_->GetObjects();
    // No object should have been added.
    EXPECT_EQ(0u, objects.size());

    const std::map<std::string,
                   std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_EQ(0u, journals.size());
    message_loop_.PostQuitTask();
  };
  page_ptr_->PutReference(convert::ToArray(key), std::move(reference),
                          Priority::LAZY, callback);
  EXPECT_FALSE(RunLoopWithTimeout());
}

TEST_F(PageImplTest, DeleteNoTransaction) {
  std::string key("some_key");

  page_ptr_->Delete(convert::ToArray(key), [this, &key](Status status) {
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
    EXPECT_EQ(1u, it->second->GetData().size());
    storage::fake::FakeJournalDelegate::Entry entry =
        it->second->GetData().at(key);
    EXPECT_TRUE(entry.deleted);
    message_loop_.PostQuitTask();
  });
  EXPECT_FALSE(RunLoopWithTimeout());
}

TEST_F(PageImplTest, TransactionCommit) {
  std::string key1("some_key1");
  storage::ObjectId object_id1;
  std::string value("a small value");

  std::string key2("some_key2");
  std::string value2("another value");
  std::unique_ptr<const storage::Object> object2 = AddObject(value2);
  storage::ObjectId object_id2 = object2->GetId();

  ReferencePtr reference = Reference::New();
  reference->opaque_id = convert::ToArray(object_id2);

  // Sequence of operations:
  //  - StartTransaction
  //  - Put
  //  - PutReference
  //  - Delete
  //  - Commit
  page_ptr_->StartTransaction([this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  });
  EXPECT_FALSE(RunLoopWithTimeout());

  auto put_callback = [this, &key1, &value, &object_id1](Status status) {
    EXPECT_EQ(Status::OK, status);
    auto objects = fake_storage_->GetObjects();
    EXPECT_EQ(2u, objects.size());
    // Objects are ordered by a randomly assigned object id, so we can't know
    // the correct possition of the value in the map.
    bool object_found = false;
    for (auto object : objects) {
      if (object.second == value) {
        object_found = true;
        object_id1 = object.first;
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
    storage::fake::FakeJournalDelegate::Entry entry =
        it->second->GetData().at(key1);
    EXPECT_EQ(object_id1, entry.value);
    EXPECT_FALSE(entry.deleted);
    EXPECT_EQ(storage::KeyPriority::EAGER, entry.priority);
    message_loop_.PostQuitTask();
  };

  page_ptr_->Put(convert::ToArray(key1), convert::ToArray(value), put_callback);
  EXPECT_FALSE(RunLoopWithTimeout());

  auto put_reference_callback = [this, &key2, &object_id2](Status status) {
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
    storage::fake::FakeJournalDelegate::Entry entry =
        it->second->GetData().at(key2);
    EXPECT_EQ(object_id2, entry.value);
    EXPECT_FALSE(entry.deleted);
    EXPECT_EQ(storage::KeyPriority::LAZY, entry.priority);
    message_loop_.PostQuitTask();
  };

  page_ptr_->PutReference(convert::ToArray(key2), std::move(reference),
                          Priority::LAZY, put_reference_callback);
  EXPECT_FALSE(RunLoopWithTimeout());

  auto delete_callback = [this, &key2](Status status) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(2u, fake_storage_->GetObjects().size());

    // No finished commit yet, with the second entry deleted.
    const std::map<std::string,
                   std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    auto it = journals.begin();
    EXPECT_FALSE(it->second->IsCommitted());
    EXPECT_EQ(2u, it->second->GetData().size());
    storage::fake::FakeJournalDelegate::Entry entry =
        it->second->GetData().at(key2);
    EXPECT_TRUE(entry.deleted);
    message_loop_.PostQuitTask();
  };

  page_ptr_->Delete(convert::ToArray(key2), delete_callback);
  EXPECT_FALSE(RunLoopWithTimeout());

  page_ptr_->Commit([this](Status status) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(2u, fake_storage_->GetObjects().size());

    const std::map<std::string,
                   std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    auto it = journals.begin();
    EXPECT_TRUE(it->second->IsCommitted());
    EXPECT_EQ(2u, it->second->GetData().size());
    message_loop_.PostQuitTask();
  });
  EXPECT_FALSE(RunLoopWithTimeout());
}

TEST_F(PageImplTest, TransactionRollback) {
  // Sequence of operations:
  //  - StartTransaction
  //  - Rollback
  page_ptr_->StartTransaction(
      [](Status status) { EXPECT_EQ(Status::OK, status); });
  page_ptr_->Rollback([this](Status status) {
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
    message_loop_.PostQuitTask();
  });
  EXPECT_FALSE(RunLoopWithTimeout());
}

TEST_F(PageImplTest, NoTwoTransactions) {
  // Sequence of operations:
  //  - StartTransaction
  //  - StartTransaction
  page_ptr_->StartTransaction(
      [](Status status) { EXPECT_EQ(Status::OK, status); });
  page_ptr_->StartTransaction([this](Status status) {
    EXPECT_EQ(Status::TRANSACTION_ALREADY_IN_PROGRESS, status);
    message_loop_.PostQuitTask();
  });
  EXPECT_FALSE(RunLoopWithTimeout());
}

TEST_F(PageImplTest, NoTransactionCommit) {
  // Sequence of operations:
  //  - Commit
  page_ptr_->Commit([this](Status status) {
    EXPECT_EQ(Status::NO_TRANSACTION_IN_PROGRESS, status);
    message_loop_.PostQuitTask();
  });
  EXPECT_FALSE(RunLoopWithTimeout());
}

TEST_F(PageImplTest, NoTransactionRollback) {
  // Sequence of operations:
  //  - Rollback
  page_ptr_->Rollback([this](Status status) {
    EXPECT_EQ(Status::NO_TRANSACTION_IN_PROGRESS, status);
    message_loop_.PostQuitTask();
  });
  EXPECT_FALSE(RunLoopWithTimeout());
}

TEST_F(PageImplTest, CreateReferenceFromSocket) {
  std::string value("a small value");
  Status status;
  ReferencePtr reference;
  page_ptr_->CreateReferenceFromSocket(
      value.size(), mtl::WriteStringToSocket(value),
      [this, &status, &reference](Status received_status,
                                  ReferencePtr received_reference) {
        status = received_status;
        reference = std::move(received_reference);
        message_loop_.PostQuitTask();
      });
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  auto objects = fake_storage_->GetObjects();
  auto it = objects.find(reference->opaque_id);
  ASSERT_NE(objects.end(), it);
  ASSERT_EQ(value, it->second);
}

TEST_F(PageImplTest, CreateReferenceFromVmo) {
  std::string value("a small value");
  mx::vmo vmo;
  ASSERT_TRUE(mtl::VmoFromString(value, &vmo));

  Status status;
  ReferencePtr reference;
  page_ptr_->CreateReferenceFromVmo(
      std::move(vmo),
      [this, &status, &reference](Status received_status,
                                  ReferencePtr received_reference) {
        status = received_status;
        reference = std::move(received_reference);
        message_loop_.PostQuitTask();
      });
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  auto objects = fake_storage_->GetObjects();
  auto it = objects.find(reference->opaque_id);
  ASSERT_NE(objects.end(), it);
  ASSERT_EQ(value, it->second);
}

TEST_F(PageImplTest, PutGetSnapshotGetEntries) {
  std::string eager_key("a_key");
  std::string eager_value("an eager value");
  std::string lazy_key("another_key");
  std::string lazy_value("a lazy value");

  auto callback_statusok = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };

  page_ptr_->Put(convert::ToArray(eager_key), convert::ToArray(eager_value),
                 callback_statusok);
  EXPECT_FALSE(RunLoopWithTimeout());
  page_ptr_->PutWithPriority(convert::ToArray(lazy_key),
                             convert::ToArray(lazy_value), Priority::LAZY,
                             callback_statusok);
  EXPECT_FALSE(RunLoopWithTimeout());
  PageSnapshotPtr snapshot = GetSnapshot();

  fidl::Array<EntryPtr> actual_entries;
  auto callback_getentries = [this, &actual_entries](
                                 Status status, fidl::Array<EntryPtr> entries,
                                 fidl::Array<uint8_t> next_token) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_TRUE(next_token.is_null());
    actual_entries = std::move(entries);
    message_loop_.PostQuitTask();
  };
  snapshot->GetEntries(nullptr, nullptr, callback_getentries);
  EXPECT_FALSE(RunLoopWithTimeout());

  ASSERT_EQ(2u, actual_entries.size());
  EXPECT_EQ(eager_key, convert::ExtendedStringView(actual_entries[0]->key));
  EXPECT_EQ(eager_value, ToString(actual_entries[0]->value));
  EXPECT_EQ(Priority::EAGER, actual_entries[0]->priority);

  EXPECT_EQ(lazy_key, convert::ExtendedStringView(actual_entries[1]->key));
  EXPECT_EQ(lazy_value, ToString(actual_entries[1]->value));
  EXPECT_EQ(Priority::LAZY, actual_entries[1]->priority);
}

TEST_F(PageImplTest, PutGetSnapshotGetEntriesInline) {
  std::string eager_key("a_key");
  std::string eager_value("an eager value");
  std::string lazy_key("another_key");
  std::string lazy_value("a lazy value");

  Status status;

  page_ptr_->Put(convert::ToArray(eager_key), convert::ToArray(eager_value),
                 callback::Capture(MakeQuitTask(), &status));

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);

  page_ptr_->PutWithPriority(convert::ToArray(lazy_key),
                             convert::ToArray(lazy_value), Priority::LAZY,
                             callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);

  PageSnapshotPtr snapshot = GetSnapshot();

  fidl::Array<uint8_t> next_token;
  fidl::Array<InlinedEntryPtr> actual_entries;
  snapshot->GetEntriesInline(
      nullptr, nullptr,
      callback::Capture(MakeQuitTask(), &status, &actual_entries, &next_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(next_token.is_null());

  ASSERT_EQ(2u, actual_entries.size());
  EXPECT_EQ(eager_key, convert::ExtendedStringView(actual_entries[0]->key));
  EXPECT_EQ(eager_value, convert::ToString(actual_entries[0]->value));
  EXPECT_EQ(Priority::EAGER, actual_entries[0]->priority);

  EXPECT_EQ(lazy_key, convert::ExtendedStringView(actual_entries[1]->key));
  EXPECT_EQ(lazy_value, convert::ToString(actual_entries[1]->value));
  EXPECT_EQ(Priority::LAZY, actual_entries[1]->priority);
}

TEST_F(PageImplTest, PutGetSnapshotGetEntriesWithTokenForSize) {
  const size_t entry_count = 20;
  const size_t min_key_size =
      fidl_serialization::kMaxInlineDataSize * 3 / 2 / entry_count;
  AddEntries(entry_count, min_key_size);
  PageSnapshotPtr snapshot = GetSnapshot();

  // Call GetEntries and find a partial result.
  fidl::Array<EntryPtr> actual_entries;
  fidl::Array<uint8_t> actual_next_token;
  auto callback_getentries = [this, &actual_entries, &actual_next_token](
                                 Status status, fidl::Array<EntryPtr> entries,
                                 fidl::Array<uint8_t> next_token) {
    EXPECT_EQ(Status::PARTIAL_RESULT, status);
    EXPECT_FALSE(next_token.is_null());
    actual_entries = std::move(entries);
    actual_next_token = std::move(next_token);
    message_loop_.PostQuitTask();
  };
  snapshot->GetEntries(nullptr, nullptr, callback_getentries);
  EXPECT_FALSE(RunLoopWithTimeout());

  // Call GetEntries with the previous token and receive the remaining results.
  auto callback_getentries2 = [this, &actual_entries](
                                  Status status, fidl::Array<EntryPtr> entries,
                                  fidl::Array<uint8_t> next_token) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_TRUE(next_token.is_null());
    for (auto& entry : entries) {
      actual_entries.push_back(std::move(entry));
    }
    EXPECT_EQ(static_cast<size_t>(entry_count), actual_entries.size());
    message_loop_.PostQuitTask();
  };
  snapshot->GetEntries(nullptr, std::move(actual_next_token),
                       callback_getentries2);
  EXPECT_FALSE(RunLoopWithTimeout());

  // Check that the correct values of the keys are all present in the result and
  // in the correct order.
  for (int i = 0; i < static_cast<int>(actual_entries.size()); ++i) {
    ASSERT_EQ(GetKey(i, min_key_size),
              convert::ToString(actual_entries[i]->key));
    ASSERT_EQ(GetValue(i, 0), ToString(actual_entries[i]->value));
  }
}

TEST_F(PageImplTest, PutGetSnapshotGetEntriesInlineWithTokenForSize) {
  const size_t entry_count = 20;
  const size_t min_value_size =
      fidl_serialization::kMaxInlineDataSize * 3 / 2 / entry_count;
  AddEntries(entry_count, 0, min_value_size);
  PageSnapshotPtr snapshot = GetSnapshot();

  // Call GetEntries and find a partial result.
  Status status;
  fidl::Array<InlinedEntryPtr> actual_entries;
  fidl::Array<uint8_t> actual_next_token;
  snapshot->GetEntriesInline(
      nullptr, nullptr,
      callback::Capture(MakeQuitTask(), &status, &actual_entries,
                        &actual_next_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::PARTIAL_RESULT, status);
  EXPECT_FALSE(actual_next_token.is_null());

  // Call GetEntries with the previous token and receive the remaining results.
  fidl::Array<InlinedEntryPtr> actual_entries2;
  fidl::Array<uint8_t> actual_next_token2;
  snapshot->GetEntriesInline(
      nullptr, std::move(actual_next_token),
      callback::Capture(MakeQuitTask(), &status, &actual_entries2,
                        &actual_next_token2));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(actual_next_token2.is_null());
  for (auto& entry : actual_entries2) {
    actual_entries.push_back(std::move(entry));
  }
  EXPECT_EQ(static_cast<size_t>(entry_count), actual_entries.size());

  // Check that the correct values of the keys are all present in the result and
  // in the correct order.
  for (int i = 0; i < static_cast<int>(actual_entries.size()); ++i) {
    ASSERT_EQ(GetKey(i, 0), convert::ToString(actual_entries[i]->key));
    ASSERT_EQ(GetValue(i, min_value_size),
              convert::ToString(actual_entries[i]->value));
  }
}

TEST_F(PageImplTest, PutGetSnapshotGetEntriesWithTokenForHandles) {
  const size_t entry_count = 100;
  AddEntries(entry_count);
  PageSnapshotPtr snapshot = GetSnapshot();

  // Call GetEntries and find a partial result.
  fidl::Array<EntryPtr> actual_entries;
  fidl::Array<uint8_t> actual_next_token;
  auto callback_getentries = [this, &actual_entries, &actual_next_token](
                                 Status status, fidl::Array<EntryPtr> entries,
                                 fidl::Array<uint8_t> next_token) {
    EXPECT_EQ(Status::PARTIAL_RESULT, status);
    EXPECT_FALSE(next_token.is_null());
    actual_entries = std::move(entries);
    actual_next_token = std::move(next_token);
    message_loop_.PostQuitTask();
  };
  snapshot->GetEntries(nullptr, nullptr, callback_getentries);
  EXPECT_FALSE(RunLoopWithTimeout());

  // Call GetEntries with the previous token and receive the remaining results.
  auto callback_getentries2 = [this, &actual_entries](
                                  Status status, fidl::Array<EntryPtr> entries,
                                  fidl::Array<uint8_t> next_token) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_TRUE(next_token.is_null());
    for (auto& entry : entries) {
      actual_entries.push_back(std::move(entry));
    }
    EXPECT_EQ(static_cast<size_t>(entry_count), actual_entries.size());
    message_loop_.PostQuitTask();
  };
  snapshot->GetEntries(nullptr, std::move(actual_next_token),
                       callback_getentries2);
  EXPECT_FALSE(RunLoopWithTimeout());

  // Check that the correct values of the keys are all present in the result and
  // in the correct order.
  for (int i = 0; i < static_cast<int>(actual_entries.size()); ++i) {
    ASSERT_EQ(GetKey(i), convert::ToString(actual_entries[i]->key));
    ASSERT_EQ(GetValue(i, 0), ToString(actual_entries[i]->value));
  }
}

TEST_F(PageImplTest, PutGetSnapshotGetEntriesWithFetch) {
  std::string eager_key("a_key");
  std::string eager_value("an eager value");
  std::string lazy_key("another_key");
  std::string lazy_value("a lazy value");

  auto callback_statusok = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };

  page_ptr_->PutWithPriority(convert::ToArray(lazy_key),
                             convert::ToArray(lazy_value), Priority::LAZY,
                             callback_statusok);
  EXPECT_FALSE(RunLoopWithTimeout());
  storage::ObjectId lazy_object_id = fake_storage_->GetObjects().begin()->first;

  page_ptr_->Put(convert::ToArray(eager_key), convert::ToArray(eager_value),
                 callback_statusok);
  EXPECT_FALSE(RunLoopWithTimeout());

  fake_storage_->DeleteObjectFromLocal(lazy_object_id);

  PageSnapshotPtr snapshot = GetSnapshot();

  fidl::Array<EntryPtr> actual_entries;
  auto callback_getentries = [this, &actual_entries](
                                 Status status, fidl::Array<EntryPtr> entries,
                                 fidl::Array<uint8_t> next_token) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_TRUE(next_token.is_null());
    actual_entries = std::move(entries);
    message_loop_.PostQuitTask();
  };
  snapshot->GetEntries(nullptr, nullptr, callback_getentries);
  EXPECT_FALSE(RunLoopWithTimeout());

  ASSERT_EQ(2u, actual_entries.size());
  EXPECT_EQ(eager_key, convert::ExtendedStringView(actual_entries[0]->key));
  EXPECT_EQ(eager_value, ToString(actual_entries[0]->value));
  EXPECT_EQ(Priority::EAGER, actual_entries[0]->priority);

  EXPECT_EQ(lazy_key, convert::ExtendedStringView(actual_entries[1]->key));
  EXPECT_FALSE(actual_entries[1]->value);
  EXPECT_EQ(Priority::LAZY, actual_entries[1]->priority);
}

TEST_F(PageImplTest, PutGetSnapshotGetEntriesWithPrefix) {
  std::string eager_key("001-a_key");
  std::string eager_value("an eager value");
  std::string lazy_key("002-another_key");
  std::string lazy_value("a lazy value");

  auto callback_statusok = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };

  page_ptr_->Put(convert::ToArray(eager_key), convert::ToArray(eager_value),
                 callback_statusok);
  EXPECT_FALSE(RunLoopWithTimeout());
  page_ptr_->PutWithPriority(convert::ToArray(lazy_key),
                             convert::ToArray(lazy_value), Priority::LAZY,
                             callback_statusok);
  EXPECT_FALSE(RunLoopWithTimeout());

  PageSnapshotPtr snapshot = GetSnapshot(convert::ToArray("001"));
  fidl::Array<EntryPtr> actual_entries;
  auto callback_getentries = [this, &actual_entries](
                                 Status status, fidl::Array<EntryPtr> entries,
                                 fidl::Array<uint8_t> next_token) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_TRUE(next_token.is_null());
    actual_entries = std::move(entries);
    message_loop_.PostQuitTask();
  };
  snapshot->GetEntries(nullptr, nullptr, callback_getentries);
  EXPECT_FALSE(RunLoopWithTimeout());

  ASSERT_EQ(1u, actual_entries.size());
  EXPECT_EQ(eager_key, convert::ExtendedStringView(actual_entries[0]->key));

  snapshot = GetSnapshot(convert::ToArray("00"));
  snapshot->GetEntries(nullptr, nullptr, callback_getentries);
  EXPECT_FALSE(RunLoopWithTimeout());

  ASSERT_EQ(2u, actual_entries.size());
  EXPECT_EQ(eager_key, convert::ExtendedStringView(actual_entries[0]->key));
  EXPECT_EQ(lazy_key, convert::ExtendedStringView(actual_entries[1]->key));
}

TEST_F(PageImplTest, PutGetSnapshotGetEntriesWithStart) {
  std::string eager_key("001-a_key");
  std::string eager_value("an eager value");
  std::string lazy_key("002-another_key");
  std::string lazy_value("a lazy value");

  auto callback_statusok = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };

  page_ptr_->Put(convert::ToArray(eager_key), convert::ToArray(eager_value),
                 callback_statusok);
  EXPECT_FALSE(RunLoopWithTimeout());
  page_ptr_->PutWithPriority(convert::ToArray(lazy_key),
                             convert::ToArray(lazy_value), Priority::LAZY,
                             callback_statusok);
  EXPECT_FALSE(RunLoopWithTimeout());

  PageSnapshotPtr snapshot = GetSnapshot();
  fidl::Array<EntryPtr> actual_entries;
  auto callback_getentries = [this, &actual_entries](
                                 Status status, fidl::Array<EntryPtr> entries,
                                 fidl::Array<uint8_t> next_token) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_TRUE(next_token.is_null());
    actual_entries = std::move(entries);
    message_loop_.PostQuitTask();
  };
  snapshot->GetEntries(convert::ToArray("002"), nullptr, callback_getentries);
  EXPECT_FALSE(RunLoopWithTimeout());

  ASSERT_EQ(1u, actual_entries.size());
  EXPECT_EQ(lazy_key, convert::ExtendedStringView(actual_entries[0]->key));

  snapshot->GetEntries(convert::ToArray("001"), nullptr, callback_getentries);
  EXPECT_FALSE(RunLoopWithTimeout());

  ASSERT_EQ(2u, actual_entries.size());
  EXPECT_EQ(eager_key, convert::ExtendedStringView(actual_entries[0]->key));
  EXPECT_EQ(lazy_key, convert::ExtendedStringView(actual_entries[1]->key));
}

TEST_F(PageImplTest, PutGetSnapshotGetKeys) {
  std::string key1("some_key");
  std::string value1("a small value");
  std::string key2("some_key2");
  std::string value2("another value");

  auto callback_statusok = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };
  page_ptr_->StartTransaction(callback_statusok);
  EXPECT_FALSE(RunLoopWithTimeout());
  page_ptr_->Put(convert::ToArray(key1), convert::ToArray(value1),
                 callback_statusok);
  EXPECT_FALSE(RunLoopWithTimeout());
  page_ptr_->Put(convert::ToArray(key2), convert::ToArray(value2),
                 callback_statusok);
  EXPECT_FALSE(RunLoopWithTimeout());
  page_ptr_->Commit(callback_statusok);
  EXPECT_FALSE(RunLoopWithTimeout());
  PageSnapshotPtr snapshot = GetSnapshot();

  fidl::Array<fidl::Array<uint8_t>> actual_keys;
  auto callback_getkeys = [this, &actual_keys](
                              Status status,
                              fidl::Array<fidl::Array<uint8_t>> keys,
                              fidl::Array<uint8_t> next_token) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_TRUE(next_token.is_null());
    actual_keys = std::move(keys);
    message_loop_.PostQuitTask();
  };
  snapshot->GetKeys(nullptr, nullptr, callback_getkeys);
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(2u, actual_keys.size());
  EXPECT_EQ(key1, convert::ExtendedStringView(actual_keys[0]));
  EXPECT_EQ(key2, convert::ExtendedStringView(actual_keys[1]));
}

TEST_F(PageImplTest, PutGetSnapshotGetKeysWithToken) {
  const size_t key_count = 20;
  const size_t min_key_size =
      fidl_serialization::kMaxInlineDataSize * 3 / 2 / key_count;
  AddEntries(key_count, min_key_size);
  PageSnapshotPtr snapshot = GetSnapshot();

  // Call GetKeys and find a partial result.
  fidl::Array<fidl::Array<uint8_t>> actual_keys;
  fidl::Array<uint8_t> actual_next_token;
  auto callback_getkeys = [this, &actual_keys, &actual_next_token](
                              Status status,
                              fidl::Array<fidl::Array<uint8_t>> keys,
                              fidl::Array<uint8_t> next_token) {
    EXPECT_EQ(Status::PARTIAL_RESULT, status);
    EXPECT_FALSE(next_token.is_null());
    actual_keys = std::move(keys);
    actual_next_token = std::move(next_token);
    message_loop_.PostQuitTask();
  };
  snapshot->GetKeys(nullptr, nullptr, callback_getkeys);
  EXPECT_FALSE(RunLoopWithTimeout());

  // Call GetKeys with the previous token and receive the remaining results.
  auto callback_getkeys2 = [this, &actual_keys](
                               Status status,
                               fidl::Array<fidl::Array<uint8_t>> keys,
                               fidl::Array<uint8_t> next_token) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_TRUE(next_token.is_null());
    for (auto& key : keys) {
      actual_keys.push_back(std::move(key));
    }
    EXPECT_EQ(static_cast<size_t>(key_count), actual_keys.size());
    message_loop_.PostQuitTask();
  };
  snapshot->GetKeys(nullptr, std::move(actual_next_token), callback_getkeys2);
  EXPECT_FALSE(RunLoopWithTimeout());

  // Check that the correct values of the keys are all present in the result and
  // in the correct order.
  for (size_t i = 0; i < actual_keys.size(); ++i) {
    ASSERT_EQ(GetKey(i, min_key_size), convert::ToString(actual_keys[i]));
  }
}

TEST_F(PageImplTest, PutGetSnapshotGetKeysWithPrefix) {
  std::string key1("001-some_key");
  std::string value1("a small value");
  std::string key2("002-some_key2");
  std::string value2("another value");

  auto callback_statusok = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };
  page_ptr_->StartTransaction(callback_statusok);
  EXPECT_FALSE(RunLoopWithTimeout());
  page_ptr_->Put(convert::ToArray(key1), convert::ToArray(value1),
                 callback_statusok);
  EXPECT_FALSE(RunLoopWithTimeout());
  page_ptr_->Put(convert::ToArray(key2), convert::ToArray(value2),
                 callback_statusok);
  EXPECT_FALSE(RunLoopWithTimeout());
  page_ptr_->Commit(callback_statusok);
  EXPECT_FALSE(RunLoopWithTimeout());

  PageSnapshotPtr snapshot = GetSnapshot(convert::ToArray("001"));

  fidl::Array<fidl::Array<uint8_t>> actual_keys;
  auto callback_getkeys = [this, &actual_keys](
                              Status status,
                              fidl::Array<fidl::Array<uint8_t>> keys,
                              fidl::Array<uint8_t> next_token) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_TRUE(next_token.is_null());
    actual_keys = std::move(keys);
    message_loop_.PostQuitTask();
  };
  snapshot->GetKeys(nullptr, nullptr, callback_getkeys);
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(1u, actual_keys.size());
  EXPECT_EQ(key1, convert::ExtendedStringView(actual_keys[0]));

  snapshot = GetSnapshot(convert::ToArray("00"));
  snapshot->GetKeys(nullptr, nullptr, callback_getkeys);
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(2u, actual_keys.size());
  EXPECT_EQ(key1, convert::ExtendedStringView(actual_keys[0]));
  EXPECT_EQ(key2, convert::ExtendedStringView(actual_keys[1]));
}

TEST_F(PageImplTest, PutGetSnapshotGetKeysWithStart) {
  std::string key1("001-some_key");
  std::string value1("a small value");
  std::string key2("002-some_key2");
  std::string value2("another value");

  auto callback_statusok = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };
  page_ptr_->StartTransaction(callback_statusok);
  EXPECT_FALSE(RunLoopWithTimeout());
  page_ptr_->Put(convert::ToArray(key1), convert::ToArray(value1),
                 callback_statusok);
  EXPECT_FALSE(RunLoopWithTimeout());
  page_ptr_->Put(convert::ToArray(key2), convert::ToArray(value2),
                 callback_statusok);
  EXPECT_FALSE(RunLoopWithTimeout());
  page_ptr_->Commit(callback_statusok);
  EXPECT_FALSE(RunLoopWithTimeout());

  PageSnapshotPtr snapshot = GetSnapshot();

  fidl::Array<fidl::Array<uint8_t>> actual_keys;
  auto callback_getkeys = [this, &actual_keys](
                              Status status,
                              fidl::Array<fidl::Array<uint8_t>> keys,
                              fidl::Array<uint8_t> next_token) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_TRUE(next_token.is_null());
    actual_keys = std::move(keys);
    message_loop_.PostQuitTask();
  };
  snapshot->GetKeys(convert::ToArray("002"), nullptr, callback_getkeys);
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(1u, actual_keys.size());
  EXPECT_EQ(key2, convert::ExtendedStringView(actual_keys[0]));

  snapshot = GetSnapshot();
  snapshot->GetKeys(convert::ToArray("001"), nullptr, callback_getkeys);
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(2u, actual_keys.size());
  EXPECT_EQ(key1, convert::ExtendedStringView(actual_keys[0]));
  EXPECT_EQ(key2, convert::ExtendedStringView(actual_keys[1]));
}

TEST_F(PageImplTest, SnapshotGetSmall) {
  std::string key("some_key");
  std::string value("a small value");

  auto callback_put = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };
  page_ptr_->Put(convert::ToArray(key), convert::ToArray(value), callback_put);
  EXPECT_FALSE(RunLoopWithTimeout());
  PageSnapshotPtr snapshot = GetSnapshot();

  mx::vmo actual_value;
  auto callback_get = [this, &actual_value](Status status, mx::vmo value) {
    EXPECT_EQ(Status::OK, status);
    actual_value = std::move(value);
    message_loop_.PostQuitTask();
  };
  snapshot->Get(convert::ToArray(key), callback_get);
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(value, ToString(actual_value));

  fidl::Array<uint8_t> actual_inlined_value;
  auto callback_get_inline = [this, &actual_inlined_value](
                                 Status status, fidl::Array<uint8_t> value) {
    EXPECT_EQ(Status::OK, status);
    actual_inlined_value = std::move(value);
    message_loop_.PostQuitTask();
  };

  snapshot->GetInline(convert::ToArray(key), callback_get_inline);
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(value, convert::ToString(actual_inlined_value));
}

TEST_F(PageImplTest, SnapshotGetLarge) {
  std::string value_string(fidl_serialization::kMaxInlineDataSize + 1, 'a');
  storage::ObjectId object_id = AddObjectToStorage(value_string);

  std::string key("some_key");
  ReferencePtr reference = Reference::New();
  reference->opaque_id = convert::ToArray(object_id);

  auto callback_put = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };
  page_ptr_->PutReference(convert::ToArray(key), std::move(reference),
                          Priority::EAGER, callback_put);
  EXPECT_FALSE(RunLoopWithTimeout());
  PageSnapshotPtr snapshot = GetSnapshot();

  mx::vmo actual_value;
  auto callback_get = [this, &actual_value](Status status, mx::vmo value) {
    EXPECT_EQ(Status::OK, status);
    actual_value = std::move(value);
    message_loop_.PostQuitTask();
  };
  snapshot->Get(convert::ExtendedStringView(key).ToArray(), callback_get);
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(value_string, ToString(actual_value));

  auto callback_get_inline = [this](Status status, fidl::Array<uint8_t> value) {
    EXPECT_EQ(Status::VALUE_TOO_LARGE, status);
    message_loop_.PostQuitTask();
  };

  snapshot->GetInline(convert::ToArray(key), callback_get_inline);
  EXPECT_FALSE(RunLoopWithTimeout());
}

TEST_F(PageImplTest, SnapshotGetNeedsFetch) {
  std::string key("some_key");
  std::string value("a small value");

  Status status;
  auto postquit_callback = MakeQuitTask();
  page_ptr_->PutWithPriority(convert::ToArray(key), convert::ToArray(value),
                             Priority::LAZY,
                             ::callback::Capture(postquit_callback, &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);

  storage::ObjectId lazy_object_id = fake_storage_->GetObjects().begin()->first;
  fake_storage_->DeleteObjectFromLocal(lazy_object_id);

  PageSnapshotPtr snapshot = GetSnapshot();

  mx::vmo actual_value;
  snapshot->Get(convert::ToArray(key),
                ::callback::Capture(postquit_callback, &status, &actual_value));
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(Status::NEEDS_FETCH, status);

  fidl::Array<uint8_t> actual_inlined_value;
  snapshot->GetInline(
      convert::ToArray(key),
      ::callback::Capture(postquit_callback, &status, &actual_inlined_value));
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(Status::NEEDS_FETCH, status);
}

TEST_F(PageImplTest, SnapshotFetchPartial) {
  std::string key("some_key");
  std::string value("a small value");

  auto callback_put = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };
  page_ptr_->Put(convert::ToArray(key), convert::ToArray(value), callback_put);
  EXPECT_FALSE(RunLoopWithTimeout());
  PageSnapshotPtr snapshot = GetSnapshot();

  Status status;
  mx::vmo buffer;
  snapshot->FetchPartial(convert::ToArray(key), 2, 5,
                         [this, &status, &buffer](Status received_status,
                                                  mx::vmo received_buffer) {
                           status = received_status;
                           buffer = std::move(received_buffer);
                           message_loop_.PostQuitTask();
                         });
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  std::string content;
  EXPECT_TRUE(mtl::StringFromVmo(buffer, &content));
  EXPECT_EQ("small", content);
}

TEST_F(PageImplTest, ParallelPut) {
  Status status;
  PagePtr page_ptr2;
  manager_->BindPage(page_ptr2.NewRequest(),
                     callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);

  std::string key("some_key");
  std::string value1("a small value");
  std::string value2("another value");

  PageSnapshotPtr snapshot1;
  PageSnapshotPtr snapshot2;

  auto callback_simple = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };
  page_ptr_->StartTransaction(callback_simple);
  EXPECT_FALSE(RunLoopWithTimeout());

  page_ptr_->Put(convert::ToArray(key), convert::ToArray(value1),
                 callback_simple);
  EXPECT_FALSE(RunLoopWithTimeout());

  page_ptr2->StartTransaction(callback_simple);
  EXPECT_FALSE(RunLoopWithTimeout());

  page_ptr2->Put(convert::ToArray(key), convert::ToArray(value2),
                 callback_simple);
  EXPECT_FALSE(RunLoopWithTimeout());

  page_ptr_->Commit(callback_simple);
  EXPECT_FALSE(RunLoopWithTimeout());
  page_ptr2->Commit(callback_simple);
  EXPECT_FALSE(RunLoopWithTimeout());

  auto callback_getsnapshot = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };
  page_ptr_->GetSnapshot(snapshot1.NewRequest(), nullptr, nullptr,
                         callback_getsnapshot);
  EXPECT_FALSE(RunLoopWithTimeout());
  page_ptr2->GetSnapshot(snapshot2.NewRequest(), nullptr, nullptr,
                         callback_getsnapshot);
  EXPECT_FALSE(RunLoopWithTimeout());

  std::string actual_value1;
  auto callback_getvalue1 = [this, &actual_value1](Status status,
                                                   mx::vmo returned_value) {
    EXPECT_EQ(Status::OK, status);
    actual_value1 = ToString(returned_value);
    message_loop_.PostQuitTask();
  };
  snapshot1->Get(convert::ToArray(key), callback_getvalue1);
  EXPECT_FALSE(RunLoopWithTimeout());

  std::string actual_value2;
  auto callback_getvalue2 = [this, &actual_value2](Status status,
                                                   mx::vmo returned_value) {
    EXPECT_EQ(Status::OK, status);
    actual_value2 = ToString(returned_value);
    message_loop_.PostQuitTask();
  };
  snapshot2->Get(convert::ToArray(key), callback_getvalue2);
  EXPECT_FALSE(RunLoopWithTimeout());

  // The two snapshots should have different contents.
  EXPECT_EQ(value1, actual_value1);
  EXPECT_EQ(value2, actual_value2);
}

TEST_F(PageImplTest, SerializedOperations) {
  fake_storage_->set_autocommit(false);

  std::string key("some_key");
  std::string value1("a value");
  std::string value2("a second value");
  std::string value3("a third value");

  auto callback_simple = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };

  page_ptr_->Put(convert::ToArray(key), convert::ToArray(value1),
                 callback_simple);
  page_ptr_->Put(convert::ToArray(key), convert::ToArray(value2),
                 callback_simple);
  page_ptr_->Delete(convert::ToArray(key), callback_simple);
  page_ptr_->StartTransaction(callback_simple);
  page_ptr_->Put(convert::ToArray(key), convert::ToArray(value3),
                 callback_simple);
  page_ptr_->Commit(callback_simple);

  // 3 first operations need to be serialized and blocked on commits.
  for (size_t i = 0; i < 3; ++i) {
    // Callbacks are blocked until operation commits.
    EXPECT_TRUE(RunLoopWithTimeout(fxl::TimeDelta::FromMilliseconds(20)));

    // The commit queue contains the new commit.
    ASSERT_EQ(i + 1, fake_storage_->GetJournals().size());
    CommitFirstPendingJournal(fake_storage_->GetJournals());

    // The operation can now succeed.
    EXPECT_FALSE(RunLoopWithTimeout());
  }

  // Neither StartTransaction, nor Put in a transaction should be blocked.
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_FALSE(RunLoopWithTimeout());
  }

  // But committing the transaction should still be blocked.
  EXPECT_TRUE(RunLoopWithTimeout(fxl::TimeDelta::FromMilliseconds(20)));

  // Unblocking the transaction commit.
  CommitFirstPendingJournal(fake_storage_->GetJournals());
  // The operation can now succeed.
  EXPECT_FALSE(RunLoopWithTimeout());
}

}  // namespace
}  // namespace ledger

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/app/page_impl.h"

#include "apps/ledger/app/constants.h"
#include "apps/ledger/convert/convert.h"
#include "apps/ledger/glue/test/run_loop.h"
#include "apps/ledger/storage/fake/fake_journal.h"
#include "apps/ledger/storage/fake/fake_journal_delegate.h"
#include "apps/ledger/storage/fake/fake_page_storage.h"
#include "apps/ledger/storage/public/page_storage.h"
#include "gtest/gtest.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace ledger {
namespace {

class PageImplTest : public ::testing::Test {
 public:
  PageImplTest() {}
  ~PageImplTest() override {}

 protected:
  // ApplicationTestBase:
  void SetUp() override {
    ::testing::Test::SetUp();
    page_id1_ = storage::PageId(kPageIdSize, 'a');
    fake_storage_.reset(new storage::fake::FakePageStorage(page_id1_));
    fake_storage_ptr_ = fake_storage_.get();
  }

  void TearDown() override { ::testing::Test::TearDown(); }

  storage::PageId page_id1_;
  // FakePageStorage is owned by the PageImpl objects. For testing, we keep
  // around a pointer to the object to inspect its state. |fake_storage_ptr_|
  // remains valid as long as |fake_storage_| is valid, or as long as the
  // |PageImpl| owning it is valid.
  std::unique_ptr<storage::fake::FakePageStorage> fake_storage_;
  storage::fake::FakePageStorage* fake_storage_ptr_;

 private:
  mtl::MessageLoop message_loop_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PageImplTest);
};

TEST_F(PageImplTest, GetId) {
  PagePtr page_ptr;

  new PageImpl(GetProxy(&page_ptr), std::move(fake_storage_));
  page_ptr->GetId([this](mojo::Array<uint8_t> page_id) {
    EXPECT_EQ(this->page_id1_, convert::ToString(page_id));
    glue::test::QuitLoop();
  });
  glue::test::RunLoop();
}

TEST_F(PageImplTest, PutNoTransaction) {
  PagePtr page_ptr;
  std::string key("some_key");
  std::string value("a little value");
  new PageImpl(GetProxy(&page_ptr), std::move(fake_storage_));
  auto callback = [this, &key, &value](Status status) {
    EXPECT_EQ(Status::OK, status);
    const std::unordered_map<storage::ObjectId, std::string> objects =
        fake_storage_ptr_->GetObjects();
    EXPECT_EQ(1u, objects.size());
    storage::ObjectId object_id = objects.begin()->first;
    std::string actual_value = objects.begin()->second;
    EXPECT_EQ(value, actual_value);

    const std::vector<std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_ptr_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    EXPECT_TRUE(journals[0]->IsCommitted());
    EXPECT_EQ(1u, journals[0]->GetData().size());
    storage::fake::FakeJournalDelegate::Entry entry =
        journals[0]->GetData().at(key);
    EXPECT_EQ(object_id, entry.value);
    EXPECT_FALSE(entry.deleted);
    EXPECT_EQ(storage::KeyPriority::EAGER, entry.priority);
    glue::test::QuitLoop();
  };
  page_ptr->Put(convert::ToArray(key), convert::ToArray(value), callback);
  glue::test::RunLoop();
}

TEST_F(PageImplTest, PutReferenceNoTransaction) {
  PagePtr page_ptr;
  std::string key("some_key");
  storage::ObjectId object_id("some_id");
  ReferencePtr reference = Reference::New();
  reference->opaque_id = convert::ToArray(object_id);

  new PageImpl(GetProxy(&page_ptr), std::move(fake_storage_));
  auto callback = [this, &key, &object_id](Status status) {
    EXPECT_EQ(Status::OK, status);
    const std::unordered_map<storage::ObjectId, std::string> objects =
        fake_storage_ptr_->GetObjects();
    // No object should have been added.
    EXPECT_EQ(0u, objects.size());

    const std::vector<std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_ptr_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    EXPECT_TRUE(journals[0]->IsCommitted());
    EXPECT_EQ(1u, journals[0]->GetData().size());
    storage::fake::FakeJournalDelegate::Entry entry =
        journals[0]->GetData().at(key);
    EXPECT_EQ(object_id, entry.value);
    EXPECT_FALSE(entry.deleted);
    EXPECT_EQ(storage::KeyPriority::LAZY, entry.priority);
    glue::test::QuitLoop();
  };
  page_ptr->PutReference(convert::ToArray(key), std::move(reference),
                         Priority::LAZY, callback);
  glue::test::RunLoop();
}

TEST_F(PageImplTest, DeleteNoTransaction) {
  PagePtr page_ptr;
  std::string key("some_key");

  new PageImpl(GetProxy(&page_ptr), std::move(fake_storage_));
  page_ptr->Delete(convert::ToArray(key), [this, &key](Status status) {
    EXPECT_EQ(Status::OK, status);
    const std::unordered_map<storage::ObjectId, std::string> objects =
        fake_storage_ptr_->GetObjects();
    // No object should have been added.
    EXPECT_EQ(0u, objects.size());

    const std::vector<std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_ptr_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    EXPECT_TRUE(journals[0]->IsCommitted());
    EXPECT_EQ(1u, journals[0]->GetData().size());
    storage::fake::FakeJournalDelegate::Entry entry =
        journals[0]->GetData().at(key);
    EXPECT_TRUE(entry.deleted);
    glue::test::QuitLoop();
  });
  glue::test::RunLoop();
}

TEST_F(PageImplTest, TransactionCommit) {
  PagePtr page_ptr;
  std::string key1("some_key1");
  storage::ObjectId object_id1;
  std::string value("a little value");
  std::string key2("some_key2");
  storage::ObjectId object_id2("some_id2");
  ReferencePtr reference = Reference::New();
  reference->opaque_id = convert::ToArray(object_id2);

  new PageImpl(GetProxy(&page_ptr), std::move(fake_storage_));

  // Sequence of operations:
  //  - StartTransaction
  //  - Put
  //  - PutReference
  //  - Delete
  //  - Commit
  page_ptr->StartTransaction([](Status status) {
    EXPECT_EQ(Status::OK, status);
    glue::test::QuitLoop();
  });
  glue::test::RunLoop();

  auto put_callback = [this, &key1, &value, &object_id1](Status status) {
    EXPECT_EQ(Status::OK, status);
    const std::unordered_map<storage::ObjectId, std::string> objects =
        fake_storage_ptr_->GetObjects();
    EXPECT_EQ(1u, objects.size());
    object_id1 = objects.begin()->first;
    std::string actual_value = objects.begin()->second;
    EXPECT_EQ(value, actual_value);

    // No finished commit yet.
    const std::vector<std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_ptr_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    EXPECT_FALSE(journals[0]->IsCommitted());
    EXPECT_EQ(1u, journals[0]->GetData().size());
    storage::fake::FakeJournalDelegate::Entry entry =
        journals[0]->GetData().at(key1);
    EXPECT_EQ(object_id1, entry.value);
    EXPECT_FALSE(entry.deleted);
    EXPECT_EQ(storage::KeyPriority::EAGER, entry.priority);
    glue::test::QuitLoop();

  };
  page_ptr->Put(convert::ToArray(key1), convert::ToArray(value), put_callback);
  glue::test::RunLoop();

  auto put_reference_callback = [this, &key2, &object_id2](Status status) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(1u, fake_storage_ptr_->GetObjects().size());

    // No finished commit yet, with now two entries.
    const std::vector<std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_ptr_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    EXPECT_FALSE(journals[0]->IsCommitted());
    EXPECT_EQ(2u, journals[0]->GetData().size());
    storage::fake::FakeJournalDelegate::Entry entry =
        journals[0]->GetData().at(key2);
    EXPECT_EQ(object_id2, entry.value);
    EXPECT_FALSE(entry.deleted);
    EXPECT_EQ(storage::KeyPriority::LAZY, entry.priority);
    glue::test::QuitLoop();

  };
  page_ptr->PutReference(convert::ToArray(key2), std::move(reference),
                         Priority::LAZY, put_reference_callback);
  glue::test::RunLoop();

  auto delete_callback = [this, &key2](Status status) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(1u, fake_storage_ptr_->GetObjects().size());

    // No finished commit yet, with the second entry deleted.
    const std::vector<std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_ptr_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    EXPECT_FALSE(journals[0]->IsCommitted());
    EXPECT_EQ(2u, journals[0]->GetData().size());
    storage::fake::FakeJournalDelegate::Entry entry =
        journals[0]->GetData().at(key2);
    EXPECT_TRUE(entry.deleted);
    glue::test::QuitLoop();

  };
  page_ptr->Delete(convert::ToArray(key2), delete_callback);
  glue::test::RunLoop();

  page_ptr->Commit([this](Status status) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(1u, fake_storage_ptr_->GetObjects().size());

    const std::vector<std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_ptr_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    EXPECT_TRUE(journals[0]->IsCommitted());
    EXPECT_EQ(2u, journals[0]->GetData().size());
    glue::test::QuitLoop();
  });
  glue::test::RunLoop();
}

TEST_F(PageImplTest, TransactionRollback) {
  PagePtr page_ptr;

  new PageImpl(GetProxy(&page_ptr), std::move(fake_storage_));

  // Sequence of operations:
  //  - StartTransaction
  //  - Rollback
  page_ptr->StartTransaction(
      [](Status status) { EXPECT_EQ(Status::OK, status); });
  page_ptr->Rollback([this](Status status) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(0u, fake_storage_ptr_->GetObjects().size());

    // Only one journal, rollbacked.
    const std::vector<std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_ptr_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    EXPECT_TRUE(journals[0]->IsRolledBack());
    EXPECT_EQ(0u, journals[0]->GetData().size());
    glue::test::QuitLoop();
  });
  glue::test::RunLoop();
}

TEST_F(PageImplTest, NoTwoTransactions) {
  PagePtr page_ptr;

  new PageImpl(GetProxy(&page_ptr), std::move(fake_storage_));

  // Sequence of operations:
  //  - StartTransaction
  //  - StartTransaction
  page_ptr->StartTransaction(
      [](Status status) { EXPECT_EQ(Status::OK, status); });
  page_ptr->StartTransaction([](Status status) {
    EXPECT_EQ(Status::TRANSACTION_ALREADY_IN_PROGRESS, status);
    glue::test::QuitLoop();
  });
  glue::test::RunLoop();
}

TEST_F(PageImplTest, NoTransactionCommit) {
  PagePtr page_ptr;

  new PageImpl(GetProxy(&page_ptr), std::move(fake_storage_));

  // Sequence of operations:
  //  - Commit
  page_ptr->Commit([](Status status) {
    EXPECT_EQ(Status::NO_TRANSACTION_IN_PROGRESS, status);
    glue::test::QuitLoop();
  });
  glue::test::RunLoop();
}

TEST_F(PageImplTest, NoTransactionRollback) {
  PagePtr page_ptr;

  new PageImpl(GetProxy(&page_ptr), std::move(fake_storage_));

  // Sequence of operations:
  //  - Rollback
  page_ptr->Rollback([](Status status) {
    EXPECT_EQ(Status::NO_TRANSACTION_IN_PROGRESS, status);
    glue::test::QuitLoop();
  });
  glue::test::RunLoop();
}

}  // namespace
}  // namespace ledger

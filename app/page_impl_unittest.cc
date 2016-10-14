// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/app/page_impl.h"

#include <memory>

#include "apps/ledger/app/constants.h"
#include "apps/ledger/convert/convert.h"
#include "apps/ledger/storage/fake/fake_journal.h"
#include "apps/ledger/storage/fake/fake_journal_delegate.h"
#include "apps/ledger/storage/fake/fake_page_storage.h"
#include "apps/ledger/storage/public/page_storage.h"
#include "gtest/gtest.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/data_pipe/strings.h"
#include "lib/mtl/tasks/message_loop.h"
#include "mojo/public/cpp/bindings/binding.h"

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

    page_impl_.reset(new PageImpl(fake_storage_.get()));
    binding_.reset(
        new mojo::Binding<Page>(page_impl_.get(), GetProxy(&page_ptr_)));
  }

  storage::PageId page_id1_;
  std::unique_ptr<storage::fake::FakePageStorage> fake_storage_;

  PagePtr page_ptr_;

  mtl::MessageLoop message_loop_;

 private:
  std::unique_ptr<PageImpl> page_impl_;
  std::unique_ptr<mojo::Binding<Page>> binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PageImplTest);
};

TEST_F(PageImplTest, GetId) {
  page_ptr_->GetId([this](mojo::Array<uint8_t> page_id) {
    EXPECT_EQ(page_id1_, convert::ToString(page_id));
    message_loop_.QuitNow();
  });
  message_loop_.Run();
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

    const std::vector<std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    EXPECT_TRUE(journals[0]->IsCommitted());
    EXPECT_EQ(1u, journals[0]->GetData().size());
    storage::fake::FakeJournalDelegate::Entry entry =
        journals[0]->GetData().at(key);
    EXPECT_EQ(object_id, entry.value);
    EXPECT_FALSE(entry.deleted);
    EXPECT_EQ(storage::KeyPriority::EAGER, entry.priority);
    message_loop_.QuitNow();
  };
  page_ptr_->Put(convert::ToArray(key), convert::ToArray(value), callback);
  message_loop_.Run();
}

TEST_F(PageImplTest, PutReferenceNoTransaction) {
  std::string key("some_key");
  storage::ObjectId object_id("some_id");
  ReferencePtr reference = Reference::New();
  reference->opaque_id = convert::ToArray(object_id);

  auto callback = [this, &key, &object_id](Status status) {
    EXPECT_EQ(Status::OK, status);
    auto objects = fake_storage_->GetObjects();
    // No object should have been added.
    EXPECT_EQ(0u, objects.size());

    const std::vector<std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    EXPECT_TRUE(journals[0]->IsCommitted());
    EXPECT_EQ(1u, journals[0]->GetData().size());
    storage::fake::FakeJournalDelegate::Entry entry =
        journals[0]->GetData().at(key);
    EXPECT_EQ(object_id, entry.value);
    EXPECT_FALSE(entry.deleted);
    EXPECT_EQ(storage::KeyPriority::LAZY, entry.priority);
    message_loop_.QuitNow();
  };
  page_ptr_->PutReference(convert::ToArray(key), std::move(reference),
                          Priority::LAZY, callback);
  message_loop_.Run();
}

TEST_F(PageImplTest, DeleteNoTransaction) {
  std::string key("some_key");

  page_ptr_->Delete(convert::ToArray(key), [this, &key](Status status) {
    EXPECT_EQ(Status::OK, status);
    auto objects = fake_storage_->GetObjects();
    // No object should have been added.
    EXPECT_EQ(0u, objects.size());

    const std::vector<std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    EXPECT_TRUE(journals[0]->IsCommitted());
    EXPECT_EQ(1u, journals[0]->GetData().size());
    storage::fake::FakeJournalDelegate::Entry entry =
        journals[0]->GetData().at(key);
    EXPECT_TRUE(entry.deleted);
    message_loop_.QuitNow();
  });
  message_loop_.Run();
}

TEST_F(PageImplTest, TransactionCommit) {
  std::string key1("some_key1");
  storage::ObjectId object_id1;
  std::string value("a small value");
  std::string key2("some_key2");
  storage::ObjectId object_id2("some_id2");
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
    message_loop_.QuitNow();
  });
  message_loop_.Run();

  auto put_callback = [this, &key1, &value, &object_id1](Status status) {
    EXPECT_EQ(Status::OK, status);
    auto objects = fake_storage_->GetObjects();
    EXPECT_EQ(1u, objects.size());
    object_id1 = objects.begin()->first;
    std::string actual_value = objects.begin()->second;
    EXPECT_EQ(value, actual_value);

    // No finished commit yet.
    const std::vector<std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    EXPECT_FALSE(journals[0]->IsCommitted());
    EXPECT_EQ(1u, journals[0]->GetData().size());
    storage::fake::FakeJournalDelegate::Entry entry =
        journals[0]->GetData().at(key1);
    EXPECT_EQ(object_id1, entry.value);
    EXPECT_FALSE(entry.deleted);
    EXPECT_EQ(storage::KeyPriority::EAGER, entry.priority);
    message_loop_.QuitNow();

  };
  page_ptr_->Put(convert::ToArray(key1), convert::ToArray(value), put_callback);
  message_loop_.Run();

  auto put_reference_callback = [this, &key2, &object_id2](Status status) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(1u, fake_storage_->GetObjects().size());

    // No finished commit yet, with now two entries.
    const std::vector<std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    EXPECT_FALSE(journals[0]->IsCommitted());
    EXPECT_EQ(2u, journals[0]->GetData().size());
    storage::fake::FakeJournalDelegate::Entry entry =
        journals[0]->GetData().at(key2);
    EXPECT_EQ(object_id2, entry.value);
    EXPECT_FALSE(entry.deleted);
    EXPECT_EQ(storage::KeyPriority::LAZY, entry.priority);
    message_loop_.QuitNow();

  };
  page_ptr_->PutReference(convert::ToArray(key2), std::move(reference),
                          Priority::LAZY, put_reference_callback);
  message_loop_.Run();

  auto delete_callback = [this, &key2](Status status) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(1u, fake_storage_->GetObjects().size());

    // No finished commit yet, with the second entry deleted.
    const std::vector<std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    EXPECT_FALSE(journals[0]->IsCommitted());
    EXPECT_EQ(2u, journals[0]->GetData().size());
    storage::fake::FakeJournalDelegate::Entry entry =
        journals[0]->GetData().at(key2);
    EXPECT_TRUE(entry.deleted);
    message_loop_.QuitNow();

  };
  page_ptr_->Delete(convert::ToArray(key2), delete_callback);
  message_loop_.Run();

  page_ptr_->Commit([this](Status status) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(1u, fake_storage_->GetObjects().size());

    const std::vector<std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    EXPECT_TRUE(journals[0]->IsCommitted());
    EXPECT_EQ(2u, journals[0]->GetData().size());
    message_loop_.QuitNow();
  });
  message_loop_.Run();
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
    const std::vector<std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    EXPECT_TRUE(journals[0]->IsRolledBack());
    EXPECT_EQ(0u, journals[0]->GetData().size());
    message_loop_.QuitNow();
  });
  message_loop_.Run();
}

TEST_F(PageImplTest, NoTwoTransactions) {
  // Sequence of operations:
  //  - StartTransaction
  //  - StartTransaction
  page_ptr_->StartTransaction(
      [](Status status) { EXPECT_EQ(Status::OK, status); });
  page_ptr_->StartTransaction([this](Status status) {
    EXPECT_EQ(Status::TRANSACTION_ALREADY_IN_PROGRESS, status);
    message_loop_.QuitNow();
  });
  message_loop_.Run();
}

TEST_F(PageImplTest, NoTransactionCommit) {
  // Sequence of operations:
  //  - Commit
  page_ptr_->Commit([this](Status status) {
    EXPECT_EQ(Status::NO_TRANSACTION_IN_PROGRESS, status);
    message_loop_.QuitNow();
  });
  message_loop_.Run();
}

TEST_F(PageImplTest, NoTransactionRollback) {
  // Sequence of operations:
  //  - Rollback
  page_ptr_->Rollback([this](Status status) {
    EXPECT_EQ(Status::NO_TRANSACTION_IN_PROGRESS, status);
    message_loop_.QuitNow();
  });
  message_loop_.Run();
}

TEST_F(PageImplTest, CreateReference) {
  std::string value("a small value");
  Status status;
  ReferencePtr reference;
  page_ptr_->CreateReference(
      value.size(), mtl::WriteStringToConsumerHandle(value),
      [this, &status, &reference](Status received_status,
                                  ReferencePtr received_reference) {
        status = received_status;
        reference = std::move(received_reference);
        message_loop_.QuitNow();
      });
  message_loop_.Run();
  EXPECT_EQ(Status::OK, status);
  auto objects = fake_storage_->GetObjects();
  auto it = objects.find(reference->opaque_id);
  ASSERT_NE(objects.end(), it);
  ASSERT_EQ(value, it->second);
}

TEST_F(PageImplTest, GetReference) {
  std::string value_string("a small value");
  storage::ObjectId object_id;
  ASSERT_EQ(storage::Status::OK,
            fake_storage_->AddObjectFromLocal(
                mtl::WriteStringToConsumerHandle(value_string),
                value_string.size(), &object_id));
  ReferencePtr reference = Reference::New();
  reference->opaque_id = convert::ToArray(object_id);

  Status status;
  ValuePtr value;
  page_ptr_->GetReference(
      std::move(reference),
      [this, &status, &value](Status received_status, ValuePtr received_value) {
        status = received_status;
        value = std::move(received_value);
        message_loop_.QuitNow();
      });
  message_loop_.Run();
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(value->is_bytes());
  EXPECT_EQ(value_string, convert::ToString(value->get_bytes()));
}

TEST_F(PageImplTest, GetUnknownReference) {
  storage::ObjectId object_id("unknown reference");

  ReferencePtr reference = Reference::New();
  reference->opaque_id = convert::ToArray(object_id);

  Status status;
  page_ptr_->GetReference(
      std::move(reference),
      [this, &status](Status received_status, ValuePtr received_value) {
        status = received_status;
        message_loop_.QuitNow();
      });
  message_loop_.Run();
  EXPECT_EQ(Status::REFERENCE_NOT_FOUND, status);
}

}  // namespace
}  // namespace ledger

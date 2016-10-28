// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include <mojo/system/main.h>

#include "apps/ledger/api/ledger.mojom.h"
#include "apps/ledger/convert/convert.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/data_pipe/strings.h"
#include "lib/mtl/shared_buffer/strings.h"
#include "lib/mtl/tasks/message_loop.h"
#include "mojo/public/cpp/application/application_test_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/callback.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/environment/logging.h"

namespace ledger {
namespace {

mojo::Array<uint8_t> RandomArray(size_t size,
                                 const std::vector<uint8_t>& prefix) {
  EXPECT_TRUE(size >= prefix.size());
  mojo::Array<uint8_t> array = mojo::Array<uint8_t>::New(size);
  for (size_t i = 0; i < prefix.size(); ++i) {
    array[i] = prefix[i];
  }
  for (size_t i = prefix.size(); i < size / 4; ++i) {
    int random = std::rand();
    for (size_t j = 0; j < 4 && 4 * i + j < size; ++j) {
      array[4 * i + j] = random & 0xFF;
      random = random >> 8;
    }
  }
  return array;
}

mojo::Array<uint8_t> RandomArray(int size) {
  return RandomArray(size, std::vector<uint8_t>());
}

mojo::Array<uint8_t> PageGetId(PagePtr* page) {
  mojo::Array<uint8_t> page_id;
  (*page)->GetId(
      [&page_id](mojo::Array<uint8_t> id) { page_id = std::move(id); });
  EXPECT_TRUE(page->WaitForIncomingResponse());
  return page_id;
}

PageSnapshotPtr PageGetSnapshot(PagePtr* page) {
  PageSnapshotPtr snapshot;
  (*page)->GetSnapshot(GetProxy(&snapshot),
                       [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page->WaitForIncomingResponse());
  return snapshot;
}

mojo::Array<mojo::Array<uint8_t>> SnapshotGetKeys(PageSnapshotPtr* snapshot,
                                                  mojo::Array<uint8_t> prefix) {
  mojo::Array<mojo::Array<uint8_t>> result;
  (*snapshot)->GetKeys(
      std::move(prefix), nullptr,
      [&result](Status status, mojo::Array<mojo::Array<uint8_t>> keys,
                mojo::Array<uint8_t> next_token) {
        EXPECT_EQ(Status::OK, status);
        EXPECT_TRUE(next_token.is_null());
        result = std::move(keys);
      });
  EXPECT_TRUE(snapshot->WaitForIncomingResponse());
  return result;
}

mojo::Array<EntryPtr> SnapshotGetEntries(PageSnapshotPtr* snapshot,
                                         mojo::Array<uint8_t> prefix) {
  mojo::Array<EntryPtr> result;
  (*snapshot)->GetEntries(
      std::move(prefix), nullptr,
      [&result](Status status, mojo::Array<EntryPtr> entries,
                mojo::Array<uint8_t> next_token) {
        EXPECT_EQ(Status::OK, status);
        EXPECT_TRUE(next_token.is_null());
        result = std::move(entries);
      });
  EXPECT_TRUE(snapshot->WaitForIncomingResponse());
  return result;
}

std::string SnapshotGetPartial(PageSnapshotPtr* snapshot,
                               mojo::Array<uint8_t> key,
                               int64_t offset,
                               int64_t max_size) {
  std::string result;
  (*snapshot)->GetPartial(
      std::move(key), offset, max_size,
      [&result](Status status, mojo::ScopedSharedBufferHandle buffer) {
        EXPECT_EQ(status, Status::OK);
        EXPECT_TRUE(mtl::StringFromSharedBuffer(buffer, &result));
      });
  EXPECT_TRUE(snapshot->WaitForIncomingResponse());
  return result;
}

class LedgerApplicationTest : public mojo::test::ApplicationTestBase {
 public:
  LedgerApplicationTest() {}
  ~LedgerApplicationTest() override {}

 protected:
  // ApplicationTestBase:
  void SetUp() override {
    ApplicationTestBase::SetUp();
    ConnectToService(shell(), "mojo:ledger", GetProxy(&ledger_factory_));
    ledger_ = GetTestLedger();
    std::srand(0);
  }

  void TearDown() override {
    // Delete all pages used in the test.
    for (auto& page_id : page_ids_) {
      ledger_->DeletePage(std::move(page_id),
                          [](Status status) { EXPECT_EQ(Status::OK, status); });
      EXPECT_TRUE(ledger_.WaitForIncomingResponse());
    }

    ApplicationTestBase::TearDown();
  }

  LedgerPtr GetTestLedger();
  PagePtr GetTestPage();
  PagePtr GetPage(const mojo::Array<uint8_t>& page_id, Status expected_status);
  void DeletePage(const mojo::Array<uint8_t>& page_id, Status expected_status);

  LedgerFactoryPtr ledger_factory_;
  LedgerPtr ledger_;

 private:
  // Record ids of pages created for testing, so that we can delete them in
  // TearDown() in a somewhat desperate attempt to clean up the files created
  // for the test.
  // TODO(ppi): Configure ledger.mojo so that it knows to write to TempScopedDir
  // when run for testing and remove this accounting.
  std::vector<mojo::Array<uint8_t>> page_ids_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LedgerApplicationTest);
};

LedgerPtr LedgerApplicationTest::GetTestLedger() {
  Status status;
  mojo::InterfaceHandle<Ledger> ledger;
  IdentityPtr identity = Identity::New();
  identity->user_id = RandomArray(1);
  identity->app_id = RandomArray(1);
  ledger_factory_->GetLedger(std::move(identity), GetProxy(&ledger),
                             [&status](Status s) { status = s; });
  EXPECT_TRUE(ledger_factory_.WaitForIncomingResponse());

  EXPECT_EQ(Status::OK, status);
  return mojo::InterfacePtr<Ledger>::Create(std::move(ledger));
}

PagePtr LedgerApplicationTest::GetTestPage() {
  mojo::InterfaceHandle<Page> page;
  Status status;

  ledger_->NewPage(GetProxy(&page), [&status](Status s) { status = s; });
  EXPECT_TRUE(ledger_.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  PagePtr page_ptr = mojo::InterfacePtr<Page>::Create(std::move(page));

  mojo::Array<uint8_t> page_id;
  page_ptr->GetId(
      [&page_id](mojo::Array<uint8_t> id) { page_id = std::move(id); });
  EXPECT_TRUE(page_ptr.WaitForIncomingResponse());
  page_ids_.push_back(std::move(page_id));

  return page_ptr;
}

PagePtr LedgerApplicationTest::GetPage(const mojo::Array<uint8_t>& page_id,
                                       Status expected_status) {
  mojo::InterfaceHandle<Page> page;
  Status status;

  ledger_->GetPage(page_id.Clone(), GetProxy(&page),
                   [&status](Status s) { status = s; });
  EXPECT_TRUE(ledger_.WaitForIncomingResponse());
  EXPECT_EQ(expected_status, status);

  PagePtr page_ptr = mojo::InterfacePtr<Page>::Create(std::move(page));
  EXPECT_EQ(expected_status == Status::OK, page_ptr.get() != nullptr);

  return page_ptr;
}

void LedgerApplicationTest::DeletePage(const mojo::Array<uint8_t>& page_id,
                                       Status expected_status) {
  mojo::InterfaceHandle<Page> page;
  Status status;

  ledger_->DeletePage(page_id.Clone(),
                      [&status, &page](Status s) { status = s; });
  EXPECT_TRUE(ledger_.WaitForIncomingResponse());
  EXPECT_EQ(expected_status, status);

  page_ids_.erase(std::remove_if(page_ids_.begin(), page_ids_.end(),
                                 [&page_id](const mojo::Array<uint8_t>& id) {
                                   return id.Equals(page_id);
                                 }),
                  page_ids_.end());
}

TEST_F(LedgerApplicationTest, GetLedger) {
  EXPECT_NE(nullptr, ledger_.get());
}

TEST_F(LedgerApplicationTest, GetRootPage) {
  Status status;
  PagePtr page;
  ledger_->GetRootPage(GetProxy(&page), [&status](Status s) { status = s; });
  EXPECT_TRUE(ledger_.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);
}

TEST_F(LedgerApplicationTest, NewPage) {
  // Get two pages and check that their ids are different.
  PagePtr page1 = GetTestPage();
  mojo::Array<uint8_t> id1 = PageGetId(&page1);
  PagePtr page2 = GetTestPage();
  mojo::Array<uint8_t> id2 = PageGetId(&page2);

  EXPECT_TRUE(!id1.Equals(id2));
}

TEST_F(LedgerApplicationTest, GetPage) {
  // Create a page and expect to find it by its id.
  PagePtr page = GetTestPage();
  mojo::Array<uint8_t> id = PageGetId(&page);
  GetPage(id, Status::OK);

  // Search with a random id and expect a PAGE_NOT_FOUND result.
  mojo::Array<uint8_t> test_id = RandomArray(16);
  GetPage(test_id, Status::PAGE_NOT_FOUND);
}

// Verifies that a page can be connected to twice.
TEST_F(LedgerApplicationTest, MultiplePageConnections) {
  // Create a new page and find its id.
  PagePtr page1 = GetTestPage();
  mojo::Array<uint8_t> page_id_1 = PageGetId(&page1);

  // Connect to the same page again.
  PagePtr page2 = GetPage(page_id_1, Status::OK);
  mojo::Array<uint8_t> page_id_2 = PageGetId(&page2);
  EXPECT_EQ(convert::ToString(page_id_1), convert::ToString(page_id_2));
}

TEST_F(LedgerApplicationTest, DeletePage) {
  // Create a new page and find its id.
  PagePtr page = GetTestPage();
  mojo::Array<uint8_t> id = PageGetId(&page);

  // Delete the page.
  bool page_closed = false;
  page.set_connection_error_handler([&page_closed] { page_closed = true; });
  DeletePage(id, Status::OK);

  // Verify that deletion of the page closed the page connection.
  EXPECT_FALSE(page.WaitForIncomingResponse());
  EXPECT_TRUE(page_closed);

  // Verify that the deleted page cannot be retrieved.
  GetPage(id, Status::PAGE_NOT_FOUND);

  // Delete the same page again and expect a PAGE_NOT_FOUND result.
  DeletePage(id, Status::PAGE_NOT_FOUND);
}

TEST_F(LedgerApplicationTest, MultipleLedgerConnections) {
  // Connect to the same ledger instance twice.
  LedgerPtr ledger_connection_1 = GetTestLedger();
  LedgerPtr ledger_connection_2 = GetTestLedger();

  // Create a page on the first connection.
  PagePtr page;
  Status status;
  ledger_connection_1->NewPage(GetProxy(&page),
                               [&status](Status s) { status = s; });
  EXPECT_TRUE(ledger_connection_1.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  // Delete this page on the second connection and verify that the operation
  // succeeds.
  mojo::Array<uint8_t> id = PageGetId(&page);
  ledger_connection_2->DeletePage(std::move(id),
                                  [&status](Status s) { status = s; });
  EXPECT_TRUE(ledger_connection_2.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);
}

TEST_F(LedgerApplicationTest, PageSnapshotGet) {
  PagePtr page = GetTestPage();
  page->Put(convert::ToArray("name"), convert::ToArray("Alice"),
            [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  ValuePtr value;
  snapshot->Get(convert::ToArray("name"), [&value](Status status, ValuePtr v) {
    EXPECT_EQ(status, Status::OK);
    value = std::move(v);
  });
  EXPECT_TRUE(snapshot.WaitForIncomingResponse());
  EXPECT_TRUE(value->is_bytes());
  EXPECT_EQ("Alice", convert::ToString(value->get_bytes()));

  // Attempt to get an entry that is not in the page.
  snapshot->Get(convert::ToArray("favorite book"),
                [](Status status, ValuePtr v) {
                  // People don't read much these days.
                  EXPECT_EQ(status, Status::KEY_NOT_FOUND);
                });
  EXPECT_TRUE(snapshot.WaitForIncomingResponse());
}

TEST_F(LedgerApplicationTest, PageSnapshotGetPartial) {
  PagePtr page = GetTestPage();
  page->Put(convert::ToArray("name"), convert::ToArray("Alice"),
            [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  EXPECT_EQ("Alice",
            SnapshotGetPartial(&snapshot, convert::ToArray("name"), 0, -1));
  EXPECT_EQ("e",
            SnapshotGetPartial(&snapshot, convert::ToArray("name"), 4, -1));
  EXPECT_EQ("", SnapshotGetPartial(&snapshot, convert::ToArray("name"), 5, -1));
  EXPECT_EQ("", SnapshotGetPartial(&snapshot, convert::ToArray("name"), 6, -1));
  EXPECT_EQ("i", SnapshotGetPartial(&snapshot, convert::ToArray("name"), 2, 1));
  EXPECT_EQ("", SnapshotGetPartial(&snapshot, convert::ToArray("name"), 2, 0));

  // Negative offsets.
  EXPECT_EQ("Alice",
            SnapshotGetPartial(&snapshot, convert::ToArray("name"), -5, -1));
  EXPECT_EQ("e",
            SnapshotGetPartial(&snapshot, convert::ToArray("name"), -1, -1));
  EXPECT_EQ("", SnapshotGetPartial(&snapshot, convert::ToArray("name"), -5, 0));
  EXPECT_EQ("i",
            SnapshotGetPartial(&snapshot, convert::ToArray("name"), -3, 1));

  // Attempt to get an entry that is not in the page.
  snapshot->GetPartial(
      convert::ToArray("favorite book"), 0, -1,
      [](Status status, mojo::ScopedSharedBufferHandle received_buffer) {
        // People don't read much these days.
        EXPECT_EQ(status, Status::KEY_NOT_FOUND);
      });
  EXPECT_TRUE(snapshot.WaitForIncomingResponse());
}

TEST_F(LedgerApplicationTest, PageSnapshotGetKeys) {
  PagePtr page = GetTestPage();

  // Grab a snapshot before adding any entries and verify that GetKeys()
  // returns empty results.
  PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  mojo::Array<mojo::Array<uint8_t>> result =
      SnapshotGetKeys(&snapshot, mojo::Array<uint8_t>());
  EXPECT_EQ(0u, result.size());

  // Add entries and grab a new snapshot.
  const size_t N = 4;
  mojo::Array<uint8_t> keys[N] = {
      RandomArray(20, {0, 0, 0}), RandomArray(20, {0, 0, 1}),
      RandomArray(20, {0, 1, 0}), RandomArray(20, {0, 1, 1}),
  };
  for (size_t i = 0; i < N; ++i) {
    page->Put(keys[i].Clone(), RandomArray(50),
              [](Status status) { EXPECT_EQ(status, Status::OK); });
    EXPECT_TRUE(page.WaitForIncomingResponse());
  }
  snapshot = PageGetSnapshot(&page);

  // Get all keys.
  result = SnapshotGetKeys(&snapshot, mojo::Array<uint8_t>());
  EXPECT_EQ(N, result.size());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_TRUE(keys[i].Equals(result[i]));
  }

  // Get keys matching the prefix "0".
  result = SnapshotGetKeys(&snapshot,
                           mojo::Array<uint8_t>::From(std::vector<uint8_t>{0}));
  EXPECT_EQ(N, result.size());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_TRUE(keys[i].Equals(result[i]));
  }

  // Get keys matching the prefix "00".
  result = SnapshotGetKeys(
      &snapshot, mojo::Array<uint8_t>::From(std::vector<uint8_t>{0, 0}));
  EXPECT_EQ(2u, result.size());
  for (size_t i = 0; i < 2u; ++i) {
    EXPECT_TRUE(keys[i].Equals(result[i]));
  }

  // Get keys matching the prefix "010".
  result = SnapshotGetKeys(
      &snapshot, mojo::Array<uint8_t>::From(std::vector<uint8_t>{0, 1, 0}));
  EXPECT_EQ(1u, result.size());
  EXPECT_TRUE(keys[2].Equals(result[0]));

  // Get keys matching the prefix "5".
  result = SnapshotGetKeys(&snapshot,
                           mojo::Array<uint8_t>::From(std::vector<uint8_t>{5}));
  EXPECT_EQ(0u, result.size());
}

TEST_F(LedgerApplicationTest, PageSnapshotGetEntries) {
  PagePtr page = GetTestPage();

  // Grab a snapshot before adding any entries and verify that GetEntries()
  // returns empty results.
  PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  mojo::Array<EntryPtr> entries =
      SnapshotGetEntries(&snapshot, mojo::Array<uint8_t>());
  EXPECT_EQ(0u, entries.size());

  // Add entries and grab a new snapshot.
  const size_t N = 4;
  mojo::Array<uint8_t> keys[N] = {
      RandomArray(20, {0, 0, 0}), RandomArray(20, {0, 0, 1}),
      RandomArray(20, {0, 1, 0}), RandomArray(20, {0, 1, 1}),
  };
  mojo::Array<uint8_t> values[N] = {
      RandomArray(50), RandomArray(50), RandomArray(50), RandomArray(50),
  };
  for (size_t i = 0; i < N; ++i) {
    page->Put(keys[i].Clone(), values[i].Clone(),
              [](Status status) { EXPECT_EQ(status, Status::OK); });
    EXPECT_TRUE(page.WaitForIncomingResponse());
  }
  snapshot = PageGetSnapshot(&page);

  // Get all entries.
  entries = SnapshotGetEntries(&snapshot, mojo::Array<uint8_t>());
  EXPECT_EQ(N, entries.size());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_TRUE(keys[i].Equals(entries[i]->key));
    EXPECT_TRUE(values[i].Equals(entries[i]->value));
  }

  // Get entries matching the prefix "0".
  entries = SnapshotGetEntries(
      &snapshot, mojo::Array<uint8_t>::From(std::vector<uint8_t>{0}));
  EXPECT_EQ(N, entries.size());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_TRUE(keys[i].Equals(entries[i]->key));
    EXPECT_TRUE(values[i].Equals(entries[i]->value));
  }

  // Get entries matching the prefix "00".
  entries = SnapshotGetEntries(
      &snapshot, mojo::Array<uint8_t>::From(std::vector<uint8_t>{0, 0}));
  EXPECT_EQ(2u, entries.size());
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_TRUE(keys[i].Equals(entries[i]->key));
    EXPECT_TRUE(values[i].Equals(entries[i]->value));
  }

  // Get keys matching the prefix "010".
  entries = SnapshotGetEntries(
      &snapshot, mojo::Array<uint8_t>::From(std::vector<uint8_t>{0, 1, 0}));
  EXPECT_EQ(1u, entries.size());
  EXPECT_TRUE(keys[2].Equals(entries[0]->key));
  EXPECT_TRUE(values[2].Equals(entries[0]->value));

  // Get keys matching the prefix "5".
  snapshot->GetEntries(mojo::Array<uint8_t>::From(std::vector<uint8_t>{5}),
                       nullptr,
                       [&entries](Status status, mojo::Array<EntryPtr> e,
                                  mojo::Array<uint8_t> next_token) {
                         EXPECT_EQ(Status::OK, status);
                         EXPECT_TRUE(next_token.is_null());
                         entries = std::move(e);
                       });
  EXPECT_TRUE(snapshot.WaitForIncomingResponse());
  EXPECT_EQ(0u, entries.size());
}

TEST_F(LedgerApplicationTest, PageSnapshotGettersReturnSortedEntries) {
  PagePtr page = GetTestPage();

  const size_t N = 4;
  mojo::Array<uint8_t> keys[N] = {
      RandomArray(20, {2}), RandomArray(20, {5}), RandomArray(20, {3}),
      RandomArray(20, {0}),
  };
  mojo::Array<uint8_t> values[N] = {
      RandomArray(20), RandomArray(20), RandomArray(20), RandomArray(20),
  };
  for (size_t i = 0; i < N; ++i) {
    page->Put(keys[i].Clone(), values[i].Clone(),
              [](Status status) { EXPECT_EQ(status, Status::OK); });
    EXPECT_TRUE(page.WaitForIncomingResponse());
  }

  // Get a snapshot.
  PageSnapshotPtr snapshot = PageGetSnapshot(&page);

  // Verify that GetKeys() results are sorted.
  mojo::Array<mojo::Array<uint8_t>> result =
      SnapshotGetKeys(&snapshot, mojo::Array<uint8_t>());
  EXPECT_TRUE(keys[3].Equals(result[0]));
  EXPECT_TRUE(keys[0].Equals(result[1]));
  EXPECT_TRUE(keys[2].Equals(result[2]));
  EXPECT_TRUE(keys[1].Equals(result[3]));

  // Verify that GetEntries() results are sorted.
  mojo::Array<EntryPtr> entries =
      SnapshotGetEntries(&snapshot, mojo::Array<uint8_t>());
  EXPECT_TRUE(keys[3].Equals(entries[0]->key));
  EXPECT_TRUE(values[3].Equals(entries[0]->value));
  EXPECT_TRUE(keys[0].Equals(entries[1]->key));
  EXPECT_TRUE(values[0].Equals(entries[1]->value));
  EXPECT_TRUE(keys[2].Equals(entries[2]->key));
  EXPECT_TRUE(values[2].Equals(entries[2]->value));
  EXPECT_TRUE(keys[1].Equals(entries[3]->key));
  EXPECT_TRUE(values[1].Equals(entries[3]->value));
}

TEST_F(LedgerApplicationTest, PageCreateReferenceNegativeSize) {
  const std::string big_data(1'000'000, 'a');

  PagePtr page = GetTestPage();

  page->CreateReference(-1, mtl::WriteStringToConsumerHandle(big_data),
                        [this](Status status, ReferencePtr ref) {
                          EXPECT_EQ(Status::OK, status);
                        });
  ASSERT_TRUE(page.WaitForIncomingResponse());
}

TEST_F(LedgerApplicationTest, PageCreateReferenceWrongSize) {
  const std::string big_data(1'000'000, 'a');

  PagePtr page = GetTestPage();

  page->CreateReference(123, mtl::WriteStringToConsumerHandle(big_data),
                        [this](Status status, ReferencePtr ref) {
                          EXPECT_EQ(Status::IO_ERROR, status);
                        });
  ASSERT_TRUE(page.WaitForIncomingResponse());
}

TEST_F(LedgerApplicationTest, PageCreatePutLargeReference) {
  const std::string big_data(1'000'000, 'a');

  PagePtr page = GetTestPage();

  // Stream the data into the reference.
  ReferencePtr reference;
  page->CreateReference(big_data.size(),
                        mtl::WriteStringToConsumerHandle(big_data),
                        [this, &reference](Status status, ReferencePtr ref) {
                          EXPECT_EQ(Status::OK, status);
                          reference = std::move(ref);
                        });
  ASSERT_TRUE(page.WaitForIncomingResponse());

  // Set the reference uder a key.
  page->PutReference(convert::ToArray("big data"), std::move(reference),
                     Priority::EAGER,
                     [](Status status) { EXPECT_EQ(Status::OK, status); });
  ASSERT_TRUE(page.WaitForIncomingResponse());

  // Get a snapshot and read the value.
  PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  ValuePtr value;
  snapshot->Get(convert::ToArray("big data"),
                [&value](Status status, ValuePtr v) {
                  EXPECT_EQ(status, Status::OK);
                  value = std::move(v);
                });
  ASSERT_TRUE(snapshot.WaitForIncomingResponse());

  EXPECT_FALSE(value->is_bytes());
  EXPECT_TRUE(value->is_buffer());
  std::string retrieved_data;
  EXPECT_TRUE(
      mtl::StringFromSharedBuffer(value->get_buffer(), &retrieved_data));
  EXPECT_EQ(big_data, retrieved_data);
}

TEST_F(LedgerApplicationTest, PageSnapshotClosePageGet) {
  PagePtr page = GetTestPage();
  page->Put(convert::ToArray("name"), convert::ToArray("Alice"),
            [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  PageSnapshotPtr snapshot = PageGetSnapshot(&page);

  // Close the pipe. PageSnapshotPtr should remain valid.
  page.reset();

  ValuePtr value;
  snapshot->Get(convert::ToArray("name"), [&value](Status status, ValuePtr v) {
    EXPECT_EQ(status, Status::OK);
    value = std::move(v);
  });
  EXPECT_TRUE(snapshot.WaitForIncomingResponse());
  EXPECT_TRUE(value->is_bytes());
  EXPECT_EQ("Alice", convert::ToString(value->get_bytes()));

  // Attempt to get an entry that is not in the page.
  snapshot->Get(convert::ToArray("favorite book"),
                [](Status status, ValuePtr v) {
                  // People don't read much these days.
                  EXPECT_EQ(status, Status::KEY_NOT_FOUND);
                });
  EXPECT_TRUE(snapshot.WaitForIncomingResponse());
}

}  // namespace
}  // namespace ledger

MojoResult MojoMain(MojoHandle handle) {
  return mojo::test::RunAllTests(handle);
}

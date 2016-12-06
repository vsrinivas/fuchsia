// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include "apps/ledger/services/ledger.fidl.h"
#include "apps/ledger/src/app/ledger_repository_factory_impl.h"
#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/glue/socket/socket_pair.h"
#include "apps/ledger/src/glue/socket/socket_writer.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/socket/strings.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/create_thread.h"
#include "lib/mtl/vmo/strings.h"

namespace ledger {
namespace {

fidl::Array<uint8_t> RandomArray(size_t size,
                                 const std::vector<uint8_t>& prefix) {
  EXPECT_TRUE(size >= prefix.size());
  fidl::Array<uint8_t> array = fidl::Array<uint8_t>::New(size);
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

fidl::Array<uint8_t> RandomArray(int size) {
  return RandomArray(size, std::vector<uint8_t>());
}

fidl::Array<uint8_t> PageGetId(PagePtr* page) {
  fidl::Array<uint8_t> page_id;
  (*page)->GetId(
      [&page_id](fidl::Array<uint8_t> id) { page_id = std::move(id); });
  EXPECT_TRUE(page->WaitForIncomingResponse());
  return page_id;
}

PageSnapshotPtr PageGetSnapshot(PagePtr* page) {
  PageSnapshotPtr snapshot;
  (*page)->GetSnapshot(snapshot.NewRequest(),
                       [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page->WaitForIncomingResponse());
  return snapshot;
}

fidl::Array<fidl::Array<uint8_t>> SnapshotGetKeys(PageSnapshotPtr* snapshot,
                                                  fidl::Array<uint8_t> prefix) {
  fidl::Array<fidl::Array<uint8_t>> result;
  (*snapshot)->GetKeys(
      std::move(prefix), nullptr,
      [&result](Status status, fidl::Array<fidl::Array<uint8_t>> keys,
                fidl::Array<uint8_t> next_token) {
        EXPECT_EQ(Status::OK, status);
        EXPECT_TRUE(next_token.is_null());
        result = std::move(keys);
      });
  EXPECT_TRUE(snapshot->WaitForIncomingResponse());
  return result;
}

fidl::Array<EntryPtr> SnapshotGetEntries(PageSnapshotPtr* snapshot,
                                         fidl::Array<uint8_t> prefix) {
  fidl::Array<EntryPtr> result;
  (*snapshot)->GetEntries(
      std::move(prefix), nullptr,
      [&result](Status status, fidl::Array<EntryPtr> entries,
                fidl::Array<uint8_t> next_token) {
        EXPECT_EQ(Status::OK, status);
        EXPECT_TRUE(next_token.is_null());
        result = std::move(entries);
      });
  EXPECT_TRUE(snapshot->WaitForIncomingResponse());
  return result;
}

std::string SnapshotGetPartial(PageSnapshotPtr* snapshot,
                               fidl::Array<uint8_t> key,
                               int64_t offset,
                               int64_t max_size) {
  std::string result;
  (*snapshot)->GetPartial(std::move(key), offset, max_size,
                          [&result](Status status, mx::vmo buffer) {
                            EXPECT_EQ(status, Status::OK);
                            EXPECT_TRUE(mtl::StringFromVmo(buffer, &result));
                          });
  EXPECT_TRUE(snapshot->WaitForIncomingResponse());
  return result;
}

class LedgerRepositoryFactoryContainer {
 public:
  LedgerRepositoryFactoryContainer(
      ftl::RefPtr<ftl::TaskRunner> task_runner,
      const std::string& path,
      fidl::InterfaceRequest<LedgerRepositoryFactory> request)
      : environment_(configuration, task_runner, nullptr),
        factory_impl_(&environment_),
        factory_binding_(&factory_impl_, std::move(request)) {}
  ~LedgerRepositoryFactoryContainer() {}

 private:
  configuration::Configuration configuration;
  Environment environment_;
  LedgerRepositoryFactoryImpl factory_impl_;
  fidl::Binding<LedgerRepositoryFactory> factory_binding_;
};

class LedgerApplicationTest : public test::TestWithMessageLoop {
 public:
  LedgerApplicationTest() {}
  ~LedgerApplicationTest() override {}

 protected:
  // ::testing::Test:
  void SetUp() override {
    ::testing::Test::SetUp();
    thread_ = mtl::CreateThread(&task_runner_);
    task_runner_->PostTask(ftl::MakeCopyable(
        [ this, request = ledger_repository_factory_.NewRequest() ]() mutable {
          factory_container_ =
              std::make_unique<LedgerRepositoryFactoryContainer>(
                  task_runner_, tmp_dir_.path(), std::move(request));
        }));
    socket_thread_ = mtl::CreateThread(&socket_task_runner_);
    ledger_ = GetTestLedger();
    std::srand(0);
  }

  void TearDown() override {
    task_runner_->PostTask([this]() {
      mtl::MessageLoop::GetCurrent()->QuitNow();
      factory_container_.reset();
    });
    thread_.join();

    socket_task_runner_->PostTask(
        [this]() { mtl::MessageLoop::GetCurrent()->QuitNow(); });
    socket_thread_.join();

    ::testing::Test::TearDown();
  }

  mx::socket StreamDataToSocket(std::string data) {
    glue::SocketPair sockets;
    socket_task_runner_->PostTask(ftl::MakeCopyable([
      socket = std::move(sockets.socket1), data = std::move(data)
    ]() mutable {
      auto writer = new glue::SocketWriter();
      writer->Start(std::move(data), std::move(socket));
    }));
    return std::move(sockets.socket2);
  }

  LedgerPtr GetTestLedger();
  PagePtr GetTestPage();
  PagePtr GetPage(const fidl::Array<uint8_t>& page_id, Status expected_status);
  void DeletePage(const fidl::Array<uint8_t>& page_id, Status expected_status);

  LedgerRepositoryFactoryPtr ledger_repository_factory_;
  LedgerPtr ledger_;

 private:
  files::ScopedTempDir tmp_dir_;
  std::unique_ptr<LedgerRepositoryFactoryContainer> factory_container_;
  std::thread thread_;
  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  std::thread socket_thread_;
  ftl::RefPtr<ftl::TaskRunner> socket_task_runner_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LedgerApplicationTest);
};

class Watcher : public PageWatcher {
 public:
  Watcher(fidl::InterfaceRequest<PageWatcher> request,
          ftl::Closure change_callback)
      : binding_(this, std::move(request)), change_callback_(change_callback) {}

  PageChangePtr GetLastPageChange() { return last_page_change_.Clone(); }

  uint changes_seen = 0;

 private:
  // PageWatcher:
  void OnInitialState(fidl::InterfaceHandle<PageSnapshot> snapshot,
                      const OnInitialStateCallback& callback) override {
    callback();
  }

  void OnChange(PageChangePtr page_change,
                const OnChangeCallback& callback) override {
    changes_seen++;
    last_page_change_ = std::move(page_change);
    callback();
    change_callback_();
  }

  PageChangePtr last_page_change_;
  fidl::Binding<PageWatcher> binding_;
  ftl::Closure change_callback_;
};

LedgerPtr LedgerApplicationTest::GetTestLedger() {
  Status status;
  LedgerRepositoryPtr repository;
  ledger_repository_factory_->GetRepository(
      tmp_dir_.path(), repository.NewRequest(),
      [&status](Status s) { status = s; });
  EXPECT_TRUE(ledger_repository_factory_.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  LedgerPtr ledger;
  repository->GetLedger(RandomArray(1), ledger.NewRequest(),
                        [&status](Status s) { status = s; });
  EXPECT_TRUE(repository.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);
  return ledger;
}

PagePtr LedgerApplicationTest::GetTestPage() {
  fidl::InterfaceHandle<Page> page;
  Status status;

  ledger_->NewPage(page.NewRequest(), [&status](Status s) { status = s; });
  EXPECT_TRUE(ledger_.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  return fidl::InterfacePtr<Page>::Create(std::move(page));
}

PagePtr LedgerApplicationTest::GetPage(const fidl::Array<uint8_t>& page_id,
                                       Status expected_status) {
  PagePtr page_ptr;
  Status status;

  ledger_->GetPage(page_id.Clone(), page_ptr.NewRequest(),
                   [&status](Status s) { status = s; });
  EXPECT_TRUE(ledger_.WaitForIncomingResponse());
  EXPECT_EQ(expected_status, status);

  return page_ptr;
}

void LedgerApplicationTest::DeletePage(const fidl::Array<uint8_t>& page_id,
                                       Status expected_status) {
  fidl::InterfaceHandle<Page> page;
  Status status;

  ledger_->DeletePage(page_id.Clone(),
                      [&status, &page](Status s) { status = s; });
  EXPECT_TRUE(ledger_.WaitForIncomingResponse());
  EXPECT_EQ(expected_status, status);
}

TEST_F(LedgerApplicationTest, GetLedger) {
  EXPECT_NE(nullptr, ledger_.get());
}

TEST_F(LedgerApplicationTest, GetRootPage) {
  Status status;
  PagePtr page;
  ledger_->GetRootPage(page.NewRequest(), [&status](Status s) { status = s; });
  EXPECT_TRUE(ledger_.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);
}

TEST_F(LedgerApplicationTest, NewPage) {
  // Get two pages and check that their ids are different.
  PagePtr page1 = GetTestPage();
  fidl::Array<uint8_t> id1 = PageGetId(&page1);
  PagePtr page2 = GetTestPage();
  fidl::Array<uint8_t> id2 = PageGetId(&page2);

  EXPECT_TRUE(!id1.Equals(id2));
}

TEST_F(LedgerApplicationTest, GetPage) {
  // Create a page and expect to find it by its id.
  PagePtr page = GetTestPage();
  fidl::Array<uint8_t> id = PageGetId(&page);
  GetPage(id, Status::OK);

// TODO(etiennej): Reactivate after LE-87 is fixed.
#if 0
  // Search with a random id and expect a PAGE_NOT_FOUND result.
  fidl::Array<uint8_t> test_id = RandomArray(16);
  GetPage(test_id, Status::PAGE_NOT_FOUND);
#endif
}

// Verifies that a page can be connected to twice.
TEST_F(LedgerApplicationTest, MultiplePageConnections) {
  // Create a new page and find its id.
  PagePtr page1 = GetTestPage();
  fidl::Array<uint8_t> page_id_1 = PageGetId(&page1);

  // Connect to the same page again.
  PagePtr page2 = GetPage(page_id_1, Status::OK);
  fidl::Array<uint8_t> page_id_2 = PageGetId(&page2);
  EXPECT_EQ(convert::ToString(page_id_1), convert::ToString(page_id_2));
}

TEST_F(LedgerApplicationTest, DeletePage) {
  // Create a new page and find its id.
  PagePtr page = GetTestPage();
  fidl::Array<uint8_t> id = PageGetId(&page);

  // Delete the page.
  bool page_closed = false;
  page.set_connection_error_handler([&page_closed] { page_closed = true; });
  DeletePage(id, Status::OK);

  // Verify that deletion of the page closed the page connection.
  EXPECT_FALSE(page.WaitForIncomingResponse());
  EXPECT_TRUE(page_closed);

// TODO(etiennej): Reactivate after LE-87 is fixed.
#if 0
  // Verify that the deleted page cannot be retrieved.
  GetPage(id, Status::PAGE_NOT_FOUND);
#endif

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
  ledger_connection_1->NewPage(page.NewRequest(),
                               [&status](Status s) { status = s; });
  EXPECT_TRUE(ledger_connection_1.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  // Delete this page on the second connection and verify that the operation
  // succeeds.
  fidl::Array<uint8_t> id = PageGetId(&page);
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
  snapshot->GetPartial(convert::ToArray("favorite book"), 0, -1,
                       [](Status status, mx::vmo received_buffer) {
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
  fidl::Array<fidl::Array<uint8_t>> result =
      SnapshotGetKeys(&snapshot, fidl::Array<uint8_t>());
  EXPECT_EQ(0u, result.size());

  // Add entries and grab a new snapshot.
  const size_t N = 4;
  fidl::Array<uint8_t> keys[N] = {
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
  result = SnapshotGetKeys(&snapshot, fidl::Array<uint8_t>());
  EXPECT_EQ(N, result.size());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_TRUE(keys[i].Equals(result[i]));
  }

  // Get keys matching the prefix "0".
  result = SnapshotGetKeys(&snapshot,
                           fidl::Array<uint8_t>::From(std::vector<uint8_t>{0}));
  EXPECT_EQ(N, result.size());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_TRUE(keys[i].Equals(result[i]));
  }

  // Get keys matching the prefix "00".
  result = SnapshotGetKeys(
      &snapshot, fidl::Array<uint8_t>::From(std::vector<uint8_t>{0, 0}));
  EXPECT_EQ(2u, result.size());
  for (size_t i = 0; i < 2u; ++i) {
    EXPECT_TRUE(keys[i].Equals(result[i]));
  }

  // Get keys matching the prefix "010".
  result = SnapshotGetKeys(
      &snapshot, fidl::Array<uint8_t>::From(std::vector<uint8_t>{0, 1, 0}));
  EXPECT_EQ(1u, result.size());
  EXPECT_TRUE(keys[2].Equals(result[0]));

  // Get keys matching the prefix "5".
  result = SnapshotGetKeys(&snapshot,
                           fidl::Array<uint8_t>::From(std::vector<uint8_t>{5}));
  EXPECT_EQ(0u, result.size());
}

TEST_F(LedgerApplicationTest, PageSnapshotGetEntries) {
  PagePtr page = GetTestPage();

  // Grab a snapshot before adding any entries and verify that GetEntries()
  // returns empty results.
  PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  fidl::Array<EntryPtr> entries =
      SnapshotGetEntries(&snapshot, fidl::Array<uint8_t>());
  EXPECT_EQ(0u, entries.size());

  // Add entries and grab a new snapshot.
  const size_t N = 4;
  fidl::Array<uint8_t> keys[N] = {
      RandomArray(20, {0, 0, 0}), RandomArray(20, {0, 0, 1}),
      RandomArray(20, {0, 1, 0}), RandomArray(20, {0, 1, 1}),
  };
  fidl::Array<uint8_t> values[N] = {
      RandomArray(50), RandomArray(50), RandomArray(50), RandomArray(50),
  };
  for (size_t i = 0; i < N; ++i) {
    page->Put(keys[i].Clone(), values[i].Clone(),
              [](Status status) { EXPECT_EQ(status, Status::OK); });
    EXPECT_TRUE(page.WaitForIncomingResponse());
  }
  snapshot = PageGetSnapshot(&page);

  // Get all entries.
  entries = SnapshotGetEntries(&snapshot, fidl::Array<uint8_t>());
  EXPECT_EQ(N, entries.size());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_TRUE(keys[i].Equals(entries[i]->key));
    EXPECT_TRUE(values[i].Equals(entries[i]->value));
  }

  // Get entries matching the prefix "0".
  entries = SnapshotGetEntries(
      &snapshot, fidl::Array<uint8_t>::From(std::vector<uint8_t>{0}));
  EXPECT_EQ(N, entries.size());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_TRUE(keys[i].Equals(entries[i]->key));
    EXPECT_TRUE(values[i].Equals(entries[i]->value));
  }

  // Get entries matching the prefix "00".
  entries = SnapshotGetEntries(
      &snapshot, fidl::Array<uint8_t>::From(std::vector<uint8_t>{0, 0}));
  EXPECT_EQ(2u, entries.size());
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_TRUE(keys[i].Equals(entries[i]->key));
    EXPECT_TRUE(values[i].Equals(entries[i]->value));
  }

  // Get keys matching the prefix "010".
  entries = SnapshotGetEntries(
      &snapshot, fidl::Array<uint8_t>::From(std::vector<uint8_t>{0, 1, 0}));
  EXPECT_EQ(1u, entries.size());
  EXPECT_TRUE(keys[2].Equals(entries[0]->key));
  EXPECT_TRUE(values[2].Equals(entries[0]->value));

  // Get keys matching the prefix "5".
  snapshot->GetEntries(fidl::Array<uint8_t>::From(std::vector<uint8_t>{5}),
                       nullptr,
                       [&entries](Status status, fidl::Array<EntryPtr> e,
                                  fidl::Array<uint8_t> next_token) {
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
  fidl::Array<uint8_t> keys[N] = {
      RandomArray(20, {2}), RandomArray(20, {5}), RandomArray(20, {3}),
      RandomArray(20, {0}),
  };
  fidl::Array<uint8_t> values[N] = {
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
  fidl::Array<fidl::Array<uint8_t>> result =
      SnapshotGetKeys(&snapshot, fidl::Array<uint8_t>());
  EXPECT_TRUE(keys[3].Equals(result[0]));
  EXPECT_TRUE(keys[0].Equals(result[1]));
  EXPECT_TRUE(keys[2].Equals(result[2]));
  EXPECT_TRUE(keys[1].Equals(result[3]));

  // Verify that GetEntries() results are sorted.
  fidl::Array<EntryPtr> entries =
      SnapshotGetEntries(&snapshot, fidl::Array<uint8_t>());
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

  page->CreateReference(-1, StreamDataToSocket(big_data),
                        [this](Status status, ReferencePtr ref) {
                          EXPECT_EQ(Status::OK, status);
                        });
  ASSERT_TRUE(page.WaitForIncomingResponse());
}

TEST_F(LedgerApplicationTest, PageCreateReferenceWrongSize) {
  const std::string big_data(1'000'000, 'a');

  PagePtr page = GetTestPage();

  page->CreateReference(123, StreamDataToSocket(big_data),
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
  page->CreateReference(big_data.size(), StreamDataToSocket(big_data),
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
  EXPECT_TRUE(mtl::StringFromVmo(value->get_buffer(), &retrieved_data));
  EXPECT_EQ(big_data, retrieved_data);
}

TEST_F(LedgerApplicationTest, PageSnapshotClosePageGet) {
  PagePtr page = GetTestPage();
  page->Put(convert::ToArray("name"), convert::ToArray("Alice"),
            [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  PageSnapshotPtr snapshot = PageGetSnapshot(&page);

  // Close the channel. PageSnapshotPtr should remain valid.
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

TEST_F(LedgerApplicationTest, PageGetById) {
  PagePtr page = GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  page->Put(convert::ToArray("name"), convert::ToArray("Alice"),
            [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  page.reset();

  page = GetPage(test_page_id, Status::OK);
  page->GetId([&test_page_id, this](fidl::Array<uint8_t> page_id) {
    EXPECT_EQ(convert::ToString(test_page_id), convert::ToString(page_id));
  });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  ValuePtr value;
  snapshot->Get(convert::ToArray("name"),
                [&value, this](Status status, ValuePtr v) {
                  EXPECT_EQ(status, Status::OK);
                  value = std::move(v);
                });
  EXPECT_TRUE(snapshot.WaitForIncomingResponse());
  EXPECT_TRUE(value->is_bytes());
  EXPECT_EQ("Alice", convert::ToString(value->get_bytes()));
}

TEST_F(LedgerApplicationTest, PageWatcherSimple) {
  PagePtr page = GetTestPage();
  PageWatcherPtr watcher_ptr;
  Watcher watcher(watcher_ptr.NewRequest(),
                  [this]() { mtl::MessageLoop::GetCurrent()->QuitNow(); });

  page->Watch(std::move(watcher_ptr),
              [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  page->Put(convert::ToArray("name"), convert::ToArray("Alice"),
            [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(1u, watcher.changes_seen);
  PageChangePtr change = watcher.GetLastPageChange();
  EXPECT_EQ(1u, change->changes.size());
  EXPECT_EQ("name", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Alice",
            convert::ToString(change->changes[0]->new_value->get_bytes()));
}

TEST_F(LedgerApplicationTest, PageWatcherTransaction) {
  PagePtr page = GetTestPage();
  PageWatcherPtr watcher_ptr;
  Watcher watcher(watcher_ptr.NewRequest(),
                  [this]() { mtl::MessageLoop::GetCurrent()->QuitNow(); });

  page->Watch(std::move(watcher_ptr),
              [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  page->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());
  page->Put(convert::ToArray("name"), convert::ToArray("Alice"),
            [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      [] { mtl::MessageLoop::GetCurrent()->QuitNow(); },
      ftl::TimeDelta::FromSeconds(1));
  mtl::MessageLoop::GetCurrent()->Run();
  EXPECT_EQ(0u, watcher.changes_seen);

  page->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(1u, watcher.changes_seen);
  PageChangePtr change = watcher.GetLastPageChange();
  EXPECT_EQ(1u, change->changes.size());
  EXPECT_EQ("name", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Alice",
            convert::ToString(change->changes[0]->new_value->get_bytes()));
}

TEST_F(LedgerApplicationTest, PageWatcherParallel) {
  PagePtr page1 = GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  PagePtr page2 = GetPage(test_page_id, Status::OK);

  PageWatcherPtr watcher1_ptr;
  Watcher watcher1(watcher1_ptr.NewRequest(),
                   [this]() { mtl::MessageLoop::GetCurrent()->QuitNow(); });
  page1->Watch(std::move(watcher1_ptr),
               [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  PageWatcherPtr watcher2_ptr;
  Watcher watcher2(watcher2_ptr.NewRequest(),
                   [this]() { mtl::MessageLoop::GetCurrent()->QuitNow(); });
  page2->Watch(std::move(watcher2_ptr),
               [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page2->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(convert::ToArray("name"), convert::ToArray("Bob"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  // Verify that each change is seen by the right watcher.
  page1->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  mtl::MessageLoop::GetCurrent()->Run();
  EXPECT_EQ(1u, watcher1.changes_seen);
  PageChangePtr change = watcher1.GetLastPageChange();
  EXPECT_EQ(1u, change->changes.size());
  EXPECT_EQ("name", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Alice",
            convert::ToString(change->changes[0]->new_value->get_bytes()));

  page2->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  mtl::MessageLoop::GetCurrent()->Run();

  EXPECT_EQ(1u, watcher2.changes_seen);
  change = watcher2.GetLastPageChange();
  EXPECT_EQ(1u, change->changes.size());
  EXPECT_EQ("name", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Bob",
            convert::ToString(change->changes[0]->new_value->get_bytes()));

  mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      [] { mtl::MessageLoop::GetCurrent()->QuitNow(); },
      ftl::TimeDelta::FromSeconds(1));
  mtl::MessageLoop::GetCurrent()->Run();
  // A merge happens now. Only the first watcher should see a change.
  EXPECT_EQ(2u, watcher1.changes_seen);
  EXPECT_EQ(1u, watcher2.changes_seen);

  change = watcher1.GetLastPageChange();
  EXPECT_EQ(1u, change->changes.size());
  EXPECT_EQ("name", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Bob",
            convert::ToString(change->changes[0]->new_value->get_bytes()));
}

TEST_F(LedgerApplicationTest, PageWatcherEmptyTransaction) {
  PagePtr page = GetTestPage();
  PageWatcherPtr watcher_ptr;
  Watcher watcher(watcher_ptr.NewRequest(),
                  [this]() { mtl::MessageLoop::GetCurrent()->QuitNow(); });

  page->Watch(std::move(watcher_ptr),
              [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  page->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  page->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());
  mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      [] { mtl::MessageLoop::GetCurrent()->QuitNow(); },
      ftl::TimeDelta::FromSeconds(1));
  mtl::MessageLoop::GetCurrent()->Run();
  EXPECT_EQ(0u, watcher.changes_seen);
}

TEST_F(LedgerApplicationTest, PageWatcher1Change2Pages) {
  PagePtr page1 = GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  PagePtr page2 = GetPage(test_page_id, Status::OK);

  PageWatcherPtr watcher1_ptr;
  Watcher watcher1(watcher1_ptr.NewRequest(),
                   [this]() { mtl::MessageLoop::GetCurrent()->QuitNow(); });
  page1->Watch(std::move(watcher1_ptr),
               [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  PageWatcherPtr watcher2_ptr;
  Watcher watcher2(watcher2_ptr.NewRequest(),
                   [this]() { mtl::MessageLoop::GetCurrent()->QuitNow(); });
  page2->Watch(std::move(watcher2_ptr),
               [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_FALSE(RunLoopWithTimeout());

  ASSERT_EQ(1u, watcher1.changes_seen);
  PageChangePtr change = watcher1.GetLastPageChange();
  ASSERT_EQ(1u, change->changes.size());
  EXPECT_EQ("name", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Alice",
            convert::ToString(change->changes[0]->new_value->get_bytes()));

  ASSERT_EQ(1u, watcher2.changes_seen);
  change = watcher2.GetLastPageChange();
  ASSERT_EQ(1u, change->changes.size());
  EXPECT_EQ("name", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Alice",
            convert::ToString(change->changes[0]->new_value->get_bytes()));
}

TEST_F(LedgerApplicationTest, Merging) {
  PagePtr page1 = GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  PagePtr page2 = GetPage(test_page_id, Status::OK);

  PageWatcherPtr watcher1_ptr;
  Watcher watcher1(GetProxy(&watcher1_ptr),
                   [this]() { mtl::MessageLoop::GetCurrent()->QuitNow(); });
  page1->Watch(std::move(watcher1_ptr),
               [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  PageWatcherPtr watcher2_ptr;
  Watcher watcher2(GetProxy(&watcher2_ptr),
                   [this]() { mtl::MessageLoop::GetCurrent()->QuitNow(); });
  page2->Watch(std::move(watcher2_ptr),
               [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(convert::ToArray("city"), convert::ToArray("Paris"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page2->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(convert::ToArray("name"), convert::ToArray("Bob"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(convert::ToArray("phone"), convert::ToArray("0123456789"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  // Verify that each change is seen by the right watcher.
  page1->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  mtl::MessageLoop::GetCurrent()->Run();
  EXPECT_EQ(1u, watcher1.changes_seen);
  PageChangePtr change = watcher1.GetLastPageChange();
  EXPECT_EQ(2u, change->changes.size());
  EXPECT_EQ("city", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Paris",
            convert::ToString(change->changes[0]->new_value->get_bytes()));
  EXPECT_EQ("name", convert::ToString(change->changes[1]->key));
  EXPECT_EQ("Alice",
            convert::ToString(change->changes[1]->new_value->get_bytes()));

  page2->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  mtl::MessageLoop::GetCurrent()->Run();

  EXPECT_EQ(1u, watcher2.changes_seen);
  change = watcher2.GetLastPageChange();
  EXPECT_EQ(2u, change->changes.size());
  EXPECT_EQ("name", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Bob",
            convert::ToString(change->changes[0]->new_value->get_bytes()));
  EXPECT_EQ("phone", convert::ToString(change->changes[1]->key));
  EXPECT_EQ("0123456789",
            convert::ToString(change->changes[1]->new_value->get_bytes()));

  mtl::MessageLoop::GetCurrent()->Run();
  mtl::MessageLoop::GetCurrent()->Run();
  // Each change is seen once, and by the correct watcher only.
  EXPECT_EQ(2u, watcher1.changes_seen);
  change = watcher1.GetLastPageChange();
  EXPECT_EQ(2u, change->changes.size());
  EXPECT_EQ("name", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Bob",
            convert::ToString(change->changes[0]->new_value->get_bytes()));
  EXPECT_EQ("phone", convert::ToString(change->changes[1]->key));
  EXPECT_EQ("0123456789",
            convert::ToString(change->changes[1]->new_value->get_bytes()));

  EXPECT_EQ(2u, watcher2.changes_seen);
  change = watcher2.GetLastPageChange();
  EXPECT_EQ(1u, change->changes.size());
  EXPECT_EQ("city", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Paris",
            convert::ToString(change->changes[0]->new_value->get_bytes()));
}

}  // namespace
}  // namespace ledger

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include "apps/ledger/api/ledger.mojom.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/data_pipe/strings.h"
#include "lib/mtl/shared_buffer/strings.h"
#include "lib/mtl/tasks/message_loop.h"
#include "mojo/public/cpp/application/application_test_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/callback.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/environment/logging.h"
#include "mojo/public/cpp/system/macros.h"

namespace ledger {
namespace {

std::string ToString(const mojo::Array<uint8_t>& array) {
  return std::string(reinterpret_cast<const char*>(array.data()), array.size());
}

bool IsValueEqual(const mojo::Array<uint8_t>& expected_value,
                  const ValuePtr& value) {
  if (expected_value.is_null()) {
    return value.is_null();
  }

  if (value->is_bytes()) {
    return expected_value.Equals(value->get_bytes());
  }

  std::string content;
  EXPECT_TRUE(
      mtl::StringFromSharedBuffer(std::move(value->get_buffer()), &content));
  return ToString(expected_value) == content;
}

mojo::Array<uint8_t> GetPageId(PagePtr page) {
  mojo::Array<uint8_t> pageId;
  page->GetId([&pageId](mojo::Array<uint8_t> id) { pageId = std::move(id); });
  EXPECT_TRUE(page.WaitForIncomingResponse());
  return pageId;
}

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

void ExpectEqual(const mojo::Array<EntryPtr>& entries,
                 mojo::Array<uint8_t> expectedKeys[],
                 mojo::Array<uint8_t> expectedValues[],
                 int expectedSize) {
  EXPECT_EQ(static_cast<size_t>(expectedSize), entries.size());
  for (size_t i = 0; i < entries.size(); ++i) {
    bool found = false;

    for (size_t j = 0; j < entries.size(); ++j) {
      if (expectedKeys[j].Equals(entries[i]->key)) {
        // Check the values.
        EXPECT_TRUE(expectedValues[j].Equals(entries[i]->value));
        found = true;
        break;
      }
    }
    if (found == false) {
      FAIL();
    }
  }
}

void ExpectEqual(const mojo::Array<EntryChangePtr>& entries,
                 mojo::Array<uint8_t> expectedKeys[],
                 mojo::Array<uint8_t> expectedValues[],
                 int expectedSize) {
  EXPECT_EQ(static_cast<size_t>(expectedSize), entries.size());
  for (size_t i = 0; i < entries.size(); ++i) {
    bool found = false;

    for (size_t j = 0; j < entries.size(); ++j) {
      if (expectedKeys[j].Equals(entries[i]->key)) {
        // Check the values.
        if (entries[i]->new_value.get() == nullptr) {
          EXPECT_TRUE(expectedValues[j].is_null());
        } else {
          EXPECT_TRUE(
              expectedValues[j].Equals(entries[i]->new_value->get_bytes()));
        }
        found = true;
        break;
      }
    }
    if (found == false) {
      FAIL();
    }
  }
}

void ExpectEqual(const mojo::Array<mojo::Array<uint8_t>>& result,
                 mojo::Array<uint8_t> expectedKeys[],
                 int expectedSize) {
  EXPECT_EQ(static_cast<size_t>(expectedSize), result.size());
  for (size_t i = 0; i < result.size(); ++i) {
    bool found = false;
    for (size_t j = 0; j < result.size(); ++j) {
      if (expectedKeys[j].Equals(result[i])) {
        found = true;
        break;
      }
    }
    if (found == false) {
      FAIL();
    }
  }
}

void ExpectSorted(const mojo::Array<EntryPtr>& entries) {
  if (entries.size() < 2) {
    return;
  }
  for (size_t i = 1; i < entries.size(); ++i) {
    size_t minSize =
        std::min(entries[i]->key.size(), entries[i - 1]->key.size());
    int n = memcmp(entries[i]->key.data(), entries[i - 1]->key.data(), minSize);
    if (n == 0) {
      EXPECT_TRUE(entries[i]->key.size() > entries[i - 1]->key.size());
    } else {
      EXPECT_TRUE(n > 0);
    }
  }
}

void ExpectSorted(const mojo::Array<mojo::Array<uint8_t>>& keys) {
  if (keys.size() < 2) {
    return;
  }
  for (size_t i = 1; i < keys.size(); ++i) {
    size_t minSize = std::min(keys[i].size(), keys[i - 1].size());
    int n = memcmp(keys[i].data(), keys[i - 1].data(), minSize);
    if (n == 0) {
      EXPECT_TRUE(keys[i].size() > keys[i - 1].size());
    } else {
      EXPECT_TRUE(n > 0);
    }
  }
}

// Test implemention of PageWatcher.
class PageWatcherTest : public PageWatcher {
 public:
  PageWatcherTest(mojo::InterfaceRequest<PageWatcher> request)
      : binding_(this, std::move(request)) {}
  ~PageWatcherTest() override {}

  void CheckInitialState(mojo::Array<uint8_t> expectedKeys[],
                         mojo::Array<uint8_t> expectedValues[],
                         int expectedSize) {
    EXPECT_TRUE(on_inital_state_called_);
    ExpectEqual(initial_entries_, expectedKeys, expectedValues, expectedSize);
  }

  void CheckLastOnChange(mojo::Array<uint8_t> expectedKeys[],
                         mojo::Array<uint8_t> expectedValues[],
                         int expectedSize) {
    EXPECT_TRUE(on_change_called_);
    ExpectEqual(last_change_, expectedKeys, expectedValues, expectedSize);
  }

 private:
  // OnInitialState(PageSnapshot snapshot) => ();
  void OnInitialState(mojo::InterfaceHandle<PageSnapshot> snapshot,
                      const OnInitialStateCallback& callback) override {
    on_inital_state_called_ = true;
    PageSnapshotPtr ptr =
        mojo::InterfacePtr<PageSnapshot>::Create(std::move(snapshot));
    Status status;
    ptr->GetEntries(nullptr,
                    [&status, this](Status s, mojo::Array<EntryPtr> e) {
                      status = s;
                      initial_entries_ = std::move(e);
                    });
    EXPECT_TRUE(ptr.WaitForIncomingResponse());
    EXPECT_EQ(status, Status::OK);
    callback.Run();
    mtl::MessageLoop::GetCurrent()->QuitNow();
  }

  // OnChange(PageChange pageChange) => ();
  void OnChange(PageChangePtr changes,
                const OnChangeCallback& callback) override {
    on_change_called_ = true;
    last_change_ = std::move(changes->changes);
    callback.Run();
    mtl::MessageLoop::GetCurrent()->QuitNow();
  }

  mojo::Binding<PageWatcher> binding_;
  bool on_inital_state_called_ = false;
  bool on_change_called_ = false;
  mojo::Array<EntryPtr> initial_entries_;
  mojo::Array<EntryChangePtr> last_change_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PageWatcherTest);
};

class LedgerApplicationTest : public mojo::test::ApplicationTestBase {
 public:
  LedgerApplicationTest() : ApplicationTestBase() {}
  ~LedgerApplicationTest() override {}

 protected:
  // ApplicationTestBase:
  void SetUp() override {
    ApplicationTestBase::SetUp();

    ledger_ = GetTestLedger();
    std::srand(0);
  }

  void TearDown() override {
    // Delete all pages used in the test.
    for (int i = page_ids_.size() - 1; i >= 0; --i) {
      DeletePage(page_ids_[i], Status::OK);
    }

    ApplicationTestBase::TearDown();
  }

  PagePtr GetTestPage();
  PagePtr GetPage(const mojo::Array<uint8_t>& pageId, Status expected_status);
  void DeletePage(const mojo::Array<uint8_t>& pageId, Status expected_status);
  void Put(PagePtr* page, mojo::Array<uint8_t> key, mojo::Array<uint8_t> value);
  void PutReference(PagePtr* page,
                    mojo::Array<uint8_t> key,
                    ReferencePtr reference,
                    Priority priority,
                    Status expected_status);
  ReferencePtr CreateReference(PagePtr* page,
                               const std::string& value,
                               int64_t size,
                               Status expected_status);
  void GetReference(PagePtr* page,
                    ReferencePtr reference,
                    Status expected_status,
                    const mojo::Array<uint8_t>& expected_value);
  void GetPartialReference(PagePtr* page,
                           ReferencePtr reference,
                           int64_t offset,
                           int64_t max_size,
                           Status expected_status,
                           const mojo::Array<uint8_t>& expected_value);
  void Get(PageSnapshotPtr* snapshot,
           mojo::Array<uint8_t> key,
           Status expected_status,
           const mojo::Array<uint8_t>& expected_value);
  void Delete(PagePtr* page, mojo::Array<uint8_t> key, Status expected_status);
  PageSnapshotPtr GetSnapshot(PagePtr* page);

  LedgerPtr ledger_;
  std::vector<mojo::Array<uint8_t>> page_ids_;

 private:
  LedgerPtr GetTestLedger();

  MOJO_DISALLOW_COPY_AND_ASSIGN(LedgerApplicationTest);
};

LedgerPtr LedgerApplicationTest::GetTestLedger() {
  LedgerFactoryPtr ledgerFactory;
  ConnectToService(shell(), "mojo:ledger", GetProxy(&ledgerFactory));

  Status status;
  mojo::InterfaceHandle<Ledger> ledger;
  IdentityPtr identity = Identity::New();
  identity->user_id = RandomArray(1);
  identity->app_id = RandomArray(1);
  ledgerFactory->GetLedger(
      std::move(identity),
      [&status, &ledger](Status s, mojo::InterfaceHandle<Ledger> l) {
        status = s;
        ledger = std::move(l);
      });
  EXPECT_TRUE(ledgerFactory.WaitForIncomingResponse());

  EXPECT_EQ(status, Status::OK);
  return mojo::InterfacePtr<Ledger>::Create(std::move(ledger));
}

PagePtr LedgerApplicationTest::GetTestPage() {
  mojo::InterfaceHandle<Page> page;
  Status status;

  ledger_->NewPage([&status, &page](Status s, mojo::InterfaceHandle<Page> p) {
    status = s;
    page = std::move(p);
  });
  EXPECT_TRUE(ledger_.WaitForIncomingResponse());
  EXPECT_EQ(status, Status::OK);

  PagePtr pagePtr = mojo::InterfacePtr<Page>::Create(std::move(page));

  mojo::Array<uint8_t> pageId;
  pagePtr->GetId(
      [&pageId](mojo::Array<uint8_t> id) { pageId = std::move(id); });
  EXPECT_TRUE(pagePtr.WaitForIncomingResponse());
  page_ids_.push_back(std::move(pageId));

  return pagePtr;
}

PagePtr LedgerApplicationTest::GetPage(const mojo::Array<uint8_t>& pageId,
                                       Status expected_status) {
  mojo::InterfaceHandle<Page> page;
  Status status;

  ledger_->GetPage(pageId.Clone(),
                   [&status, &page](Status s, mojo::InterfaceHandle<Page> p) {
                     status = s;
                     page = std::move(p);
                   });
  EXPECT_TRUE(ledger_.WaitForIncomingResponse());
  EXPECT_EQ(expected_status, status);

  PagePtr pagePtr = mojo::InterfacePtr<Page>::Create(std::move(page));
  EXPECT_EQ(pagePtr.get() != nullptr, expected_status == Status::OK);

  return pagePtr;
}

void LedgerApplicationTest::DeletePage(const mojo::Array<uint8_t>& pageId,
                                       Status expected_status) {
  mojo::InterfaceHandle<Page> page;
  Status status;

  ledger_->DeletePage(pageId.Clone(),
                      [&status, &page](Status s) { status = s; });
  EXPECT_TRUE(ledger_.WaitForIncomingResponse());
  EXPECT_EQ(expected_status, status);

  page_ids_.erase(std::remove_if(page_ids_.begin(), page_ids_.end(),
                                 [&pageId](const mojo::Array<uint8_t>& id) {
                                   return id.Equals(pageId);
                                 }),
                  page_ids_.end());
}

PageSnapshotPtr LedgerApplicationTest::GetSnapshot(PagePtr* page) {
  mojo::InterfaceHandle<PageSnapshot> snapshot;
  Status status;

  (*page)->GetSnapshot(
      [&status, &snapshot](Status s, mojo::InterfaceHandle<PageSnapshot> sn) {
        status = s;
        snapshot = std::move(sn);
      });
  EXPECT_TRUE(page->WaitForIncomingResponse());
  EXPECT_EQ(status, Status::OK);

  PageSnapshotPtr ptr =
      mojo::InterfacePtr<PageSnapshot>::Create(std::move(snapshot));
  EXPECT_TRUE(ptr.get() != nullptr);

  return ptr;
}

void LedgerApplicationTest::Put(PagePtr* page,
                                mojo::Array<uint8_t> key,
                                mojo::Array<uint8_t> value) {
  Status status;
  (*page)->Put(std::move(key), std::move(value),
               [&status](Status s) { status = s; });
  EXPECT_TRUE(page->WaitForIncomingResponse());
  EXPECT_EQ(status, Status::OK);
}

void LedgerApplicationTest::PutReference(PagePtr* page,
                                         mojo::Array<uint8_t> key,
                                         ReferencePtr reference,
                                         Priority priority,
                                         Status expected_status) {
  Status status;
  (*page)->PutReference(std::move(key), std::move(reference), priority,
                        [&status](Status s) { status = s; });
  EXPECT_TRUE(page->WaitForIncomingResponse());
  EXPECT_EQ(expected_status, status);
}

ReferencePtr LedgerApplicationTest::CreateReference(PagePtr* page,
                                                    const std::string& value,
                                                    int64_t size,
                                                    Status expected_status) {
  Status status;
  ReferencePtr reference;
  (*page)->CreateReference(size, mtl::WriteStringToConsumerHandle(value),
                           [&status, &reference](Status s, ReferencePtr r) {
                             status = s;
                             reference = std::move(r);
                           });
  EXPECT_TRUE(page->WaitForIncomingResponse());
  EXPECT_EQ(expected_status, status);
  return reference;
}

void LedgerApplicationTest::GetReference(
    PagePtr* page,
    ReferencePtr reference,
    Status expected_status,
    const mojo::Array<uint8_t>& expected_value) {
  Status status;
  ValuePtr value;
  (*page)->GetReference(std::move(reference),
                        [&status, &value](Status s, ValuePtr v) {
                          status = s;
                          value = std::move(v);
                        });
  EXPECT_TRUE(page->WaitForIncomingResponse());
  EXPECT_EQ(expected_status, status);
  EXPECT_TRUE(IsValueEqual(expected_value, value));
}

void LedgerApplicationTest::GetPartialReference(
    PagePtr* page,
    ReferencePtr reference,
    int64_t offset,
    int64_t max_size,
    Status expected_status,
    const mojo::Array<uint8_t>& expected_value) {
  Status status;
  ValuePtr value;
  (*page)->GetPartialReference(
      std::move(reference), offset, max_size,
      [&status, &value](Status s, mojo::ScopedSharedBufferHandle buffer) {
        status = s;
        if (status == Status::OK) {
          value = Value::New();
          value->set_buffer(std::move(buffer));
        }
      });
  EXPECT_TRUE(page->WaitForIncomingResponse());
  EXPECT_EQ(expected_status, status);
  EXPECT_TRUE(IsValueEqual(expected_value, value));
}

void LedgerApplicationTest::Get(PageSnapshotPtr* snapshot,
                                mojo::Array<uint8_t> key,
                                Status expected_status,
                                const mojo::Array<uint8_t>& expected_value) {
  Status status;
  ValuePtr value;
  (*snapshot)->Get(std::move(key), [&status, &value](Status s, ValuePtr v) {
    status = s;
    value = std::move(v);
  });
  EXPECT_TRUE(snapshot->WaitForIncomingResponse());
  EXPECT_EQ(expected_status, status);
  EXPECT_TRUE(IsValueEqual(expected_value, value));
}

void LedgerApplicationTest::Delete(PagePtr* page,
                                   mojo::Array<uint8_t> key,
                                   Status expected_status) {
  Status status;
  (*page)->Delete(std::move(key), [&status](Status s) { status = s; });
  EXPECT_TRUE(page->WaitForIncomingResponse());
  EXPECT_EQ(expected_status, status);
}

TEST_F(LedgerApplicationTest, GetLedger) {
  EXPECT_NE(ledger_.get(), nullptr);
}

TEST_F(LedgerApplicationTest, LedgerGetRootPage) {
  Status status;
  ledger_->GetRootPage(
      [&status](Status s, mojo::InterfaceHandle<Page> p) { status = s; });
  EXPECT_TRUE(ledger_.WaitForIncomingResponse());
  EXPECT_EQ(status, Status::OK);
}

TEST_F(LedgerApplicationTest, LedgerNewPage) {
  // Get two pages and check that their ids are different.
  mojo::Array<uint8_t> id1 = GetPageId(GetTestPage());
  mojo::Array<uint8_t> id2 = GetPageId(GetTestPage());

  EXPECT_TRUE(!id1.Equals(id2));
}

TEST_F(LedgerApplicationTest, LedgerGetPage) {
  // Create a page and expect to find it by its id.
  mojo::Array<uint8_t> id = GetPageId(GetTestPage());
  GetPage(id, Status::OK);

  // Search with a random id and expect a PAGE_NOT_FOUND result.
  mojo::Array<uint8_t> testId = RandomArray(16);
  GetPage(testId, Status::PAGE_NOT_FOUND);
}

TEST_F(LedgerApplicationTest, LedgerDeletePage) {
  // Create a page, remove it and expect it doesn't exist.
  mojo::Array<uint8_t> id = GetPageId(GetTestPage());
  PagePtr page = GetPage(id, Status::OK);
  bool page_closed = false;
  page.set_connection_error_handler([&page_closed]() { page_closed = true; });
  DeletePage(id, Status::OK);
  EXPECT_FALSE(page.WaitForIncomingResponseWithTimeout(100000));
  EXPECT_TRUE(page_closed);
  GetPage(id, Status::PAGE_NOT_FOUND);

  // Remove a page with a random id and expect a PAGE_NOT_FOUND result.
  mojo::Array<uint8_t> testId = RandomArray(16);
  DeletePage(testId, Status::PAGE_NOT_FOUND);
}

TEST_F(LedgerApplicationTest, PagePutGet) {
  PagePtr page = GetTestPage();
  mojo::Array<uint8_t> key = RandomArray(20);
  mojo::Array<uint8_t> value_bytes = RandomArray(50);

  // Put a key-value pair in the page.
  Put(&page, key.Clone(), value_bytes.Clone());

  // Successfully retrieve the stored key-value pair.
  PageSnapshotPtr snapshot = GetSnapshot(&page);
  Get(&snapshot, std::move(key), Status::OK, value_bytes);

  // Add another key-value pair and do not find it in the previous snapshot.
  mojo::Array<uint8_t> key3 = RandomArray(20);
  mojo::Array<uint8_t> value3 = RandomArray(50);
  Put(&page, key3.Clone(), std::move(value3));
  Get(&snapshot, std::move(key3), Status::KEY_NOT_FOUND, nullptr);
}

TEST_F(LedgerApplicationTest, PageSnapshotGetEntries) {
  PagePtr page = GetTestPage();

  // Put three values and get them all with no prefix, or 2 of them with their
  // common prefix.
  std::vector<uint8_t> prefix = {1, 2, 3};
  mojo::Array<uint8_t> expectedKeys[] = {
      RandomArray(20, prefix), RandomArray(20, prefix), RandomArray(20),
  };
  mojo::Array<uint8_t> expectedValues[3];
  for (int i = 0; i < 3; ++i) {
    expectedValues[i] = RandomArray(50);
    Put(&page, expectedKeys[i].Clone(), expectedValues[i].Clone());
  }

  // Test get with an empty prefix.
  PageSnapshotPtr snapshot = GetSnapshot(&page);
  Status status;
  mojo::Array<EntryPtr> entries;
  snapshot->GetEntries(mojo::Array<uint8_t>(),
                       [&status, &entries](Status s, mojo::Array<EntryPtr> e) {
                         status = s;
                         entries = std::move(e);
                       });
  EXPECT_TRUE(snapshot.WaitForIncomingResponse());
  EXPECT_EQ(status, Status::OK);
  ExpectEqual(entries, expectedKeys, expectedValues, 3);
  ExpectSorted(entries);

  // Test get by prefix.
  mojo::Array<uint8_t> expectedKeys2[] = {
      std::move(expectedKeys[0]), std::move(expectedKeys[1]),
  };
  mojo::Array<uint8_t> expectedValues2[] = {
      std::move(expectedValues[0]), std::move(expectedValues[1]),
  };
  snapshot->GetEntries(mojo::Array<uint8_t>::From(prefix),
                       [&status, &entries](Status s, mojo::Array<EntryPtr> e) {
                         status = s;
                         entries = std::move(e);
                       });
  EXPECT_TRUE(snapshot.WaitForIncomingResponse());
  EXPECT_EQ(status, Status::OK);
  ExpectEqual(entries, expectedKeys2, expectedValues2, 2);
  ExpectSorted(entries);
}

TEST_F(LedgerApplicationTest, PageSnapshotGetKeys) {
  PagePtr page = GetTestPage();

  // Put three values and get them all with no prefix, or 2 of them with their
  // common prefix.
  std::vector<uint8_t> prefix = {1, 2, 3};
  mojo::Array<uint8_t> expectedKeys[] = {
      RandomArray(20, prefix), RandomArray(20, prefix), RandomArray(20),
  };
  mojo::Array<uint8_t> values[3];
  for (int i = 0; i < 3; ++i) {
    values[i] = RandomArray(50);
    Put(&page, expectedKeys[i].Clone(), values[i].Clone());
  }

  PageSnapshotPtr snapshot = GetSnapshot(&page);
  Status status;
  mojo::Array<mojo::Array<uint8_t>> result;
  snapshot->GetKeys(
      mojo::Array<uint8_t>(),
      [&status, &result](Status s, mojo::Array<mojo::Array<uint8_t>> k) {
        status = s;
        result = std::move(k);
      });
  EXPECT_TRUE(snapshot.WaitForIncomingResponse());
  EXPECT_EQ(status, Status::OK);
  ExpectEqual(result, expectedKeys, 3);
  ExpectSorted(result);

  snapshot->GetKeys(
      mojo::Array<uint8_t>::From(prefix),
      [&status, &result](Status s, mojo::Array<mojo::Array<uint8_t>> k) {
        status = s;
        result = std::move(k);
      });
  EXPECT_TRUE(snapshot.WaitForIncomingResponse());
  EXPECT_EQ(status, Status::OK);
  EXPECT_EQ(static_cast<const unsigned long>(2), result.size());
}

TEST_F(LedgerApplicationTest, PageWatch) {
  PagePtr page = GetTestPage();

  mojo::Array<uint8_t> keys[3];
  mojo::Array<uint8_t> values[3];
  for (int i = 0; i < 3; ++i) {
    keys[i] = RandomArray(20);
    values[i] = RandomArray(50);
  }

  // Put a row before adding a watcher.
  Put(&page, keys[0].Clone(), values[0].Clone());

  PageWatcherPtr pageWatcher;
  PageWatcherTest* testWatcher = new PageWatcherTest(GetProxy(&pageWatcher));
  Status status;
  page->Watch(std::move(pageWatcher), [&status](Status s) { status = s; });
  EXPECT_TRUE(page.WaitForIncomingResponse());
  EXPECT_EQ(status, Status::OK);

  // Check the inital state.
  mtl::MessageLoop::GetCurrent()->Run();
  mojo::Array<uint8_t> expectedKeys[] = {std::move(keys[0])};
  mojo::Array<uint8_t> expectedValues[] = {std::move(values[0])};
  testWatcher->CheckInitialState(expectedKeys, expectedValues, 1);

  // Add a key-value pair and expect an OnChange call.
  Put(&page, keys[1].Clone(), values[1].Clone());
  mtl::MessageLoop::GetCurrent()->Run();
  mojo::Array<uint8_t> expectedKeys2[] = {std::move(keys[1])};
  mojo::Array<uint8_t> expectedValues2[] = {std::move(values[1])};
  testWatcher->CheckLastOnChange(expectedKeys2, expectedValues2, 1);

  // Remove a key-value pair and expect an OnChange call.
  Delete(&page, keys[2].Clone(), Status::OK);
  mtl::MessageLoop::GetCurrent()->Run();
  mojo::Array<uint8_t> expectedKeys3[] = {std::move(keys[2])};
  mojo::Array<uint8_t> expectedValues3[] = {mojo::Array<uint8_t>()};
  testWatcher->CheckLastOnChange(expectedKeys3, expectedValues3, 1);
}

TEST_F(LedgerApplicationTest, Reference) {
  PagePtr page = GetTestPage();

  mojo::Array<uint8_t> value = RandomArray(50);

  // Create a reference.
  ReferencePtr reference =
      CreateReference(&page, ToString(value), value.size(), Status::OK);

  // Get it back.
  GetReference(&page, reference.Clone(), Status::OK, value);

  // Get full value with partial value.
  GetPartialReference(&page, reference.Clone(), 0, value.size(), Status::OK,
                      value);
  GetPartialReference(&page, reference.Clone(), 0, -1, Status::OK, value);
  GetPartialReference(&page, reference.Clone(), 0, value.size() + 1, Status::OK,
                      value);

  // Get partial values.
  mojo::Array<uint8_t> partial_value;
  partial_value.resize(5);

  memcpy(partial_value.data(), value.data() + 5, 5);
  GetPartialReference(&page, reference.Clone(), 5, 5, Status::OK,
                      partial_value);

  memcpy(partial_value.data(), value.data() + (value.size() - 5), 5);
  GetPartialReference(&page, reference.Clone(), -5, 5, Status::OK,
                      partial_value);

  // Get partial values with out of bounds parameters.
  partial_value.resize(0);
  GetPartialReference(&page, reference.Clone(), value.size() + 1, 5, Status::OK,
                      partial_value);
  GetPartialReference(&page, reference.Clone(), -(value.size() + 1), 5,
                      Status::OK, partial_value);

  // Associate the reference with a key.
  mojo::Array<uint8_t> key = RandomArray(20);
  PutReference(&page, key.Clone(), reference.Clone(), Priority::EAGER,
               Status::OK);

  // Retrieve the value.
  PageSnapshotPtr snapshot = GetSnapshot(&page);
  Get(&snapshot, key.Clone(), Status::OK, value);
}

TEST_F(LedgerApplicationTest, EmptyReference) {
  PagePtr page = GetTestPage();

  mojo::Array<uint8_t> value = mojo::Array<uint8_t>::New(0);

  // Create a reference.
  ReferencePtr reference =
      CreateReference(&page, ToString(value), value.size(), Status::OK);

  // Get it back.
  GetReference(&page, reference.Clone(), Status::OK, value);
  GetPartialReference(&page, reference.Clone(), 0, 0, Status::OK, value);
  GetPartialReference(&page, reference.Clone(), 0, -1, Status::OK, value);
  GetPartialReference(&page, reference.Clone(), 0, 1, Status::OK, value);
  GetPartialReference(&page, reference.Clone(), 5, 5, Status::OK, value);
  GetPartialReference(&page, reference.Clone(), -5, 5, Status::OK, value);

  // Associate the reference with a key.
  mojo::Array<uint8_t> key = RandomArray(20);
  PutReference(&page, key.Clone(), reference.Clone(), Priority::EAGER,
               Status::OK);

  // Retrieve the value.
  PageSnapshotPtr snapshot = GetSnapshot(&page);
  Get(&snapshot, key.Clone(), Status::OK, value);
}

TEST_F(LedgerApplicationTest, ReferenceFailures) {
  PagePtr page = GetTestPage();

  mojo::Array<uint8_t> key = RandomArray(20);
  mojo::Array<uint8_t> value = RandomArray(50);

  // Fail creation due to wrong size.
  CreateReference(&page, ToString(value), value.size() - 1, Status::IO_ERROR);
  CreateReference(&page, ToString(value), value.size() + 1, Status::IO_ERROR);

  // Fail retrieval due to unknown reference.
  ReferencePtr reference = Reference::New();
  reference->opaque_id = key.Clone();
  GetReference(&page, reference.Clone(), Status::REFERENCE_NOT_FOUND, nullptr);
  GetPartialReference(&page, reference.Clone(), 0, -1,
                      Status::REFERENCE_NOT_FOUND, nullptr);

  // Fail association due to unknown reference.
  PutReference(&page, key.Clone(), reference.Clone(), Priority::EAGER,
               Status::REFERENCE_NOT_FOUND);
}

}  // namespace
}  // namespace ledger

MojoResult MojoMain(MojoHandle handle) {
  return mojo::test::RunAllTests(handle);
}

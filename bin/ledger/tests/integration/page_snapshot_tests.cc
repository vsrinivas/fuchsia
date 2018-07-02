// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include "garnet/public/lib/callback/capture.h"
#include "garnet/public/lib/callback/waiter.h"
#include "gtest/gtest.h"
#include "lib/callback/capture.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/vmo/sized_vmo.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/app/fidl/serialization_size.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/tests/integration/integration_test.h"
#include "peridot/bin/ledger/tests/integration/test_utils.h"
#include "peridot/lib/convert/convert.h"

namespace test {
namespace integration {
namespace {

class PageSnapshotIntegrationTest : public IntegrationTest {
 public:
  PageSnapshotIntegrationTest() {}
  ~PageSnapshotIntegrationTest() override {}

  // Returns a snapshot of |page|, checking success.
  ledger::PageSnapshotPtr PageGetSnapshot(
      ledger::PagePtr* page,
      fidl::VectorPtr<uint8_t> prefix = fidl::VectorPtr<uint8_t>::New(0)) {
    ledger::Status status;
    ledger::PageSnapshotPtr snapshot;
    auto waiter = NewWaiter();
    (*page)->GetSnapshot(snapshot.NewRequest(), std::move(prefix), nullptr,
                         callback::Capture(waiter->GetCallback(), &status));
    waiter->RunUntilCalled();
    EXPECT_EQ(ledger::Status::OK, status);
    return snapshot;
  }

  // Returns all keys from |snapshot|, starting at |start|. If |num_queries| is
  // not null, stores the number of calls to GetKeys.
  std::vector<fidl::VectorPtr<uint8_t>> SnapshotGetKeys(
      ledger::PageSnapshotPtr* snapshot,
      fidl::VectorPtr<uint8_t> start = fidl::VectorPtr<uint8_t>::New(0),
      int* num_queries = nullptr) {
    std::vector<fidl::VectorPtr<uint8_t>> result;
    std::unique_ptr<ledger::Token> token;
    if (num_queries) {
      *num_queries = 0;
    }
    do {
      ledger::Status status;
      fidl::VectorPtr<fidl::VectorPtr<uint8_t>> keys;
      auto waiter = NewWaiter();
      (*snapshot)->GetKeys(
          start.Clone(), std::move(token),
          callback::Capture(waiter->GetCallback(), &status, &keys, &token));
      waiter->RunUntilCalled();
      EXPECT_TRUE(status == ledger::Status::OK ||
                  status == ledger::Status::PARTIAL_RESULT);
      if (num_queries) {
        (*num_queries)++;
      }
      for (auto& key : keys.take()) {
        result.push_back(std::move(key));
      }
    } while (token);
    return result;
  }

  std::string SnapshotFetchPartial(ledger::PageSnapshotPtr* snapshot,
                                   fidl::VectorPtr<uint8_t> key, int64_t offset,
                                   int64_t max_size) {
    ledger::Status status;
    fuchsia::mem::BufferPtr buffer;
    auto waiter = NewWaiter();
    (*snapshot)->FetchPartial(
        std::move(key), offset, max_size,
        callback::Capture(waiter->GetCallback(), &status, &buffer));
    waiter->RunUntilCalled();
    EXPECT_EQ(ledger::Status::OK, status);
    std::string result;
    EXPECT_TRUE(fsl::StringFromVmo(*buffer, &result));
    return result;
  }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageSnapshotIntegrationTest);
};

TEST_P(PageSnapshotIntegrationTest, PageSnapshotGet) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();
  ledger::Status status;
  auto waiter = NewWaiter();
  page->Put(convert::ToArray("name"), convert::ToArray("Alice"),
            callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  fuchsia::mem::BufferPtr value;
  waiter = NewWaiter();
  snapshot->Get(convert::ToArray("name"),
                callback::Capture(waiter->GetCallback(), &status, &value));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);
  EXPECT_EQ("Alice", ToString(value));

  // Attempt to get an entry that is not in the page.
  waiter = NewWaiter();
  snapshot->Get(convert::ToArray("favorite book"),
                callback::Capture(waiter->GetCallback(), &status, &value));
  waiter->RunUntilCalled();
  // People don't read much these days.
  EXPECT_EQ(ledger::Status::KEY_NOT_FOUND, status);
}

TEST_P(PageSnapshotIntegrationTest, PageSnapshotGetPipeline) {
  auto instance = NewLedgerAppInstance();
  std::string expected_value = "Alice";
  expected_value.resize(100);

  auto status_waiter =
      fxl::MakeRefCounted<callback::StatusWaiter<ledger::Status>>(
          ledger::Status::OK);

  ledger::PagePtr page = instance->GetTestPage();
  page->Put(convert::ToArray("name"), convert::ToArray(expected_value),
            status_waiter->NewCallback());

  ledger::PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                    nullptr, status_waiter->NewCallback());

  ledger::Status status;
  fuchsia::mem::BufferPtr value;
  snapshot->Get(
      convert::ToArray("name"),
      [&value, status_callback = status_waiter->NewCallback()](
          ledger::Status status, fuchsia::mem::BufferPtr received_value) {
        value = std::move(received_value);
        status_callback(status);
      });
  auto waiter = NewWaiter();
  status_waiter->Finalize(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);
  ASSERT_TRUE(value);
  EXPECT_EQ(expected_value, ToString(value));
}

TEST_P(PageSnapshotIntegrationTest, PageSnapshotPutOrder) {
  auto instance = NewLedgerAppInstance();
  std::string value1 = "Alice";
  value1.resize(100);
  std::string value2;

  // Put the 2 values without waiting for the callbacks.
  ledger::PagePtr page = instance->GetTestPage();
  auto status_waiter =
      fxl::MakeRefCounted<callback::StatusWaiter<ledger::Status>>(
          ledger::Status::OK);
  page->Put(convert::ToArray("name"), convert::ToArray(value1),
            status_waiter->NewCallback());
  page->Put(convert::ToArray("name"), convert::ToArray(value2),
            status_waiter->NewCallback());
  ledger::Status status;
  auto waiter = NewWaiter();
  status_waiter->Finalize(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  fuchsia::mem::BufferPtr value;
  waiter = NewWaiter();
  snapshot->Get(convert::ToArray("name"),
                callback::Capture(waiter->GetCallback(), &status, &value));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);
  EXPECT_EQ(value2, ToString(value));
}

TEST_P(PageSnapshotIntegrationTest, PageSnapshotFetchPartial) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();
  ledger::Status status;
  auto waiter = NewWaiter();
  page->Put(convert::ToArray("name"), convert::ToArray("Alice"),
            callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  EXPECT_EQ("Alice",
            SnapshotFetchPartial(&snapshot, convert::ToArray("name"), 0, -1));
  EXPECT_EQ("e",
            SnapshotFetchPartial(&snapshot, convert::ToArray("name"), 4, -1));
  EXPECT_EQ("",
            SnapshotFetchPartial(&snapshot, convert::ToArray("name"), 5, -1));
  EXPECT_EQ("",
            SnapshotFetchPartial(&snapshot, convert::ToArray("name"), 6, -1));
  EXPECT_EQ("i",
            SnapshotFetchPartial(&snapshot, convert::ToArray("name"), 2, 1));
  EXPECT_EQ("",
            SnapshotFetchPartial(&snapshot, convert::ToArray("name"), 2, 0));

  // Negative offsets.
  EXPECT_EQ("Alice",
            SnapshotFetchPartial(&snapshot, convert::ToArray("name"), -5, -1));
  EXPECT_EQ("e",
            SnapshotFetchPartial(&snapshot, convert::ToArray("name"), -1, -1));
  EXPECT_EQ("",
            SnapshotFetchPartial(&snapshot, convert::ToArray("name"), -5, 0));
  EXPECT_EQ("i",
            SnapshotFetchPartial(&snapshot, convert::ToArray("name"), -3, 1));

  // Attempt to get an entry that is not in the page.
  fuchsia::mem::BufferPtr value;
  waiter = NewWaiter();
  snapshot->FetchPartial(
      convert::ToArray("favorite book"), 0, -1,
      callback::Capture(waiter->GetCallback(), &status, &value));
  waiter->RunUntilCalled();
  // People don't read much these days.
  EXPECT_EQ(ledger::Status::KEY_NOT_FOUND, status);
}

TEST_P(PageSnapshotIntegrationTest, PageSnapshotGetKeys) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();

  // Grab a snapshot before adding any entries and verify that GetKeys()
  // returns empty results.
  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  std::vector<fidl::VectorPtr<uint8_t>> result = SnapshotGetKeys(&snapshot);
  EXPECT_EQ(0u, result.size());

  // Add entries and grab a new snapshot.
  const size_t N = 4;
  fidl::VectorPtr<uint8_t> keys[N] = {
      RandomArray(20, {0, 0, 0}),
      RandomArray(20, {0, 0, 1}),
      RandomArray(20, {0, 1, 0}),
      RandomArray(20, {0, 1, 1}),
  };
  ledger::Status status;
  for (auto& key : keys) {
    auto waiter = NewWaiter();
    page->Put(key.Clone(), RandomArray(50),
              callback::Capture(waiter->GetCallback(), &status));
    waiter->RunUntilCalled();
    EXPECT_EQ(ledger::Status::OK, status);
  }
  snapshot = PageGetSnapshot(&page);

  // Get all keys.
  result = SnapshotGetKeys(&snapshot);
  EXPECT_EQ(N, result.size());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_EQ(keys[i], result.at(i));
  }

  // Get keys matching the prefix "0".
  snapshot =
      PageGetSnapshot(&page, fidl::VectorPtr<uint8_t>(std::vector<uint8_t>{0}));
  result = SnapshotGetKeys(&snapshot);
  EXPECT_EQ(N, result.size());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_EQ(keys[i], result.at(i));
  }

  // Get keys matching the prefix "00".
  snapshot = PageGetSnapshot(
      &page, fidl::VectorPtr<uint8_t>(std::vector<uint8_t>{0, 0}));
  result = SnapshotGetKeys(&snapshot);
  ASSERT_EQ(2u, result.size());
  for (size_t i = 0; i < 2u; ++i) {
    EXPECT_EQ(keys[i], result.at(i));
  }

  // Get keys matching the prefix "010".
  snapshot = PageGetSnapshot(
      &page, fidl::VectorPtr<uint8_t>(std::vector<uint8_t>{0, 1, 0}));
  result = SnapshotGetKeys(&snapshot);
  ASSERT_EQ(1u, result.size());
  EXPECT_EQ(keys[2], result.at(0));

  // Get keys matching the prefix "5".
  snapshot =
      PageGetSnapshot(&page, fidl::VectorPtr<uint8_t>(std::vector<uint8_t>{5}));
  result = SnapshotGetKeys(&snapshot);
  EXPECT_EQ(0u, result.size());

  // Get keys matching the prefix "0" and starting with the key "010".
  snapshot =
      PageGetSnapshot(&page, fidl::VectorPtr<uint8_t>(std::vector<uint8_t>{0}));
  result = SnapshotGetKeys(
      &snapshot, fidl::VectorPtr<uint8_t>(std::vector<uint8_t>{0, 1, 0}));
  EXPECT_EQ(2u, result.size());
}

TEST_P(PageSnapshotIntegrationTest, PageSnapshotGetKeysMultiPart) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();

  // Grab a snapshot before adding any entries and verify that GetKeys()
  // returns empty results.
  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  int num_queries;
  std::vector<fidl::VectorPtr<uint8_t>> result = SnapshotGetKeys(
      &snapshot, fidl::VectorPtr<uint8_t>::New(0), &num_queries);
  EXPECT_EQ(0u, result.size());
  EXPECT_EQ(1, num_queries);

  // Add entries and grab a new snapshot.
  // Add enough keys so they don't all fit in memory and we will have to have
  // multiple queries.
  const size_t key_size = ledger::kMaxKeySize;
  const size_t N =
      ledger::fidl_serialization::kMaxInlineDataSize / key_size + 1;
  fidl::VectorPtr<uint8_t> keys[N];
  for (size_t i = 0; i < N; ++i) {
    // Generate keys so that they are in increasing order to match the order
    // of results from GetKeys().
    keys[i] = RandomArray(key_size, {static_cast<uint8_t>(i >> 8),
                                     static_cast<uint8_t>(i & 0xFF)});
  }

  ledger::Status status;
  for (auto& key : keys) {
    auto waiter = NewWaiter();
    page->Put(key.Clone(), RandomArray(10),
              callback::Capture(waiter->GetCallback(), &status));
    waiter->RunUntilCalled();
    EXPECT_EQ(ledger::Status::OK, status);
  }
  snapshot = PageGetSnapshot(&page);

  // Get all keys.
  result = SnapshotGetKeys(&snapshot, fidl::VectorPtr<uint8_t>::New(0),
                           &num_queries);
  EXPECT_TRUE(num_queries > 1);
  ASSERT_EQ(N, result.size());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_EQ(keys[i], result.at(i));
  }
}

TEST_P(PageSnapshotIntegrationTest, PageSnapshotGetEntries) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();

  // Grab a snapshot before adding any entries and verify that GetEntries()
  // returns empty results.
  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  auto entries = SnapshotGetEntries(this, &snapshot);
  EXPECT_EQ(0u, entries.size());

  // Add entries and grab a new snapshot.
  const size_t N = 4;
  fidl::VectorPtr<uint8_t> keys[N] = {
      RandomArray(20, {0, 0, 0}),
      RandomArray(20, {0, 0, 1}),
      RandomArray(20, {0, 1, 0}),
      RandomArray(20, {0, 1, 1}),
  };
  fidl::VectorPtr<uint8_t> values[N] = {
      RandomArray(50),
      RandomArray(50),
      RandomArray(50),
      RandomArray(50),
  };
  ledger::Status status;
  for (size_t i = 0; i < N; ++i) {
    auto waiter = NewWaiter();
    page->Put(keys[i].Clone(), values[i].Clone(),
              callback::Capture(waiter->GetCallback(), &status));
    waiter->RunUntilCalled();
    EXPECT_EQ(ledger::Status::OK, status);
  }
  snapshot = PageGetSnapshot(&page);

  // Get all entries.
  entries = SnapshotGetEntries(this, &snapshot);
  EXPECT_EQ(N, entries.size());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_EQ(keys[i], entries.at(i).key);
    EXPECT_EQ(values[i], ToArray(entries.at(i).value));
  }

  // Get entries matching the prefix "0".
  snapshot =
      PageGetSnapshot(&page, fidl::VectorPtr<uint8_t>(std::vector<uint8_t>{0}));
  entries = SnapshotGetEntries(this, &snapshot);
  EXPECT_EQ(N, entries.size());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_EQ(keys[i], entries.at(i).key);
    EXPECT_EQ(values[i], ToArray(entries.at(i).value));
  }

  // Get entries matching the prefix "00".
  snapshot = PageGetSnapshot(
      &page, fidl::VectorPtr<uint8_t>(std::vector<uint8_t>{0, 0}));
  entries = SnapshotGetEntries(this, &snapshot);
  ASSERT_EQ(2u, entries.size());
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_EQ(keys[i], entries.at(i).key);
    EXPECT_EQ(values[i], ToArray(entries.at(i).value));
  }

  // Get keys matching the prefix "010".
  snapshot = PageGetSnapshot(
      &page, fidl::VectorPtr<uint8_t>(std::vector<uint8_t>{0, 1, 0}));
  entries = SnapshotGetEntries(this, &snapshot);
  ASSERT_EQ(1u, entries.size());
  EXPECT_EQ(keys[2], entries.at(0).key);
  EXPECT_EQ(values[2], ToArray(entries.at(0).value));

  // Get keys matching the prefix "5".
  snapshot =
      PageGetSnapshot(&page, fidl::VectorPtr<uint8_t>(std::vector<uint8_t>{5}));

  entries = SnapshotGetEntries(this, &snapshot);
  EXPECT_EQ(0u, entries.size());
}

TEST_P(PageSnapshotIntegrationTest, PageSnapshotGetEntriesMultiPartSize) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();

  // Grab a snapshot before adding any entries and verify that GetEntries()
  // returns empty results.
  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  int num_queries;
  auto entries = SnapshotGetEntries(
      this, &snapshot, fidl::VectorPtr<uint8_t>::New(0), &num_queries);
  EXPECT_EQ(0u, entries.size());
  EXPECT_EQ(1, num_queries);

  // Add entries and grab a new snapshot.
  // Add enough keys so they don't all fit in memory and we will have to have
  // multiple queries.
  const size_t value_size = 100;
  const size_t key_size = ledger::kMaxKeySize;
  const size_t N =
      ledger::fidl_serialization::kMaxInlineDataSize / (key_size + value_size) +
      1;
  fidl::VectorPtr<uint8_t> keys[N];
  fidl::VectorPtr<uint8_t> values[N];
  for (size_t i = 0; i < N; ++i) {
    // Generate keys so that they are in increasing order to match the order
    // of results from GetEntries().
    keys[i] = RandomArray(key_size, {static_cast<uint8_t>(i >> 8),
                                     static_cast<uint8_t>(i & 0xFF)});
    values[i] = RandomArray(value_size);
  }

  ledger::Status status;
  for (size_t i = 0; i < N; ++i) {
    auto waiter = NewWaiter();
    page->Put(keys[i].Clone(), values[i].Clone(),
              callback::Capture(waiter->GetCallback(), &status));

    waiter->RunUntilCalled();

    EXPECT_EQ(ledger::Status::OK, status);
  }
  snapshot = PageGetSnapshot(&page);

  // Get all entries.
  entries = SnapshotGetEntries(this, &snapshot,
                               fidl::VectorPtr<uint8_t>::New(0), &num_queries);
  EXPECT_TRUE(num_queries > 1);
  ASSERT_EQ(N, entries.size());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_EQ(keys[i], entries[i].key);
    EXPECT_EQ(values[i], ToArray(entries[i].value));
  }
}

TEST_P(PageSnapshotIntegrationTest, PageSnapshotGetEntriesMultiPartHandles) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();

  // Grab a snapshot before adding any entries and verify that GetEntries()
  // returns empty results.
  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  int num_queries;
  auto entries = SnapshotGetEntries(
      this, &snapshot, fidl::VectorPtr<uint8_t>::New(0), &num_queries);
  EXPECT_EQ(0u, entries.size());
  EXPECT_EQ(1, num_queries);

  // Add entries and grab a new snapshot.
  const size_t N = 100;
  fidl::VectorPtr<uint8_t> keys[N];
  fidl::VectorPtr<uint8_t> values[N];
  for (size_t i = 0; i < N; ++i) {
    // Generate keys so that they are in increasing order to match the order
    // of results from GetEntries().
    keys[i] = RandomArray(
        20, {static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i & 0xFF)});
    values[i] = RandomArray(100);
  }

  for (size_t i = 0; i < N; ++i) {
    ledger::Status status;
    auto waiter = NewWaiter();
    page->Put(keys[i].Clone(), values[i].Clone(),
              callback::Capture(waiter->GetCallback(), &status));
    waiter->RunUntilCalled();
    EXPECT_EQ(ledger::Status::OK, status);
  }
  snapshot = PageGetSnapshot(&page);

  // Get all entries.
  entries = SnapshotGetEntries(this, &snapshot,
                               fidl::VectorPtr<uint8_t>::New(0), &num_queries);
  EXPECT_TRUE(num_queries > 1);
  ASSERT_EQ(N, entries.size());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_EQ(keys[i], entries[i].key);
    EXPECT_EQ(values[i], ToArray(entries[i].value));
  }
}

TEST_P(PageSnapshotIntegrationTest, PageSnapshotGettersReturnSortedEntries) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();

  const size_t N = 4;
  fidl::VectorPtr<uint8_t> keys[N] = {
      RandomArray(20, {2}),
      RandomArray(20, {5}),
      RandomArray(20, {3}),
      RandomArray(20, {0}),
  };
  fidl::VectorPtr<uint8_t> values[N] = {
      RandomArray(20),
      RandomArray(20),
      RandomArray(20),
      RandomArray(20),
  };
  for (size_t i = 0; i < N; ++i) {
    ledger::Status status;
    auto waiter = NewWaiter();
    page->Put(keys[i].Clone(), values[i].Clone(),
              callback::Capture(waiter->GetCallback(), &status));
    waiter->RunUntilCalled();
    EXPECT_EQ(ledger::Status::OK, status);
  }

  // Get a snapshot.
  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);

  // Verify that GetKeys() results are sorted.
  std::vector<fidl::VectorPtr<uint8_t>> result = SnapshotGetKeys(&snapshot);
  EXPECT_EQ(keys[3], result.at(0));
  EXPECT_EQ(keys[0], result.at(1));
  EXPECT_EQ(keys[2], result.at(2));
  EXPECT_EQ(keys[1], result.at(3));

  // Verify that GetEntries() results are sorted.
  auto entries = SnapshotGetEntries(this, &snapshot);
  EXPECT_EQ(keys[3], entries[0].key);
  EXPECT_EQ(values[3], ToArray(entries[0].value));
  EXPECT_EQ(keys[0], entries[1].key);
  EXPECT_EQ(values[0], ToArray(entries[1].value));
  EXPECT_EQ(keys[2], entries[2].key);
  EXPECT_EQ(values[2], ToArray(entries[2].value));
  EXPECT_EQ(keys[1], entries[3].key);
  EXPECT_EQ(values[1], ToArray(entries[3].value));
}

TEST_P(PageSnapshotIntegrationTest, PageCreateReferenceFromSocketWrongSize) {
  auto instance = NewLedgerAppInstance();
  const std::string big_data(1'000'000, 'a');

  ledger::PagePtr page = instance->GetTestPage();

  ledger::Status status;
  ledger::ReferencePtr reference;
  auto waiter = NewWaiter();
  page->CreateReferenceFromSocket(
      123, StreamDataToSocket(big_data),
      callback::Capture(waiter->GetCallback(), &status, &reference));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::IO_ERROR, status);
}

TEST_P(PageSnapshotIntegrationTest, PageCreatePutLargeReferenceFromSocket) {
  auto instance = NewLedgerAppInstance();
  const std::string big_data(1'000'000, 'a');

  ledger::PagePtr page = instance->GetTestPage();

  // Stream the data into the reference.
  ledger::Status status;
  ledger::ReferencePtr reference;
  auto waiter = NewWaiter();
  page->CreateReferenceFromSocket(
      big_data.size(), StreamDataToSocket(big_data),
      callback::Capture(waiter->GetCallback(), &status, &reference));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  // Set the reference under a key.
  waiter = NewWaiter();
  page->PutReference(convert::ToArray("big data"), std::move(*reference),
                     ledger::Priority::EAGER,
                     callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  // Get a snapshot and read the value.
  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  fuchsia::mem::BufferPtr value;
  waiter = NewWaiter();
  snapshot->Get(convert::ToArray("big data"),
                callback::Capture(waiter->GetCallback(), &status, &value));
  waiter->RunUntilCalled();

  EXPECT_EQ(ledger::Status::OK, status);
  EXPECT_EQ(big_data, ToString(value));
}

TEST_P(PageSnapshotIntegrationTest, PageCreatePutLargeReferenceFromVmo) {
  auto instance = NewLedgerAppInstance();
  const std::string big_data(1'000'000, 'a');
  fsl::SizedVmo vmo;
  ASSERT_TRUE(fsl::VmoFromString(big_data, &vmo));

  ledger::PagePtr page = instance->GetTestPage();

  // Stream the data into the reference.
  ledger::Status status;
  ledger::ReferencePtr reference;
  auto waiter = NewWaiter();
  page->CreateReferenceFromVmo(
      std::move(vmo).ToTransport(),
      callback::Capture(waiter->GetCallback(), &status, &reference));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  // Set the reference under a key.
  waiter = NewWaiter();
  page->PutReference(convert::ToArray("big data"), std::move(*reference),
                     ledger::Priority::EAGER,
                     callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  // Get a snapshot and read the value.
  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  fuchsia::mem::BufferPtr value;
  waiter = NewWaiter();
  snapshot->Get(convert::ToArray("big data"),
                callback::Capture(waiter->GetCallback(), &status, &value));
  waiter->RunUntilCalled();

  EXPECT_EQ(ledger::Status::OK, status);
  EXPECT_EQ(big_data, ToString(value));
}

TEST_P(PageSnapshotIntegrationTest, PageSnapshotClosePageGet) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();
  ledger::Status status;
  auto waiter = NewWaiter();
  page->Put(convert::ToArray("name"), convert::ToArray("Alice"),
            callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);

  // Close the channel. ledger::PageSnapshotPtr should remain valid.
  page.Unbind();

  fuchsia::mem::BufferPtr value;
  waiter = NewWaiter();
  snapshot->Get(convert::ToArray("name"),
                callback::Capture(waiter->GetCallback(), &status, &value));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);
  EXPECT_EQ("Alice", ToString(value));

  // Attempt to get an entry that is not in the page.
  waiter = NewWaiter();
  snapshot->Get(convert::ToArray("favorite book"),
                callback::Capture(waiter->GetCallback(), &status, &value));
  waiter->RunUntilCalled();
  // People don't read much these days.
  EXPECT_EQ(ledger::Status::KEY_NOT_FOUND, status);
}

TEST_P(PageSnapshotIntegrationTest, PageGetById) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();
  ledger::PageId test_page_id;
  auto waiter = NewWaiter();
  page->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  waiter->RunUntilCalled();

  ledger::Status status;
  waiter = NewWaiter();
  page->Put(convert::ToArray("name"), convert::ToArray("Alice"),
            callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  page.Unbind();

  page =
      instance->GetPage(fidl::MakeOptional(test_page_id), ledger::Status::OK);
  ledger::PageId page_id;
  waiter = NewWaiter();
  page->GetId(callback::Capture(waiter->GetCallback(), &page_id));
  waiter->RunUntilCalled();
  EXPECT_EQ(test_page_id.id, page_id.id);

  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  fuchsia::mem::BufferPtr value;
  waiter = NewWaiter();
  snapshot->Get(convert::ToArray("name"),
                callback::Capture(waiter->GetCallback(), &status, &value));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);
  EXPECT_EQ("Alice", ToString(value));
}

INSTANTIATE_TEST_CASE_P(PageSnapshotIntegrationTest,
                        PageSnapshotIntegrationTest,
                        ::testing::ValuesIn(GetLedgerAppInstanceFactories()));

}  // namespace
}  // namespace integration
}  // namespace test

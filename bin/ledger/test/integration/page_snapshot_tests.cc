// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/macros.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/app/fidl/serialization_size.h"
#include "peridot/bin/ledger/convert/convert.h"
#include "peridot/bin/ledger/test/integration/integration_test.h"
#include "peridot/bin/ledger/test/integration/test_utils.h"
#include "zx/vmo.h"

namespace test {
namespace integration {
namespace {

class PageSnapshotIntegrationTest : public IntegrationTest {
 public:
  PageSnapshotIntegrationTest() {}
  ~PageSnapshotIntegrationTest() override {}

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageSnapshotIntegrationTest);
};

TEST_F(PageSnapshotIntegrationTest, PageSnapshotGet) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();
  page->Put(
      convert::ToArray("name"), convert::ToArray("Alice"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  zx::vmo value;
  snapshot->Get(convert::ToArray("name"),
                [&value](ledger::Status status, zx::vmo v) {
                  EXPECT_EQ(status, ledger::Status::OK);
                  value = std::move(v);
                });
  EXPECT_TRUE(snapshot.WaitForIncomingResponse());
  EXPECT_EQ("Alice", ToString(value));

  // Attempt to get an entry that is not in the page.
  snapshot->Get(convert::ToArray("favorite book"),
                [](ledger::Status status, zx::vmo v) {
                  // People don't read much these days.
                  EXPECT_EQ(status, ledger::Status::KEY_NOT_FOUND);
                });
  EXPECT_TRUE(snapshot.WaitForIncomingResponse());
}

TEST_F(PageSnapshotIntegrationTest, PageSnapshotGetPipeline) {
  auto instance = NewLedgerAppInstance();
  std::string expected_value = "Alice";
  expected_value.resize(100);

  ledger::PagePtr page = instance->GetTestPage();
  page->Put(
      convert::ToArray("name"), convert::ToArray(expected_value),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });

  ledger::PageSnapshotPtr snapshot;
  page->GetSnapshot(
      snapshot.NewRequest(), nullptr, nullptr,
      [](ledger::Status status) { EXPECT_EQ(ledger::Status::OK, status); });

  zx::vmo value;
  snapshot->Get(convert::ToArray("name"),
                [&value](ledger::Status status, zx::vmo v) {
                  EXPECT_EQ(status, ledger::Status::OK);
                  value = std::move(v);
                });

  EXPECT_TRUE(page.WaitForIncomingResponse());
  EXPECT_TRUE(page.WaitForIncomingResponse());
  EXPECT_TRUE(snapshot.WaitForIncomingResponse());

  ASSERT_TRUE(value);
  EXPECT_EQ(expected_value, ToString(value));
}

TEST_F(PageSnapshotIntegrationTest, PageSnapshotPutOrder) {
  auto instance = NewLedgerAppInstance();
  std::string value1 = "Alice";
  value1.resize(100);
  std::string value2;

  // Put the 2 values without waiting for the callbacks.
  ledger::PagePtr page = instance->GetTestPage();
  page->Put(
      convert::ToArray("name"), convert::ToArray(value1),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  page->Put(
      convert::ToArray("name"), convert::ToArray(value2),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });

  EXPECT_TRUE(page.WaitForIncomingResponse());
  EXPECT_TRUE(page.WaitForIncomingResponse());

  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  zx::vmo value;
  snapshot->Get(convert::ToArray("name"),
                [&value](ledger::Status status, zx::vmo v) {
                  EXPECT_EQ(status, ledger::Status::OK);
                  value = std::move(v);
                });
  EXPECT_TRUE(snapshot.WaitForIncomingResponse());
  EXPECT_EQ(value2, ToString(value));
}

TEST_F(PageSnapshotIntegrationTest, PageSnapshotFetchPartial) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();
  page->Put(
      convert::ToArray("name"), convert::ToArray("Alice"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

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
  snapshot->FetchPartial(convert::ToArray("favorite book"), 0, -1,
                         [](ledger::Status status, zx::vmo received_buffer) {
                           // People don't read much these days.
                           EXPECT_EQ(ledger::Status::KEY_NOT_FOUND, status);
                         });
  EXPECT_TRUE(snapshot.WaitForIncomingResponse());
}

TEST_F(PageSnapshotIntegrationTest, PageSnapshotGetKeys) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();

  // Grab a snapshot before adding any entries and verify that GetKeys()
  // returns empty results.
  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  fidl::Array<fidl::Array<uint8_t>> result =
      SnapshotGetKeys(&snapshot, fidl::Array<uint8_t>());
  EXPECT_EQ(0u, result.size());

  // Add entries and grab a new snapshot.
  const size_t N = 4;
  fidl::Array<uint8_t> keys[N] = {
      RandomArray(20, {0, 0, 0}),
      RandomArray(20, {0, 0, 1}),
      RandomArray(20, {0, 1, 0}),
      RandomArray(20, {0, 1, 1}),
  };
  for (auto& key : keys) {
    page->Put(key.Clone(), RandomArray(50), [](ledger::Status status) {
      EXPECT_EQ(status, ledger::Status::OK);
    });
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
  snapshot = PageGetSnapshot(
      &page, fidl::Array<uint8_t>::From(std::vector<uint8_t>{0}));
  result = SnapshotGetKeys(&snapshot, fidl::Array<uint8_t>());
  EXPECT_EQ(N, result.size());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_TRUE(keys[i].Equals(result[i]));
  }

  // Get keys matching the prefix "00".
  snapshot = PageGetSnapshot(
      &page, fidl::Array<uint8_t>::From(std::vector<uint8_t>{0, 0}));
  result = SnapshotGetKeys(&snapshot, fidl::Array<uint8_t>());
  ASSERT_EQ(2u, result.size());
  for (size_t i = 0; i < 2u; ++i) {
    EXPECT_TRUE(keys[i].Equals(result[i]));
  }

  // Get keys matching the prefix "010".
  snapshot = PageGetSnapshot(
      &page, fidl::Array<uint8_t>::From(std::vector<uint8_t>{0, 1, 0}));
  result = SnapshotGetKeys(&snapshot, fidl::Array<uint8_t>());
  ASSERT_EQ(1u, result.size());
  EXPECT_TRUE(keys[2].Equals(result[0]));

  // Get keys matching the prefix "5".
  snapshot = PageGetSnapshot(
      &page, fidl::Array<uint8_t>::From(std::vector<uint8_t>{5}));
  result = SnapshotGetKeys(&snapshot, fidl::Array<uint8_t>());
  EXPECT_EQ(0u, result.size());

  // Get keys matching the prefix "0" and starting with the key "010".
  snapshot = PageGetSnapshot(
      &page, fidl::Array<uint8_t>::From(std::vector<uint8_t>{0}));
  result = SnapshotGetKeys(
      &snapshot, fidl::Array<uint8_t>::From(std::vector<uint8_t>{0, 1, 0}));
  EXPECT_EQ(2u, result.size());
}

TEST_F(PageSnapshotIntegrationTest, PageSnapshotGetKeysMultiPart) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();

  // Grab a snapshot before adding any entries and verify that GetKeys()
  // returns empty results.
  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  int num_queries;
  fidl::Array<fidl::Array<uint8_t>> result =
      SnapshotGetKeys(&snapshot, fidl::Array<uint8_t>(), &num_queries);
  EXPECT_EQ(0u, result.size());
  EXPECT_EQ(1, num_queries);

  // Add entries and grab a new snapshot.
  // Add enough keys so they don't all fit in memory and we will have to have
  // multiple queries.
  const size_t key_size = ledger::kMaxKeySize;
  const size_t N =
      ledger::fidl_serialization::kMaxInlineDataSize / key_size + 1;
  fidl::Array<uint8_t> keys[N];
  for (size_t i = 0; i < N; ++i) {
    // Generate keys so that they are in increasing order to match the order
    // of results from GetKeys().
    keys[i] = RandomArray(
        key_size,
        {static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i & 0xFF)});
  }

  for (auto& key : keys) {
    page->Put(key.Clone(), RandomArray(10), [](ledger::Status status) {
      EXPECT_EQ(status, ledger::Status::OK);
    });
    ASSERT_TRUE(page.WaitForIncomingResponse());
  }
  snapshot = PageGetSnapshot(&page);

  // Get all keys.
  result = SnapshotGetKeys(&snapshot, fidl::Array<uint8_t>(), &num_queries);
  EXPECT_TRUE(num_queries > 1);
  ASSERT_EQ(N, result.size());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_TRUE(keys[i].Equals(result[i]));
  }
}

TEST_F(PageSnapshotIntegrationTest, PageSnapshotGetEntries) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();

  // Grab a snapshot before adding any entries and verify that GetEntries()
  // returns empty results.
  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  fidl::Array<ledger::EntryPtr> entries =
      SnapshotGetEntries(&snapshot, fidl::Array<uint8_t>());
  EXPECT_EQ(0u, entries.size());

  // Add entries and grab a new snapshot.
  const size_t N = 4;
  fidl::Array<uint8_t> keys[N] = {
      RandomArray(20, {0, 0, 0}),
      RandomArray(20, {0, 0, 1}),
      RandomArray(20, {0, 1, 0}),
      RandomArray(20, {0, 1, 1}),
  };
  fidl::Array<uint8_t> values[N] = {
      RandomArray(50),
      RandomArray(50),
      RandomArray(50),
      RandomArray(50),
  };
  for (size_t i = 0; i < N; ++i) {
    page->Put(keys[i].Clone(), values[i].Clone(), [](ledger::Status status) {
      EXPECT_EQ(status, ledger::Status::OK);
    });
    EXPECT_TRUE(page.WaitForIncomingResponse());
  }
  snapshot = PageGetSnapshot(&page);

  // Get all entries.
  entries = SnapshotGetEntries(&snapshot, fidl::Array<uint8_t>());
  EXPECT_EQ(N, entries.size());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_TRUE(keys[i].Equals(entries[i]->key));
    EXPECT_TRUE(values[i].Equals(ToArray(entries[i]->value)));
  }

  // Get entries matching the prefix "0".
  snapshot = PageGetSnapshot(
      &page, fidl::Array<uint8_t>::From(std::vector<uint8_t>{0}));
  entries = SnapshotGetEntries(&snapshot, fidl::Array<uint8_t>());
  EXPECT_EQ(N, entries.size());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_TRUE(keys[i].Equals(entries[i]->key));
    EXPECT_TRUE(values[i].Equals(ToArray(entries[i]->value)));
  }

  // Get entries matching the prefix "00".
  snapshot = PageGetSnapshot(
      &page, fidl::Array<uint8_t>::From(std::vector<uint8_t>{0, 0}));
  entries = SnapshotGetEntries(&snapshot, fidl::Array<uint8_t>());
  ASSERT_EQ(2u, entries.size());
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_TRUE(keys[i].Equals(entries[i]->key));
    EXPECT_TRUE(values[i].Equals(ToArray(entries[i]->value)));
  }

  // Get keys matching the prefix "010".
  snapshot = PageGetSnapshot(
      &page, fidl::Array<uint8_t>::From(std::vector<uint8_t>{0, 1, 0}));
  entries = SnapshotGetEntries(&snapshot, fidl::Array<uint8_t>());
  ASSERT_EQ(1u, entries.size());
  EXPECT_TRUE(keys[2].Equals(entries[0]->key));
  EXPECT_TRUE(values[2].Equals(ToArray(entries[0]->value)));

  // Get keys matching the prefix "5".
  snapshot = PageGetSnapshot(
      &page, fidl::Array<uint8_t>::From(std::vector<uint8_t>{5}));

  snapshot->GetEntries(
      fidl::Array<uint8_t>(), nullptr,
      [&entries](ledger::Status status, fidl::Array<ledger::EntryPtr> e,
                 fidl::Array<uint8_t> next_token) {
        EXPECT_EQ(ledger::Status::OK, status);
        EXPECT_TRUE(next_token.is_null());
        entries = std::move(e);
      });
  EXPECT_TRUE(snapshot.WaitForIncomingResponse());
  EXPECT_EQ(0u, entries.size());
}

TEST_F(PageSnapshotIntegrationTest, PageSnapshotGetEntriesMultiPartSize) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();

  // Grab a snapshot before adding any entries and verify that GetEntries()
  // returns empty results.
  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  int num_queries;
  fidl::Array<ledger::EntryPtr> entries =
      SnapshotGetEntries(&snapshot, fidl::Array<uint8_t>(), &num_queries);
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
  fidl::Array<uint8_t> keys[N];
  fidl::Array<uint8_t> values[N];
  for (size_t i = 0; i < N; ++i) {
    // Generate keys so that they are in increasing order to match the order
    // of results from GetEntries().
    keys[i] = RandomArray(
        key_size,
        {static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i & 0xFF)});
    values[i] = RandomArray(value_size);
  }

  for (size_t i = 0; i < N; ++i) {
    page->Put(keys[i].Clone(), values[i].Clone(), [](ledger::Status status) {
      EXPECT_EQ(status, ledger::Status::OK);
    });
    ASSERT_TRUE(page.WaitForIncomingResponse());
  }
  snapshot = PageGetSnapshot(&page);

  // Get all entries.
  entries = SnapshotGetEntries(&snapshot, fidl::Array<uint8_t>(), &num_queries);
  EXPECT_TRUE(num_queries > 1);
  ASSERT_EQ(N, entries.size());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_TRUE(keys[i].Equals(entries[i]->key));
    EXPECT_TRUE(values[i].Equals(ToArray(entries[i]->value)));
  }
}

TEST_F(PageSnapshotIntegrationTest, PageSnapshotGetEntriesMultiPartHandles) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();

  // Grab a snapshot before adding any entries and verify that GetEntries()
  // returns empty results.
  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  int num_queries;
  fidl::Array<ledger::EntryPtr> entries =
      SnapshotGetEntries(&snapshot, fidl::Array<uint8_t>(), &num_queries);
  EXPECT_EQ(0u, entries.size());
  EXPECT_EQ(1, num_queries);

  // Add entries and grab a new snapshot.
  const size_t N = 100;
  fidl::Array<uint8_t> keys[N];
  fidl::Array<uint8_t> values[N];
  for (size_t i = 0; i < N; ++i) {
    // Generate keys so that they are in increasing order to match the order
    // of results from GetEntries().
    keys[i] = RandomArray(
        20, {static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i & 0xFF)});
    values[i] = RandomArray(100);
  }

  for (size_t i = 0; i < N; ++i) {
    page->Put(keys[i].Clone(), values[i].Clone(), [](ledger::Status status) {
      EXPECT_EQ(status, ledger::Status::OK);
    });
    ASSERT_TRUE(page.WaitForIncomingResponse());
  }
  snapshot = PageGetSnapshot(&page);

  // Get all entries.
  entries = SnapshotGetEntries(&snapshot, fidl::Array<uint8_t>(), &num_queries);
  EXPECT_TRUE(num_queries > 1);
  ASSERT_EQ(N, entries.size());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_TRUE(keys[i].Equals(entries[i]->key));
    EXPECT_TRUE(values[i].Equals(ToArray(entries[i]->value)));
  }
}

TEST_F(PageSnapshotIntegrationTest, PageSnapshotGettersReturnSortedEntries) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();

  const size_t N = 4;
  fidl::Array<uint8_t> keys[N] = {
      RandomArray(20, {2}),
      RandomArray(20, {5}),
      RandomArray(20, {3}),
      RandomArray(20, {0}),
  };
  fidl::Array<uint8_t> values[N] = {
      RandomArray(20),
      RandomArray(20),
      RandomArray(20),
      RandomArray(20),
  };
  for (size_t i = 0; i < N; ++i) {
    page->Put(keys[i].Clone(), values[i].Clone(), [](ledger::Status status) {
      EXPECT_EQ(status, ledger::Status::OK);
    });
    EXPECT_TRUE(page.WaitForIncomingResponse());
  }

  // Get a snapshot.
  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);

  // Verify that GetKeys() results are sorted.
  fidl::Array<fidl::Array<uint8_t>> result =
      SnapshotGetKeys(&snapshot, fidl::Array<uint8_t>());
  EXPECT_TRUE(keys[3].Equals(result[0]));
  EXPECT_TRUE(keys[0].Equals(result[1]));
  EXPECT_TRUE(keys[2].Equals(result[2]));
  EXPECT_TRUE(keys[1].Equals(result[3]));

  // Verify that GetEntries() results are sorted.
  fidl::Array<ledger::EntryPtr> entries =
      SnapshotGetEntries(&snapshot, fidl::Array<uint8_t>());
  EXPECT_TRUE(keys[3].Equals(entries[0]->key));
  EXPECT_TRUE(values[3].Equals(ToArray(entries[0]->value)));
  EXPECT_TRUE(keys[0].Equals(entries[1]->key));
  EXPECT_TRUE(values[0].Equals(ToArray(entries[1]->value)));
  EXPECT_TRUE(keys[2].Equals(entries[2]->key));
  EXPECT_TRUE(values[2].Equals(ToArray(entries[2]->value)));
  EXPECT_TRUE(keys[1].Equals(entries[3]->key));
  EXPECT_TRUE(values[1].Equals(ToArray(entries[3]->value)));
}

TEST_F(PageSnapshotIntegrationTest, PageCreateReferenceFromSocketWrongSize) {
  auto instance = NewLedgerAppInstance();
  const std::string big_data(1'000'000, 'a');

  ledger::PagePtr page = instance->GetTestPage();

  page->CreateReferenceFromSocket(
      123, StreamDataToSocket(big_data),
      [](ledger::Status status, ledger::ReferencePtr ref) {
        EXPECT_EQ(ledger::Status::IO_ERROR, status);
      });
  ASSERT_TRUE(page.WaitForIncomingResponse());
}

TEST_F(PageSnapshotIntegrationTest, PageCreatePutLargeReferenceFromSocket) {
  auto instance = NewLedgerAppInstance();
  const std::string big_data(1'000'000, 'a');

  ledger::PagePtr page = instance->GetTestPage();

  // Stream the data into the reference.
  ledger::ReferencePtr reference;
  page->CreateReferenceFromSocket(
      big_data.size(), StreamDataToSocket(big_data),
      [&reference](ledger::Status status, ledger::ReferencePtr ref) {
        EXPECT_EQ(ledger::Status::OK, status);
        reference = std::move(ref);
      });
  ASSERT_TRUE(page.WaitForIncomingResponse());

  // Set the reference under a key.
  page->PutReference(convert::ToArray("big data"), std::move(reference),
                     ledger::Priority::EAGER, [](ledger::Status status) {
                       EXPECT_EQ(ledger::Status::OK, status);
                     });
  ASSERT_TRUE(page.WaitForIncomingResponse());

  // Get a snapshot and read the value.
  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  zx::vmo value;
  snapshot->Get(convert::ToArray("big data"),
                [&value](ledger::Status status, zx::vmo v) {
                  EXPECT_EQ(status, ledger::Status::OK);
                  value = std::move(v);
                });
  ASSERT_TRUE(snapshot.WaitForIncomingResponse());

  EXPECT_EQ(big_data, ToString(value));
}

TEST_F(PageSnapshotIntegrationTest, PageCreatePutLargeReferenceFromVmo) {
  auto instance = NewLedgerAppInstance();
  const std::string big_data(1'000'000, 'a');
  zx::vmo vmo;
  ASSERT_TRUE(fsl::VmoFromString(big_data, &vmo));

  ledger::PagePtr page = instance->GetTestPage();

  // Stream the data into the reference.
  ledger::ReferencePtr reference;
  page->CreateReferenceFromVmo(
      std::move(vmo),
      [&reference](ledger::Status status, ledger::ReferencePtr ref) {
        EXPECT_EQ(ledger::Status::OK, status);
        reference = std::move(ref);
      });
  ASSERT_TRUE(page.WaitForIncomingResponse());

  // Set the reference under a key.
  page->PutReference(convert::ToArray("big data"), std::move(reference),
                     ledger::Priority::EAGER, [](ledger::Status status) {
                       EXPECT_EQ(ledger::Status::OK, status);
                     });
  ASSERT_TRUE(page.WaitForIncomingResponse());

  // Get a snapshot and read the value.
  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  zx::vmo value;
  snapshot->Get(convert::ToArray("big data"),
                [&value](ledger::Status status, zx::vmo v) {
                  EXPECT_EQ(status, ledger::Status::OK);
                  value = std::move(v);
                });
  ASSERT_TRUE(snapshot.WaitForIncomingResponse());

  EXPECT_EQ(big_data, ToString(value));
}

TEST_F(PageSnapshotIntegrationTest, PageSnapshotClosePageGet) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();
  page->Put(
      convert::ToArray("name"), convert::ToArray("Alice"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);

  // Close the channel. ledger::PageSnapshotPtr should remain valid.
  page.reset();

  zx::vmo value;
  snapshot->Get(convert::ToArray("name"),
                [&value](ledger::Status status, zx::vmo v) {
                  EXPECT_EQ(status, ledger::Status::OK);
                  value = std::move(v);
                });
  EXPECT_TRUE(snapshot.WaitForIncomingResponse());
  EXPECT_EQ("Alice", ToString(value));

  // Attempt to get an entry that is not in the page.
  snapshot->Get(convert::ToArray("favorite book"),
                [](ledger::Status status, zx::vmo v) {
                  // People don't read much these days.
                  EXPECT_EQ(status, ledger::Status::KEY_NOT_FOUND);
                });
  EXPECT_TRUE(snapshot.WaitForIncomingResponse());
}

TEST_F(PageSnapshotIntegrationTest, PageGetById) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  page->Put(
      convert::ToArray("name"), convert::ToArray("Alice"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  page.reset();

  page = instance->GetPage(test_page_id, ledger::Status::OK);
  page->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    EXPECT_EQ(convert::ToString(test_page_id), convert::ToString(page_id));
  });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  ledger::PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  zx::vmo value;
  snapshot->Get(convert::ToArray("name"),
                [&value](ledger::Status status, zx::vmo v) {
                  EXPECT_EQ(status, ledger::Status::OK);
                  value = std::move(v);
                });
  EXPECT_TRUE(snapshot.WaitForIncomingResponse());
  EXPECT_EQ("Alice", ToString(value));
}

}  // namespace
}  // namespace integration
}  // namespace test

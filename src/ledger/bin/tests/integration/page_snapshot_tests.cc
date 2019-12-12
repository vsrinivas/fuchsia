// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/optional.h>
#include <zircon/errors.h>

#include <utility>
#include <vector>

#include "fuchsia/ledger/cpp/fidl.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/fidl/serialization_size.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/testing/ledger_matcher.h"
#include "src/ledger/bin/tests/integration/integration_test.h"
#include "src/ledger/bin/tests/integration/test_utils.h"
#include "src/ledger/lib/callback/capture.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/vmo/sized_vmo.h"
#include "src/ledger/lib/vmo/strings.h"
#include "src/lib/callback/waiter.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace ledger {
namespace {

class PageSnapshotIntegrationTest : public IntegrationTest {
 public:
  PageSnapshotIntegrationTest() = default;
  PageSnapshotIntegrationTest(const PageSnapshotIntegrationTest&) = delete;
  PageSnapshotIntegrationTest& operator=(const PageSnapshotIntegrationTest&) = delete;
  ~PageSnapshotIntegrationTest() override = default;

  // Returns a snapshot of |page|, checking success.
  PageSnapshotPtr PageGetSnapshot(PagePtr* page, std::vector<uint8_t> prefix = {}) {
    PageSnapshotPtr snapshot;
    (*page)->GetSnapshot(snapshot.NewRequest(), std::move(prefix), nullptr);
    return snapshot;
  }

  // Returns all keys from |snapshot|, starting at |start|. If |num_queries| is
  // not null, stores the number of calls to GetKeys.
  std::vector<std::vector<uint8_t>> SnapshotGetKeys(PageSnapshotPtr* snapshot,
                                                    std::vector<uint8_t> start = {},
                                                    int* num_queries = nullptr) {
    std::vector<std::vector<uint8_t>> result;
    std::unique_ptr<Token> token;
    if (num_queries) {
      *num_queries = 0;
    }
    do {
      std::vector<std::vector<uint8_t>> keys;
      auto waiter = NewWaiter();
      (*snapshot)->GetKeys(start, std::move(token), Capture(waiter->GetCallback(), &keys, &token));
      if (!waiter->RunUntilCalled()) {
        ADD_FAILURE() << "|GetKeys| failed to call back.";
        return {};
      }
      if (num_queries) {
        (*num_queries)++;
      }
      for (auto& key : keys) {
        result.push_back(std::move(key));
      }
    } while (token);
    return result;
  }

  std::string SnapshotFetchPartial(PageSnapshotPtr* snapshot, std::vector<uint8_t> key,
                                   int64_t offset, int64_t max_size) {
    fuchsia::ledger::PageSnapshot_FetchPartial_Result result;
    auto waiter = NewWaiter();
    (*snapshot)->FetchPartial(std::move(key), offset, max_size,
                              Capture(waiter->GetCallback(), &result));
    if (!waiter->RunUntilCalled()) {
      ADD_FAILURE() << "|FetchPartial| failed to call back.";
      return {};
    }
    EXPECT_TRUE(result.is_response());
    std::string result_as_string;
    EXPECT_TRUE(ledger::StringFromVmo(result.response().buffer, &result_as_string));
    return result_as_string;
  }
};

TEST_P(PageSnapshotIntegrationTest, PageSnapshotGet) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();
  page->Put(convert::ToArray("name"), convert::ToArray("Alice"));

  PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  fuchsia::ledger::PageSnapshot_Get_Result result;
  auto waiter = NewWaiter();
  snapshot->Get(convert::ToArray("name"), Capture(waiter->GetCallback(), &result));
  ASSERT_TRUE(waiter->RunUntilCalled());
  ASSERT_THAT(result, MatchesString("Alice"));

  // Attempt to get an entry that is not in the page.
  waiter = NewWaiter();
  snapshot->Get(convert::ToArray("favorite book"), Capture(waiter->GetCallback(), &result));
  ASSERT_TRUE(waiter->RunUntilCalled());
  // People don't read much these days.
  EXPECT_THAT(result, MatchesError(fuchsia::ledger::Error::KEY_NOT_FOUND));
}

TEST_P(PageSnapshotIntegrationTest, PageSnapshotGetPipeline) {
  auto instance = NewLedgerAppInstance();
  std::string expected_value = "Alice";
  expected_value.resize(100);

  PagePtr page = instance->GetTestPage();
  page->Put(convert::ToArray("name"), convert::ToArray(expected_value));

  PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  fuchsia::ledger::PageSnapshot_Get_Result result;
  auto waiter = NewWaiter();
  snapshot->Get(convert::ToArray("name"), Capture(waiter->GetCallback(), &result));
  ASSERT_TRUE(waiter->RunUntilCalled());
  ASSERT_THAT(result, MatchesString(expected_value));
}

TEST_P(PageSnapshotIntegrationTest, PageSnapshotPutOrder) {
  auto instance = NewLedgerAppInstance();
  std::string value1 = "Alice";
  value1.resize(100);
  std::string value2;

  // Put the 2 values without waiting for the callbacks.
  PagePtr page = instance->GetTestPage();
  page->Put(convert::ToArray("name"), convert::ToArray(value1));
  page->Put(convert::ToArray("name"), convert::ToArray(value2));

  PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  fuchsia::ledger::PageSnapshot_Get_Result result;
  auto waiter = NewWaiter();
  snapshot->Get(convert::ToArray("name"), Capture(waiter->GetCallback(), &result));
  ASSERT_TRUE(waiter->RunUntilCalled());
  ASSERT_THAT(result, MatchesString(value2));
}

TEST_P(PageSnapshotIntegrationTest, PageSnapshotFetchPartial) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();
  page->Put(convert::ToArray("name"), convert::ToArray("Alice"));

  PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  EXPECT_EQ(SnapshotFetchPartial(&snapshot, convert::ToArray("name"), 0, -1), "Alice");
  EXPECT_EQ(SnapshotFetchPartial(&snapshot, convert::ToArray("name"), 4, -1), "e");
  EXPECT_EQ(SnapshotFetchPartial(&snapshot, convert::ToArray("name"), 5, -1), "");
  EXPECT_EQ(SnapshotFetchPartial(&snapshot, convert::ToArray("name"), 6, -1), "");
  EXPECT_EQ(SnapshotFetchPartial(&snapshot, convert::ToArray("name"), 2, 1), "i");
  EXPECT_EQ(SnapshotFetchPartial(&snapshot, convert::ToArray("name"), 2, 0), "");

  // Negative offsets.
  EXPECT_EQ(SnapshotFetchPartial(&snapshot, convert::ToArray("name"), -5, -1), "Alice");
  EXPECT_EQ(SnapshotFetchPartial(&snapshot, convert::ToArray("name"), -1, -1), "e");
  EXPECT_EQ(SnapshotFetchPartial(&snapshot, convert::ToArray("name"), -5, 0), "");
  EXPECT_EQ(SnapshotFetchPartial(&snapshot, convert::ToArray("name"), -3, 1), "i");

  // Attempt to get an entry that is not in the page.
  fuchsia::ledger::PageSnapshot_FetchPartial_Result result;
  auto waiter = NewWaiter();
  snapshot->FetchPartial(convert::ToArray("favorite book"), 0, -1,
                         Capture(waiter->GetCallback(), &result));
  ASSERT_TRUE(waiter->RunUntilCalled());
  // People don't read much these days.
  EXPECT_THAT(result, MatchesError(fuchsia::ledger::Error::KEY_NOT_FOUND));
}

TEST_P(PageSnapshotIntegrationTest, PageSnapshotGetKeys) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();

  // Grab a snapshot before adding any entries and verify that GetKeys()
  // returns empty results.
  PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  std::vector<std::vector<uint8_t>> result = SnapshotGetKeys(&snapshot);
  EXPECT_EQ(result.size(), 0u);

  // Add entries and grab a new snapshot.
  const size_t N = 4;
  std::vector<uint8_t> keys[N] = {
      RandomArray(GetRandom(), 20, {0, 0, 0}),
      RandomArray(GetRandom(), 20, {0, 0, 1}),
      RandomArray(GetRandom(), 20, {0, 1, 0}),
      RandomArray(GetRandom(), 20, {0, 1, 1}),
  };
  for (auto& key : keys) {
    page->Put(key, RandomArray(GetRandom(), 50));
  }
  snapshot = PageGetSnapshot(&page);

  // Get all keys.
  result = SnapshotGetKeys(&snapshot);
  EXPECT_EQ(result.size(), N);
  for (size_t i = 0; i < N; ++i) {
    EXPECT_EQ(result.at(i), keys[i]);
  }

  // Get keys matching the prefix "0".
  snapshot = PageGetSnapshot(&page, {0});
  result = SnapshotGetKeys(&snapshot);
  EXPECT_EQ(result.size(), N);
  for (size_t i = 0; i < N; ++i) {
    EXPECT_EQ(result.at(i), keys[i]);
  }

  // Get keys matching the prefix "00".
  snapshot = PageGetSnapshot(&page, {0, 0});
  result = SnapshotGetKeys(&snapshot);
  ASSERT_EQ(result.size(), 2u);
  for (size_t i = 0; i < 2u; ++i) {
    EXPECT_EQ(result.at(i), keys[i]);
  }

  // Get keys matching the prefix "010".
  snapshot = PageGetSnapshot(&page, {0, 1, 0});
  result = SnapshotGetKeys(&snapshot);
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result.at(0), keys[2]);

  // Get keys matching the prefix "5".
  snapshot = PageGetSnapshot(&page, {5});
  result = SnapshotGetKeys(&snapshot);
  EXPECT_EQ(result.size(), 0u);

  // Get keys matching the prefix "0" and starting with the key "010".
  snapshot = PageGetSnapshot(&page, {0});
  result = SnapshotGetKeys(&snapshot, {0, 1, 0});
  EXPECT_EQ(result.size(), 2u);
}

TEST_P(PageSnapshotIntegrationTest, PageSnapshotGetKeysMultiPart) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();

  // Grab a snapshot before adding any entries and verify that GetKeys()
  // returns empty results.
  PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  int num_queries;
  std::vector<std::vector<uint8_t>> result = SnapshotGetKeys(&snapshot, {}, &num_queries);
  EXPECT_EQ(result.size(), 0u);
  EXPECT_EQ(num_queries, 1);

  // Add entries and grab a new snapshot.
  // Add enough keys so they don't all fit in memory and we will have to have
  // multiple queries.
  const size_t key_size = kMaxKeySize;
  const size_t N = fidl_serialization::kMaxInlineDataSize / key_size + 1;
  std::vector<uint8_t> keys[N];
  for (size_t i = 0; i < N; ++i) {
    // Generate keys so that they are in increasing order to match the order
    // of results from GetKeys().
    keys[i] = RandomArray(GetRandom(), key_size,
                          {static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i & 0xFF)});
  }

  for (auto& key : keys) {
    page->Put(key, RandomArray(GetRandom(), 10));
  }
  snapshot = PageGetSnapshot(&page);

  // Get all keys.
  result = SnapshotGetKeys(&snapshot, {}, &num_queries);
  EXPECT_TRUE(num_queries > 1);
  ASSERT_EQ(result.size(), N);
  for (size_t i = 0; i < N; ++i) {
    EXPECT_EQ(result.at(i), keys[i]);
  }
}

TEST_P(PageSnapshotIntegrationTest, PageSnapshotGetEntries) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();

  // Grab a snapshot before adding any entries and verify that GetEntries()
  // returns empty results.
  PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  auto entries = SnapshotGetEntries(this, &snapshot);
  EXPECT_EQ(entries.size(), 0u);

  // Add entries and grab a new snapshot.
  const size_t N = 4;
  std::vector<uint8_t> keys[N] = {
      RandomArray(GetRandom(), 20, {0, 0, 0}),
      RandomArray(GetRandom(), 20, {0, 0, 1}),
      RandomArray(GetRandom(), 20, {0, 1, 0}),
      RandomArray(GetRandom(), 20, {0, 1, 1}),
  };
  std::vector<uint8_t> values[N] = {
      RandomArray(GetRandom(), 50),
      RandomArray(GetRandom(), 50),
      RandomArray(GetRandom(), 50),
      RandomArray(GetRandom(), 50),
  };
  for (size_t i = 0; i < N; ++i) {
    page->Put(keys[i], values[i]);
  }
  snapshot = PageGetSnapshot(&page);

  // Get all entries.
  entries = SnapshotGetEntries(this, &snapshot);
  EXPECT_EQ(entries.size(), N);
  for (size_t i = 0; i < N; ++i) {
    EXPECT_EQ(entries.at(i).key, keys[i]);
    EXPECT_EQ(ToArray(entries.at(i).value), values[i]);
  }

  // Get entries matching the prefix "0".
  snapshot = PageGetSnapshot(&page, {0});
  entries = SnapshotGetEntries(this, &snapshot);
  EXPECT_EQ(entries.size(), N);
  for (size_t i = 0; i < N; ++i) {
    EXPECT_EQ(entries.at(i).key, keys[i]);
    EXPECT_EQ(ToArray(entries.at(i).value), values[i]);
  }

  // Get entries matching the prefix "00".
  snapshot = PageGetSnapshot(&page, {0, 0});
  entries = SnapshotGetEntries(this, &snapshot);
  ASSERT_EQ(entries.size(), 2u);
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_EQ(entries.at(i).key, keys[i]);
    EXPECT_EQ(ToArray(entries.at(i).value), values[i]);
  }

  // Get keys matching the prefix "010".
  snapshot = PageGetSnapshot(&page, {0, 1, 0});
  entries = SnapshotGetEntries(this, &snapshot);
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries.at(0).key, keys[2]);
  EXPECT_EQ(ToArray(entries.at(0).value), values[2]);

  // Get keys matching the prefix "5".
  snapshot = PageGetSnapshot(&page, {5});

  entries = SnapshotGetEntries(this, &snapshot);
  EXPECT_EQ(entries.size(), 0u);
}

TEST_P(PageSnapshotIntegrationTest, PageSnapshotGetEntriesMultiPartSize) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();

  // Grab a snapshot before adding any entries and verify that GetEntries()
  // returns empty results.
  PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  int num_queries;
  auto entries = SnapshotGetEntries(this, &snapshot, {}, &num_queries);
  EXPECT_EQ(entries.size(), 0u);
  EXPECT_EQ(num_queries, 1);

  // Add entries and grab a new snapshot.
  // Add enough keys so they don't all fit in memory and we will have to have
  // multiple queries.
  const size_t value_size = 100;
  const size_t key_size = kMaxKeySize;
  const size_t N = fidl_serialization::kMaxInlineDataSize / (key_size + value_size) + 1;
  std::vector<uint8_t> keys[N];
  std::vector<uint8_t> values[N];
  for (size_t i = 0; i < N; ++i) {
    // Generate keys so that they are in increasing order to match the order
    // of results from GetEntries().
    keys[i] = RandomArray(GetRandom(), key_size,
                          {static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i & 0xFF)});
    values[i] = RandomArray(GetRandom(), value_size);
  }

  for (size_t i = 0; i < N; ++i) {
    page->Put(keys[i], values[i]);
  }
  snapshot = PageGetSnapshot(&page);

  // Get all entries.
  entries = SnapshotGetEntries(this, &snapshot, {}, &num_queries);
  EXPECT_TRUE(num_queries > 1);
  ASSERT_EQ(entries.size(), N);
  for (size_t i = 0; i < N; ++i) {
    EXPECT_EQ(entries[i].key, keys[i]);
    EXPECT_EQ(ToArray(entries[i].value), values[i]);
  }
}

TEST_P(PageSnapshotIntegrationTest, PageSnapshotGetEntriesMultiPartHandles) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();

  // Grab a snapshot before adding any entries and verify that GetEntries()
  // returns empty results.
  PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  int num_queries;
  auto entries = SnapshotGetEntries(this, &snapshot, {}, &num_queries);
  EXPECT_EQ(entries.size(), 0u);
  EXPECT_EQ(num_queries, 1);

  // Add entries and grab a new snapshot.
  const size_t N = 100;
  std::vector<uint8_t> keys[N];
  std::vector<uint8_t> values[N];
  for (size_t i = 0; i < N; ++i) {
    // Generate keys so that they are in increasing order to match the order
    // of results from GetEntries().
    keys[i] = RandomArray(GetRandom(), 20,
                          {static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i & 0xFF)});
    values[i] = RandomArray(GetRandom(), 100);
  }

  for (size_t i = 0; i < N; ++i) {
    page->Put(keys[i], values[i]);
  }
  snapshot = PageGetSnapshot(&page);

  // Get all entries.
  entries = SnapshotGetEntries(this, &snapshot, {}, &num_queries);
  EXPECT_TRUE(num_queries > 1);
  ASSERT_EQ(entries.size(), N);
  for (size_t i = 0; i < N; ++i) {
    EXPECT_EQ(entries[i].key, keys[i]);
    EXPECT_EQ(ToArray(entries[i].value), values[i]);
  }
}

TEST_P(PageSnapshotIntegrationTest, PageSnapshotGettersReturnSortedEntries) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();

  const size_t N = 4;
  std::vector<uint8_t> keys[N] = {
      RandomArray(GetRandom(), 20, {2}),
      RandomArray(GetRandom(), 20, {5}),
      RandomArray(GetRandom(), 20, {3}),
      RandomArray(GetRandom(), 20, {0}),
  };
  std::vector<uint8_t> values[N] = {
      RandomArray(GetRandom(), 20),
      RandomArray(GetRandom(), 20),
      RandomArray(GetRandom(), 20),
      RandomArray(GetRandom(), 20),
  };
  for (size_t i = 0; i < N; ++i) {
    page->Put(keys[i], values[i]);
  }

  // Get a snapshot.
  PageSnapshotPtr snapshot = PageGetSnapshot(&page);

  // Verify that GetKeys() results are sorted.
  std::vector<std::vector<uint8_t>> result = SnapshotGetKeys(&snapshot);
  EXPECT_EQ(result.at(0), keys[3]);
  EXPECT_EQ(result.at(1), keys[0]);
  EXPECT_EQ(result.at(2), keys[2]);
  EXPECT_EQ(result.at(3), keys[1]);

  // Verify that GetEntries() results are sorted.
  auto entries = SnapshotGetEntries(this, &snapshot);
  EXPECT_EQ(entries[0].key, keys[3]);
  EXPECT_EQ(ToArray(entries[0].value), values[3]);
  EXPECT_EQ(entries[1].key, keys[0]);
  EXPECT_EQ(ToArray(entries[1].value), values[0]);
  EXPECT_EQ(entries[2].key, keys[2]);
  EXPECT_EQ(ToArray(entries[2].value), values[2]);
  EXPECT_EQ(entries[3].key, keys[1]);
  EXPECT_EQ(ToArray(entries[3].value), values[1]);
}

TEST_P(PageSnapshotIntegrationTest, PageCreateReferenceFromSocketWrongSize) {
  auto instance = NewLedgerAppInstance();
  const std::string big_data(1'000'000, 'a');

  PagePtr page = instance->GetTestPage();

  fuchsia::ledger::Page_CreateReferenceFromSocket_Result result;
  auto waiter = NewWaiter();
  page->CreateReferenceFromSocket(123, StreamDataToSocket(big_data),
                                  Capture(waiter->GetCallback(), &result));
  ASSERT_TRUE(waiter->RunUntilCalled());
  ASSERT_TRUE(result.is_err());
  EXPECT_EQ(result.err(), ZX_ERR_INVALID_ARGS);
}

TEST_P(PageSnapshotIntegrationTest, PageCreatePutLargeReferenceFromSocket) {
  auto instance = NewLedgerAppInstance();
  const std::string big_data(1'000'000, 'a');

  PagePtr page = instance->GetTestPage();

  // Stream the data into the reference.
  fuchsia::ledger::Page_CreateReferenceFromSocket_Result create_result;
  auto waiter = NewWaiter();
  page->CreateReferenceFromSocket(big_data.size(), StreamDataToSocket(big_data),
                                  Capture(waiter->GetCallback(), &create_result));
  ASSERT_TRUE(waiter->RunUntilCalled());
  EXPECT_TRUE(create_result.is_response());

  // Set the reference under a key.
  page->PutReference(convert::ToArray("big data"), std::move(create_result.response().reference),
                     Priority::EAGER);

  // Get a snapshot and read the value.
  PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  fuchsia::ledger::PageSnapshot_Get_Result get_result;
  waiter = NewWaiter();
  snapshot->Get(convert::ToArray("big data"), Capture(waiter->GetCallback(), &get_result));
  ASSERT_TRUE(waiter->RunUntilCalled());
  EXPECT_THAT(get_result, MatchesString(big_data));
}

TEST_P(PageSnapshotIntegrationTest, PageCreatePutLargeReferenceFromVmo) {
  auto instance = NewLedgerAppInstance();
  const std::string big_data(1'000'000, 'a');
  ledger::SizedVmo vmo;
  ASSERT_TRUE(ledger::VmoFromString(big_data, &vmo));

  PagePtr page = instance->GetTestPage();

  // Stream the data into the reference.
  fuchsia::ledger::Page_CreateReferenceFromBuffer_Result create_result;
  auto waiter = NewWaiter();
  page->CreateReferenceFromBuffer(std::move(vmo).ToTransport(),
                                  Capture(waiter->GetCallback(), &create_result));
  ASSERT_TRUE(waiter->RunUntilCalled());
  EXPECT_TRUE(create_result.is_response());

  // Set the reference under a key.
  page->PutReference(convert::ToArray("big data"), std::move(create_result.response().reference),
                     Priority::EAGER);

  // Get a snapshot and read the value.
  PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  fuchsia::ledger::PageSnapshot_Get_Result get_result;
  waiter = NewWaiter();
  snapshot->Get(convert::ToArray("big data"), Capture(waiter->GetCallback(), &get_result));
  ASSERT_TRUE(waiter->RunUntilCalled());
  EXPECT_THAT(get_result, MatchesString(big_data));
}

TEST_P(PageSnapshotIntegrationTest, PageSnapshotClosePageGet) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();
  page->Put(convert::ToArray("name"), convert::ToArray("Alice"));

  PageSnapshotPtr snapshot = PageGetSnapshot(&page);

  // Close the channel. PageSnapshotPtr should remain valid.
  page.Unbind();

  fuchsia::ledger::PageSnapshot_Get_Result result;
  auto waiter = NewWaiter();
  snapshot->Get(convert::ToArray("name"), Capture(waiter->GetCallback(), &result));
  ASSERT_TRUE(waiter->RunUntilCalled());
  EXPECT_THAT(result, MatchesString("Alice"));

  // Attempt to get an entry that is not in the page.
  waiter = NewWaiter();
  snapshot->Get(convert::ToArray("favorite book"), Capture(waiter->GetCallback(), &result));
  ASSERT_TRUE(waiter->RunUntilCalled());
  // People don't read much these days.
  EXPECT_THAT(result, MatchesError(fuchsia::ledger::Error::KEY_NOT_FOUND));
}

TEST_P(PageSnapshotIntegrationTest, PageGetById) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();
  PageId test_page_id;
  auto waiter = NewWaiter();
  page->GetId(Capture(waiter->GetCallback(), &test_page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());

  page->Put(convert::ToArray("name"), convert::ToArray("Alice"));
  // Waiting to sync, otherwise the snapshot requested in the rest of the test
  // might be bound before |Put| has terminated.
  waiter = NewWaiter();
  page->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  page.Unbind();

  page = instance->GetPage(fidl::MakeOptional(test_page_id));
  PageId page_id;
  waiter = NewWaiter();
  page->GetId(Capture(waiter->GetCallback(), &page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());
  EXPECT_EQ(page_id.id, test_page_id.id);

  PageSnapshotPtr snapshot = PageGetSnapshot(&page);
  fuchsia::ledger::PageSnapshot_Get_Result result;
  waiter = NewWaiter();
  snapshot->Get(convert::ToArray("name"), Capture(waiter->GetCallback(), &result));
  ASSERT_TRUE(waiter->RunUntilCalled());
  EXPECT_THAT(result, MatchesString("Alice"));
}

INSTANTIATE_TEST_SUITE_P(PageSnapshotIntegrationTest, PageSnapshotIntegrationTest,
                         ::testing::ValuesIn(GetLedgerAppInstanceFactoryBuilders(
                             EnableSynchronization::SYNC_OR_OFFLINE_DIFFS_IRRELEVANT)),
                         PrintLedgerAppInstanceFactoryBuilder());

}  // namespace
}  // namespace ledger

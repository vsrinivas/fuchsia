// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_connection_notifier.h"

#include <fuchsia/ledger/cpp/fidl.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>

#include <random>

#include "gtest/gtest.h"
#include "src/ledger/bin/testing/fake_disk_cleanup_manager.h"
#include "src/ledger/bin/testing/test_with_environment.h"

namespace ledger {
namespace {

constexpr char kLedgerName[] = "test_ledger_name";

class PageConnectionNotifierTest : public TestWithEnvironment {
 public:
  PageConnectionNotifierTest()
      : page_connection_notifier_(kLedgerName, std::string(::fuchsia::ledger::PAGE_ID_SIZE, '3'),
                                  std::vector<PageUsageListener*>{&fake_disk_cleanup_manager_}){};
  ~PageConnectionNotifierTest() override = default;

 protected:
  FakeDiskCleanupManager fake_disk_cleanup_manager_;
  PageConnectionNotifier page_connection_notifier_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageConnectionNotifierTest);
};

TEST_F(PageConnectionNotifierTest, SingleExternalRequest) {
  page_connection_notifier_.RegisterExternalRequest();

  EXPECT_EQ(fake_disk_cleanup_manager_.externally_used_count, 1);
  EXPECT_FALSE(page_connection_notifier_.IsEmpty());
}

TEST_F(PageConnectionNotifierTest, MultipleExternalRequests) {
  page_connection_notifier_.RegisterExternalRequest();
  page_connection_notifier_.RegisterExternalRequest();
  page_connection_notifier_.RegisterExternalRequest();

  EXPECT_EQ(fake_disk_cleanup_manager_.externally_used_count, 1);
  EXPECT_FALSE(page_connection_notifier_.IsEmpty());
}

TEST_F(PageConnectionNotifierTest, UnregisteredExternalRequests) {
  bool on_empty_called;

  page_connection_notifier_.set_on_empty(callback::SetWhenCalled(&on_empty_called));
  page_connection_notifier_.RegisterExternalRequest();
  page_connection_notifier_.UnregisterExternalRequests();

  EXPECT_EQ(fake_disk_cleanup_manager_.externally_used_count, 1);
  EXPECT_EQ(fake_disk_cleanup_manager_.externally_unused_count, 1);
  EXPECT_TRUE(page_connection_notifier_.IsEmpty());
  EXPECT_TRUE(on_empty_called);
}

TEST_F(PageConnectionNotifierTest, SingleExpiringTokenImmediatelyDiscarded) {
  bool on_empty_called;

  page_connection_notifier_.set_on_empty(callback::SetWhenCalled(&on_empty_called));
  page_connection_notifier_.NewInternalRequestToken();

  EXPECT_TRUE(page_connection_notifier_.IsEmpty());
  EXPECT_TRUE(on_empty_called);
}

TEST_F(PageConnectionNotifierTest, SingleExpiringTokenNotImmediatelyDiscarded) {
  bool on_empty_called;

  page_connection_notifier_.set_on_empty(callback::SetWhenCalled(&on_empty_called));
  {
    auto expiring_token = page_connection_notifier_.NewInternalRequestToken();

    EXPECT_FALSE(page_connection_notifier_.IsEmpty());
    EXPECT_FALSE(on_empty_called);
  }
  EXPECT_TRUE(page_connection_notifier_.IsEmpty());
  EXPECT_TRUE(on_empty_called);
}

TEST_F(PageConnectionNotifierTest, MultipleExpiringTokensNotImmediatelyDiscarded) {
  auto bit_generator = environment_.random()->NewBitGenerator<size_t>();
  int token_count = std::uniform_int_distribution(2, 20)(bit_generator);
  bool on_empty_called;
  std::vector<std::unique_ptr<ExpiringToken>> tokens;

  page_connection_notifier_.set_on_empty(callback::SetWhenCalled(&on_empty_called));
  for (int i = 0; i < token_count; i++) {
    tokens.emplace_back(
        std::make_unique<ExpiringToken>(page_connection_notifier_.NewInternalRequestToken()));
    EXPECT_FALSE(page_connection_notifier_.IsEmpty());
    EXPECT_FALSE(on_empty_called);
  }
  // Destroy the tokens in random order; the PageConnectionNotifier will stay
  // not-empty until all the tokens have been destroyed.
  std::shuffle(tokens.begin(), tokens.end(), bit_generator);
  while (tokens.size() > 1) {
    tokens.pop_back();
    EXPECT_FALSE(page_connection_notifier_.IsEmpty());
    EXPECT_FALSE(on_empty_called);
  }
  tokens.pop_back();
  EXPECT_TRUE(page_connection_notifier_.IsEmpty());
  EXPECT_TRUE(on_empty_called);
}

TEST_F(PageConnectionNotifierTest, MultipleExternalRequestsAndMultipleExpiringTokensDiscarded) {
  auto bit_generator = environment_.random()->NewBitGenerator<size_t>();
  size_t token_count = std::uniform_int_distribution(2u, 20u)(bit_generator);
  size_t unregister_requests_when_tokens_remain =
      std::uniform_int_distribution<size_t>(0u, token_count)(bit_generator);
  bool on_empty_called;
  std::vector<std::unique_ptr<ExpiringToken>> tokens;

  page_connection_notifier_.set_on_empty(callback::SetWhenCalled(&on_empty_called));
  EXPECT_TRUE(page_connection_notifier_.IsEmpty());
  for (size_t i = 0; i < token_count; i++) {
    page_connection_notifier_.RegisterExternalRequest();
    EXPECT_FALSE(page_connection_notifier_.IsEmpty());
    EXPECT_FALSE(on_empty_called);
    tokens.emplace_back(
        std::make_unique<ExpiringToken>(page_connection_notifier_.NewInternalRequestToken()));
    EXPECT_FALSE(page_connection_notifier_.IsEmpty());
    EXPECT_FALSE(on_empty_called);
  }
  // We'll be deleting the tokens in an order randomized relative to the order
  // in which they were created.
  std::shuffle(tokens.begin(), tokens.end(), bit_generator);
  // Fencepost logic: because we want the single UnregisterExternalRequests call
  // to be made before the token deletions, somewhere in the middle of the token
  // deletions, or after all the token deletions, there are token_count + 1
  // places where it might be made.
  if (unregister_requests_when_tokens_remain == token_count) {
    page_connection_notifier_.UnregisterExternalRequests();
  }
  while (tokens.size() > 1) {
    tokens.pop_back();
    EXPECT_FALSE(page_connection_notifier_.IsEmpty());
    EXPECT_FALSE(on_empty_called);
    if (unregister_requests_when_tokens_remain == tokens.size()) {
      page_connection_notifier_.UnregisterExternalRequests();
      EXPECT_FALSE(page_connection_notifier_.IsEmpty());
      EXPECT_FALSE(on_empty_called);
    }
  }
  tokens.pop_back();
  if (unregister_requests_when_tokens_remain == 0) {
    EXPECT_FALSE(page_connection_notifier_.IsEmpty());
    EXPECT_FALSE(on_empty_called);
    page_connection_notifier_.UnregisterExternalRequests();
  }
  EXPECT_TRUE(page_connection_notifier_.IsEmpty());
  EXPECT_TRUE(on_empty_called);
}

TEST_F(PageConnectionNotifierTest, PageConnectionNotifierDestroyedWhileRequestsOutstanding) {
  FakeDiskCleanupManager fake_disk_cleanup_manager;
  bool on_empty_called;

  auto page_connection_notifier = std::make_unique<PageConnectionNotifier>(
      kLedgerName, std::string(::fuchsia::ledger::PAGE_ID_SIZE, '3'),
      std::vector<PageUsageListener*>{&fake_disk_cleanup_manager});
  page_connection_notifier->set_on_empty(callback::SetWhenCalled(&on_empty_called));
  page_connection_notifier->RegisterExternalRequest();
  page_connection_notifier->RegisterExternalRequest();
  page_connection_notifier->RegisterExternalRequest();
  page_connection_notifier.reset();

  EXPECT_FALSE(on_empty_called);
}

TEST_F(PageConnectionNotifierTest, PageConnectionNotifierDestroyedWhileTokensOutstanding) {
  FakeDiskCleanupManager fake_disk_cleanup_manager;
  bool on_empty_called;

  auto page_connection_notifier = std::make_unique<PageConnectionNotifier>(
      kLedgerName, std::string(::fuchsia::ledger::PAGE_ID_SIZE, '3'),
      std::vector<PageUsageListener*>{&fake_disk_cleanup_manager});
  page_connection_notifier->set_on_empty(callback::SetWhenCalled(&on_empty_called));
  auto first_expiring_token = page_connection_notifier->NewInternalRequestToken();
  auto second_expiring_token = page_connection_notifier->NewInternalRequestToken();
  page_connection_notifier.reset();

  EXPECT_FALSE(on_empty_called);
}

TEST_F(PageConnectionNotifierTest,
       PageConnectionNotifierDestroyedWhileRequestsAndTokensOutstanding) {
  FakeDiskCleanupManager fake_disk_cleanup_manager;
  bool on_empty_called;

  auto page_connection_notifier = std::make_unique<PageConnectionNotifier>(
      kLedgerName, std::string(::fuchsia::ledger::PAGE_ID_SIZE, '3'),
      std::vector<PageUsageListener*>{&fake_disk_cleanup_manager});
  page_connection_notifier->set_on_empty(callback::SetWhenCalled(&on_empty_called));
  page_connection_notifier->RegisterExternalRequest();
  page_connection_notifier->RegisterExternalRequest();
  page_connection_notifier->RegisterExternalRequest();
  auto first_expiring_token = page_connection_notifier->NewInternalRequestToken();
  auto second_expiring_token = page_connection_notifier->NewInternalRequestToken();
  page_connection_notifier.reset();

  EXPECT_FALSE(on_empty_called);
}

TEST_F(PageConnectionNotifierTest, PageConnectionNotifierDestroyedWhileCallingPageUsageListener) {
  FakeDiskCleanupManager fake_disk_cleanup_manager;
  auto bit_generator = environment_.random()->NewBitGenerator<size_t>();
  size_t token_count = std::uniform_int_distribution(2u, 20u)(bit_generator);
  size_t unregister_requests_when_tokens_remain =
      std::uniform_int_distribution<size_t>(0u, token_count)(bit_generator);
  bool on_empty_called;
  bool on_OnExternallyUnused_called = false;
  bool on_OnInternallyUnused_called = false;
  std::vector<std::unique_ptr<ExpiringToken>> tokens;

  auto page_connection_notifier = std::make_unique<PageConnectionNotifier>(
      kLedgerName, std::string(::fuchsia::ledger::PAGE_ID_SIZE, '3'),
      std::vector<PageUsageListener*>{&fake_disk_cleanup_manager});
  page_connection_notifier->set_on_empty(callback::SetWhenCalled(&on_empty_called));
  fake_disk_cleanup_manager.set_on_OnExternallyUnused(
      [&on_OnExternallyUnused_called, &on_OnInternallyUnused_called,
       page_connection_notifier_ptr = &page_connection_notifier] {
        on_OnExternallyUnused_called = true;
        if (on_OnInternallyUnused_called) {
          page_connection_notifier_ptr->reset();
        }
      });
  fake_disk_cleanup_manager.set_on_OnInternallyUnused(
      [&on_OnExternallyUnused_called, &on_OnInternallyUnused_called,
       page_connection_notifier_ptr = &page_connection_notifier] {
        on_OnInternallyUnused_called = true;
        if (on_OnExternallyUnused_called) {
          page_connection_notifier_ptr->reset();
        }
      });

  EXPECT_TRUE(page_connection_notifier->IsEmpty());
  page_connection_notifier->RegisterExternalRequest();
  page_connection_notifier->RegisterExternalRequest();
  page_connection_notifier->RegisterExternalRequest();
  for (size_t i = 0; i < token_count; i++) {
    page_connection_notifier->RegisterExternalRequest();
    EXPECT_FALSE(page_connection_notifier->IsEmpty());
    EXPECT_FALSE(on_empty_called);
    tokens.emplace_back(
        std::make_unique<ExpiringToken>(page_connection_notifier->NewInternalRequestToken()));
    EXPECT_FALSE(page_connection_notifier->IsEmpty());
    EXPECT_FALSE(on_empty_called);
  }
  // We'll be deleting the tokens in an order randomized relative to the order
  // in which they were created.
  std::shuffle(tokens.begin(), tokens.end(), bit_generator);
  // Fencepost logic: because we want the single UnregisterExternalRequests call
  // to be made before the token deletions, somewhere in the middle of the token
  // deletions, or after all the token deletions, there are token_count + 1
  // places where it might be made.
  if (unregister_requests_when_tokens_remain == token_count) {
    page_connection_notifier->UnregisterExternalRequests();
    EXPECT_TRUE(on_OnExternallyUnused_called);
  }
  while (tokens.size() > 1) {
    tokens.pop_back();
    EXPECT_FALSE(page_connection_notifier->IsEmpty());
    EXPECT_FALSE(on_OnInternallyUnused_called);
    EXPECT_FALSE(on_empty_called);
    if (unregister_requests_when_tokens_remain == tokens.size()) {
      page_connection_notifier->UnregisterExternalRequests();
      EXPECT_TRUE(on_OnExternallyUnused_called);
      EXPECT_FALSE(on_OnInternallyUnused_called);
      EXPECT_FALSE(page_connection_notifier->IsEmpty());
      EXPECT_FALSE(on_empty_called);
    }
  }
  tokens.pop_back();
  EXPECT_TRUE(on_OnInternallyUnused_called);
  if (unregister_requests_when_tokens_remain == 0) {
    EXPECT_FALSE(page_connection_notifier->IsEmpty());
    EXPECT_FALSE(on_OnExternallyUnused_called);
    EXPECT_FALSE(on_empty_called);
    page_connection_notifier->UnregisterExternalRequests();
  }
  EXPECT_FALSE(on_empty_called);
}

TEST_F(PageConnectionNotifierTest, MultiplePageUsageListeners) {
  std::vector<FakeDiskCleanupManager> page_usage_listeners(3);
  std::vector<PageUsageListener*> page_usage_listeners_ptr;
  for (auto& page_usage_listener : page_usage_listeners) {
    page_usage_listeners_ptr.push_back(&page_usage_listener);
  }

  auto page_connection_notifier = std::make_unique<PageConnectionNotifier>(
      kLedgerName, std::string(::fuchsia::ledger::PAGE_ID_SIZE, '3'), page_usage_listeners_ptr);
  page_connection_notifier->RegisterExternalRequest();
  page_connection_notifier->UnregisterExternalRequests();

  for (const auto& page_usage_listener : page_usage_listeners) {
    EXPECT_EQ(page_usage_listener.externally_used_count, 1);
    EXPECT_EQ(page_usage_listener.externally_unused_count, 1);
  }
  EXPECT_TRUE(page_connection_notifier_.IsEmpty());
}

}  // namespace
}  // namespace ledger

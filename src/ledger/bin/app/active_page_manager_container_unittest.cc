// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/active_page_manager_container.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/storage/fake/fake_page_storage.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/fake_disk_cleanup_manager.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"
#include "src/lib/fxl/strings/string_view.h"

namespace ledger {
namespace {

using ::testing::Each;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::SizeIs;

constexpr fxl::StringView kLedgerName = "test_ledger_name";

class ActivePageManagerContainerTest : public TestWithEnvironment {
 public:
  ActivePageManagerContainerTest() : page_id_(::fuchsia::ledger::PAGE_ID_SIZE, '3') {
    auto page_storage = std::make_unique<storage::fake::FakePageStorage>(&environment_, page_id_);
    auto merge_resolver =
        std::make_unique<MergeResolver>([] {}, &environment_, page_storage.get(), nullptr);
    active_page_manager_ = std::make_unique<ActivePageManager>(
        &environment_, std::move(page_storage), nullptr, std::move(merge_resolver),
        ActivePageManager::PageStorageState::AVAILABLE);
  }
  ActivePageManagerContainerTest(const ActivePageManagerContainerTest&) = delete;
  ActivePageManagerContainerTest& operator=(const ActivePageManagerContainerTest&) = delete;
  ~ActivePageManagerContainerTest() override = default;

 protected:
  storage::PageId page_id_;
  std::unique_ptr<ActivePageManager> active_page_manager_;
  FakeDiskCleanupManager fake_disk_cleanup_manager_;
};

TEST_F(ActivePageManagerContainerTest, OneEarlyBindingNoPageManager) {
  storage::PageId page_id = std::string(::fuchsia::ledger::PAGE_ID_SIZE, 'a');
  PagePtr page;
  bool callback_called;
  Status status;
  bool on_discardable_called;

  ActivePageManagerContainer active_page_manager_container(
      &environment_, kLedgerName.ToString(), page_id,
      std::vector<PageUsageListener*>{&fake_disk_cleanup_manager_});
  active_page_manager_container.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  active_page_manager_container.BindPage(
      page.NewRequest(), callback::Capture(callback::SetWhenCalled(&callback_called), &status));
  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_FALSE(on_discardable_called);

  active_page_manager_container.SetActivePageManager(Status::IO_ERROR, nullptr);
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(status, Status::IO_ERROR);
  EXPECT_TRUE(on_discardable_called);

  // We expect that the page unbinding will have no further effect.
  callback_called = false;
  on_discardable_called = false;
  page.Unbind();
  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_FALSE(on_discardable_called);
}

TEST_F(ActivePageManagerContainerTest, BindBeforePageManager) {
  PagePtr page;
  bool callback_called;
  Status status;
  bool on_discardable_called;

  ActivePageManagerContainer active_page_manager_container(
      &environment_, kLedgerName.ToString(), page_id_,
      std::vector<PageUsageListener*>{&fake_disk_cleanup_manager_});
  active_page_manager_container.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  active_page_manager_container.BindPage(
      page.NewRequest(), callback::Capture(callback::SetWhenCalled(&callback_called), &status));
  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  active_page_manager_container.SetActivePageManager(Status::OK, std::move(active_page_manager_));

  EXPECT_TRUE(callback_called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_FALSE(on_discardable_called);

  // We expect that the page unbinding will empty the ActivePageManagerContainer
  // but will not cause the page's own callback to be called again.
  callback_called = false;
  page.Unbind();
  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_TRUE(on_discardable_called);
}

TEST_F(ActivePageManagerContainerTest, SingleExternalRequest) {
  PagePtr page;
  bool callback_called;
  Status status;
  bool on_discardable_called;

  ActivePageManagerContainer active_page_manager_container(
      &environment_, kLedgerName.ToString(), page_id_,
      std::vector<PageUsageListener*>{&fake_disk_cleanup_manager_});
  active_page_manager_container.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  active_page_manager_container.BindPage(
      page.NewRequest(), callback::Capture(callback::SetWhenCalled(&callback_called), &status));
  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_FALSE(on_discardable_called);
  EXPECT_EQ(fake_disk_cleanup_manager_.externally_used_count, 1);

  active_page_manager_container.SetActivePageManager(Status::OK, std::move(active_page_manager_));
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_EQ(fake_disk_cleanup_manager_.externally_used_count, 1);
  EXPECT_FALSE(on_discardable_called);
}

TEST_F(ActivePageManagerContainerTest, MultipleExternalRequests) {
  bool on_discardable_called;

  ActivePageManagerContainer active_page_manager_container(
      &environment_, kLedgerName.ToString(), page_id_,
      std::vector<PageUsageListener*>{&fake_disk_cleanup_manager_});
  active_page_manager_container.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  PagePtr first_connection;
  bool first_callback_called;
  Status first_status;
  active_page_manager_container.BindPage(
      first_connection.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&first_callback_called), &first_status));
  RunLoopUntilIdle();
  EXPECT_FALSE(first_callback_called);
  EXPECT_FALSE(on_discardable_called);
  EXPECT_EQ(fake_disk_cleanup_manager_.externally_used_count, 1);

  PagePtr second_connection;
  PagePtr third_connection;
  bool second_callback_called;
  bool third_callback_called;
  Status second_status;
  Status third_status;
  active_page_manager_container.BindPage(
      second_connection.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&second_callback_called), &second_status));
  active_page_manager_container.BindPage(
      third_connection.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&third_callback_called), &third_status));
  RunLoopUntilIdle();
  EXPECT_FALSE(first_callback_called);
  EXPECT_FALSE(second_callback_called);
  EXPECT_FALSE(third_callback_called);
  EXPECT_FALSE(on_discardable_called);
  EXPECT_EQ(fake_disk_cleanup_manager_.externally_used_count, 1);

  active_page_manager_container.SetActivePageManager(Status::OK, std::move(active_page_manager_));
  EXPECT_TRUE(first_callback_called);
  EXPECT_EQ(first_status, Status::OK);
  EXPECT_TRUE(second_callback_called);
  EXPECT_EQ(second_status, Status::OK);
  EXPECT_TRUE(third_callback_called);
  EXPECT_EQ(third_status, Status::OK);
  EXPECT_EQ(fake_disk_cleanup_manager_.externally_used_count, 1);
  EXPECT_FALSE(on_discardable_called);
}

TEST_F(ActivePageManagerContainerTest, UnregisteredExternalRequests) {
  bool on_discardable_called;

  ActivePageManagerContainer active_page_manager_container(
      &environment_, kLedgerName.ToString(), page_id_,
      std::vector<PageUsageListener*>{&fake_disk_cleanup_manager_});
  active_page_manager_container.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  PagePtr first_connection;
  bool first_callback_called;
  Status first_status;
  active_page_manager_container.BindPage(
      first_connection.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&first_callback_called), &first_status));
  RunLoopUntilIdle();
  EXPECT_FALSE(first_callback_called);
  EXPECT_FALSE(on_discardable_called);
  EXPECT_EQ(fake_disk_cleanup_manager_.externally_used_count, 1);

  PagePtr second_connection;
  PagePtr third_connection;
  bool second_callback_called;
  bool third_callback_called;
  Status second_status;
  Status third_status;
  active_page_manager_container.BindPage(
      second_connection.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&second_callback_called), &second_status));
  active_page_manager_container.BindPage(
      third_connection.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&third_callback_called), &third_status));
  RunLoopUntilIdle();
  EXPECT_FALSE(first_callback_called);
  EXPECT_FALSE(second_callback_called);
  EXPECT_FALSE(third_callback_called);
  EXPECT_FALSE(on_discardable_called);
  EXPECT_EQ(fake_disk_cleanup_manager_.externally_used_count, 1);

  active_page_manager_container.SetActivePageManager(Status::OK, std::move(active_page_manager_));
  EXPECT_TRUE(first_callback_called);
  EXPECT_EQ(first_status, Status::OK);
  EXPECT_TRUE(second_callback_called);
  EXPECT_EQ(second_status, Status::OK);
  EXPECT_TRUE(third_callback_called);
  EXPECT_EQ(third_status, Status::OK);
  EXPECT_EQ(fake_disk_cleanup_manager_.externally_used_count, 1);
  EXPECT_FALSE(on_discardable_called);

  first_connection.Unbind();
  RunLoopUntilIdle();
  EXPECT_EQ(fake_disk_cleanup_manager_.externally_unused_count, 0);

  second_connection.Unbind();
  third_connection.Unbind();
  RunLoopUntilIdle();
  EXPECT_EQ(fake_disk_cleanup_manager_.externally_unused_count, 1);
  EXPECT_TRUE(on_discardable_called);
}

TEST_F(ActivePageManagerContainerTest, MultipleExternalRequestsAndMultipleInternalRequests) {
  auto bit_generator = environment_.random()->NewBitGenerator<size_t>();
  const size_t external_request_count = std::uniform_int_distribution(2u, 10u)(bit_generator);
  const size_t internal_request_count = std::uniform_int_distribution(2u, 10u)(bit_generator);
  std::vector<PagePtr> external_requests;
  std::vector<Status> external_request_statuses;
  std::vector<Status> internal_request_statuses;
  std::vector<std::unique_ptr<ExpiringToken>> internal_request_expiring_tokens;
  std::vector<ActivePageManager*> internal_request_active_page_managers;
  bool on_discardable_called;

  ActivePageManagerContainer active_page_manager_container(
      &environment_, kLedgerName.ToString(), page_id_,
      std::vector<PageUsageListener*>{&fake_disk_cleanup_manager_});
  active_page_manager_container.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  size_t requested_internal_requests = 0;
  size_t live_external_requests = 0;
  size_t live_internal_requests = 0;
  while (external_requests.size() < external_request_count ||
         requested_internal_requests < internal_request_count) {
    if (requested_internal_requests == internal_request_count ||
        (external_requests.size() < external_request_count &&
         std::uniform_int_distribution(0u, 1u)(bit_generator))) {
      PagePtr& external_request = external_requests.emplace_back();
      active_page_manager_container.BindPage(external_request.NewRequest(), [&](Status status) {
        external_request_statuses.push_back(status);
        live_external_requests++;
      });
    } else {
      active_page_manager_container.NewInternalRequest(
          [&](Status status, ExpiringToken token, ActivePageManager* active_page_manager) {
            internal_request_statuses.push_back(status);
            std::unique_ptr<ExpiringToken> expiring_token_ptr =
                std::make_unique<ExpiringToken>(std::move(token));
            internal_request_expiring_tokens.emplace_back(std::move(expiring_token_ptr));
            internal_request_active_page_managers.push_back(active_page_manager);
            live_internal_requests++;
          });
      requested_internal_requests++;
    }
    if (std::uniform_int_distribution(0u, 30u)(bit_generator) == 0) {
      RunLoopUntilIdle();
      if (!external_requests.empty()) {
        EXPECT_EQ(fake_disk_cleanup_manager_.externally_used_count, 1);
      }
    }
  }
  EXPECT_EQ(live_internal_requests, 0u);
  EXPECT_EQ(live_external_requests, 0u);
  EXPECT_EQ(fake_disk_cleanup_manager_.externally_used_count, 1);
  EXPECT_EQ(fake_disk_cleanup_manager_.internally_used_count, 0);
  EXPECT_FALSE(on_discardable_called);
  RunLoopUntilIdle();
  EXPECT_EQ(live_internal_requests, 0u);
  EXPECT_EQ(live_external_requests, 0u);
  EXPECT_EQ(fake_disk_cleanup_manager_.externally_used_count, 1);
  EXPECT_EQ(fake_disk_cleanup_manager_.internally_used_count, 0);
  EXPECT_FALSE(on_discardable_called);

  ActivePageManager* memoized_active_page_manager_raw_pointer = active_page_manager_.get();
  active_page_manager_container.SetActivePageManager(Status::OK, std::move(active_page_manager_));
  EXPECT_EQ(live_external_requests, external_request_count);
  EXPECT_EQ(external_request_statuses.size(), external_request_count);
  for (const Status& external_status : external_request_statuses) {
    EXPECT_EQ(external_status, Status::OK);
  }
  EXPECT_EQ(live_internal_requests, internal_request_count);
  EXPECT_EQ(internal_request_statuses.size(), internal_request_count);
  for (const Status& internal_status : internal_request_statuses) {
    EXPECT_EQ(internal_status, Status::OK);
  }
  EXPECT_EQ(internal_request_expiring_tokens.size(), internal_request_count);
  EXPECT_EQ(internal_request_active_page_managers.size(), internal_request_count);
  for (const ActivePageManager* internal_request_active_page_manager :
       internal_request_active_page_managers) {
    EXPECT_EQ(internal_request_active_page_manager, memoized_active_page_manager_raw_pointer);
  }
  EXPECT_EQ(fake_disk_cleanup_manager_.internally_used_count, 1);

  // We'll be closing both the internal and external requests in an order randomized relative to the
  // order in which they were created.
  std::shuffle(external_requests.begin(), external_requests.end(), bit_generator);
  std::shuffle(internal_request_expiring_tokens.begin(), internal_request_expiring_tokens.end(),
               bit_generator);
  while (true) {
    if (internal_request_expiring_tokens.empty() ||
        (!external_requests.empty() && std::uniform_int_distribution(0u, 1u)(bit_generator))) {
      PagePtr external_request = std::move(external_requests[external_requests.size() - 1]);
      external_requests.pop_back();
      external_request.Unbind();
    } else {
      std::unique_ptr<ExpiringToken> expiring_token =
          std::move(internal_request_expiring_tokens[internal_request_expiring_tokens.size() - 1]);
      internal_request_expiring_tokens.pop_back();
      expiring_token.reset();
    }
    if (external_requests.empty()) {
      RunLoopUntilIdle();
      EXPECT_EQ(fake_disk_cleanup_manager_.externally_unused_count, 1);
    }
    if (internal_request_expiring_tokens.empty()) {
      EXPECT_EQ(fake_disk_cleanup_manager_.internally_unused_count, 1);
    }
    if (external_requests.empty() && internal_request_expiring_tokens.empty()) {
      break;
    }
  }
  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);
}

TEST_F(ActivePageManagerContainerTest, DestroyedWhileExternalRequestsOutstanding) {
  bool on_discardable_called;

  std::unique_ptr<ActivePageManagerContainer> active_page_manager_container =
      std::make_unique<ActivePageManagerContainer>(
          &environment_, kLedgerName.ToString(), page_id_,
          std::vector<PageUsageListener*>{&fake_disk_cleanup_manager_});
  active_page_manager_container->SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  PagePtr first_connection;
  bool first_callback_called;
  Status first_status;
  active_page_manager_container->BindPage(
      first_connection.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&first_callback_called), &first_status));
  RunLoopUntilIdle();
  EXPECT_FALSE(first_callback_called);
  EXPECT_FALSE(on_discardable_called);
  EXPECT_EQ(fake_disk_cleanup_manager_.externally_used_count, 1);

  PagePtr second_connection;
  PagePtr third_connection;
  bool second_callback_called;
  bool third_callback_called;
  Status second_status;
  Status third_status;
  active_page_manager_container->BindPage(
      second_connection.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&second_callback_called), &second_status));
  active_page_manager_container->BindPage(
      third_connection.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&third_callback_called), &third_status));
  RunLoopUntilIdle();
  EXPECT_FALSE(first_callback_called);
  EXPECT_FALSE(second_callback_called);
  EXPECT_FALSE(third_callback_called);
  EXPECT_FALSE(on_discardable_called);
  EXPECT_EQ(fake_disk_cleanup_manager_.externally_used_count, 1);

  active_page_manager_container.reset();
  EXPECT_FALSE(on_discardable_called);
}

TEST_F(ActivePageManagerContainerTest, DestroyedWhileRequestsAndTokensOutstanding) {
  auto bit_generator = environment_.random()->NewBitGenerator<size_t>();
  const size_t external_request_count = std::uniform_int_distribution(2u, 10u)(bit_generator);
  const size_t internal_request_count = std::uniform_int_distribution(2u, 10u)(bit_generator);
  std::vector<PagePtr> external_requests;
  std::vector<Status> external_request_statuses;
  std::vector<Status> internal_request_statuses;
  std::vector<std::unique_ptr<ExpiringToken>> internal_request_expiring_tokens;
  std::vector<ActivePageManager*> internal_request_active_page_managers;
  bool on_discardable_called;

  std::unique_ptr<ActivePageManagerContainer> active_page_manager_container =
      std::make_unique<ActivePageManagerContainer>(
          &environment_, kLedgerName.ToString(), page_id_,
          std::vector<PageUsageListener*>{&fake_disk_cleanup_manager_});
  active_page_manager_container->SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  size_t requested_internal_requests = 0;
  size_t live_external_requests = 0;
  size_t live_internal_requests = 0;
  while (external_requests.size() < external_request_count ||
         requested_internal_requests < internal_request_count) {
    if (requested_internal_requests == internal_request_count ||
        (external_requests.size() < external_request_count &&
         std::uniform_int_distribution(0u, 1u)(bit_generator))) {
      PagePtr& external_request = external_requests.emplace_back();
      active_page_manager_container->BindPage(external_request.NewRequest(), [&](Status status) {
        external_request_statuses.push_back(status);
        live_external_requests++;
      });
    } else {
      active_page_manager_container->NewInternalRequest(
          [&](Status status, ExpiringToken token, ActivePageManager* active_page_manager) {
            internal_request_statuses.push_back(status);
            std::unique_ptr<ExpiringToken> expiring_token_ptr =
                std::make_unique<ExpiringToken>(std::move(token));
            internal_request_expiring_tokens.emplace_back(std::move(expiring_token_ptr));
            internal_request_active_page_managers.push_back(active_page_manager);
            live_internal_requests++;
          });
      requested_internal_requests++;
    }
  }
  RunLoopUntilIdle();

  active_page_manager_container.reset();
  EXPECT_FALSE(on_discardable_called);
}

TEST_F(ActivePageManagerContainerTest, DestroyedWhileCallingPageUsageListener) {
  auto bit_generator = environment_.random()->NewBitGenerator<size_t>();
  const size_t external_request_count = std::uniform_int_distribution(2u, 10u)(bit_generator);
  const size_t internal_request_count = std::uniform_int_distribution(2u, 10u)(bit_generator);
  std::vector<PagePtr> external_requests;
  std::vector<Status> external_request_statuses;
  std::vector<Status> internal_request_statuses;
  std::vector<std::unique_ptr<ExpiringToken>> internal_request_expiring_tokens;
  std::vector<ActivePageManager*> internal_request_active_page_managers;
  bool on_discardable_called;

  std::unique_ptr<ActivePageManagerContainer> active_page_manager_container =
      std::make_unique<ActivePageManagerContainer>(
          &environment_, kLedgerName.ToString(), page_id_,
          std::vector<PageUsageListener*>{&fake_disk_cleanup_manager_});
  active_page_manager_container->SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  fake_disk_cleanup_manager_.set_on_OnExternallyUnused(
      [&] { active_page_manager_container.reset(); });
  fake_disk_cleanup_manager_.set_on_OnInternallyUnused(
      [&] { active_page_manager_container.reset(); });

  size_t requested_internal_requests = 0;
  size_t live_external_requests = 0;
  size_t live_internal_requests = 0;
  while (external_requests.size() < external_request_count ||
         requested_internal_requests < internal_request_count) {
    if (requested_internal_requests == internal_request_count ||
        (external_requests.size() < external_request_count &&
         std::uniform_int_distribution(0u, 1u)(bit_generator))) {
      PagePtr& external_request = external_requests.emplace_back();
      active_page_manager_container->BindPage(external_request.NewRequest(), [&](Status status) {
        external_request_statuses.push_back(status);
        live_external_requests++;
      });
    } else {
      active_page_manager_container->NewInternalRequest(
          [&](Status status, ExpiringToken token, ActivePageManager* active_page_manager) {
            internal_request_statuses.push_back(status);
            std::unique_ptr<ExpiringToken> expiring_token_ptr =
                std::make_unique<ExpiringToken>(std::move(token));
            internal_request_expiring_tokens.emplace_back(std::move(expiring_token_ptr));
            internal_request_active_page_managers.push_back(active_page_manager);
            live_internal_requests++;
          });
      requested_internal_requests++;
    }
  }
  active_page_manager_container->SetActivePageManager(Status::OK, std::move(active_page_manager_));
  RunLoopUntilIdle();
  ASSERT_EQ(live_external_requests, external_request_count);
  ASSERT_EQ(live_internal_requests, internal_request_count);

  // We'll be closing both the internal and external requests in an order randomized relative to the
  // order in which they were created.
  std::shuffle(external_requests.begin(), external_requests.end(), bit_generator);
  std::shuffle(internal_request_expiring_tokens.begin(), internal_request_expiring_tokens.end(),
               bit_generator);
  while (true) {
    if (internal_request_expiring_tokens.empty() ||
        (!external_requests.empty() && std::uniform_int_distribution(0u, 1u)(bit_generator))) {
      PagePtr external_request = std::move(external_requests[external_requests.size() - 1]);
      external_requests.pop_back();
      external_request.Unbind();
    } else {
      std::unique_ptr<ExpiringToken> expiring_token =
          std::move(internal_request_expiring_tokens[internal_request_expiring_tokens.size() - 1]);
      internal_request_expiring_tokens.pop_back();
      expiring_token.reset();
    }
    RunLoopUntilIdle();
    if (external_requests.empty() || internal_request_expiring_tokens.empty()) {
      break;
    }
  }
  EXPECT_FALSE(on_discardable_called);
}

TEST_F(ActivePageManagerContainerTest, OnlyInternalRequestCallbacks) {
  auto bit_generator = environment_.random()->NewBitGenerator<size_t>();
  const size_t internal_request_count = std::uniform_int_distribution(2u, 10u)(bit_generator);
  std::vector<Status> internal_request_statuses;
  std::vector<ActivePageManager*> internal_request_active_page_managers;
  bool on_discardable_called;
  std::vector<bool> on_discardable_called_during_internal_request_callbacks;

  ActivePageManagerContainer active_page_manager_container =
      ActivePageManagerContainer(&environment_, kLedgerName.ToString(), page_id_,
                                 std::vector<PageUsageListener*>{&fake_disk_cleanup_manager_});
  active_page_manager_container.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  size_t requested_internal_requests = 0;
  while (requested_internal_requests < internal_request_count) {
    active_page_manager_container.NewInternalRequest(
        [&](Status status, ExpiringToken token, ActivePageManager* active_page_manager) {
          internal_request_statuses.push_back(status);
          // Note whether or not the |ActivePageManagerContainer|'s on-empty callback has been
          // called both before destroying the token...
          on_discardable_called_during_internal_request_callbacks.push_back(on_discardable_called);
          // ... and after destroying the token.
          token.call();
          on_discardable_called_during_internal_request_callbacks.push_back(on_discardable_called);
        });
    requested_internal_requests++;
  }
  RunLoopUntilIdle();
  EXPECT_THAT(on_discardable_called_during_internal_request_callbacks, IsEmpty());
  active_page_manager_container.SetActivePageManager(Status::OK, std::move(active_page_manager_));
  EXPECT_THAT(on_discardable_called_during_internal_request_callbacks,
              SizeIs(internal_request_count * 2));
  EXPECT_THAT(on_discardable_called_during_internal_request_callbacks, Each(IsFalse()));
  EXPECT_EQ(fake_disk_cleanup_manager_.externally_used_count, 0);
  EXPECT_EQ(fake_disk_cleanup_manager_.externally_unused_count, 0);
  EXPECT_EQ(fake_disk_cleanup_manager_.internally_used_count, 1);
  EXPECT_EQ(fake_disk_cleanup_manager_.internally_unused_count, 1);
  EXPECT_TRUE(on_discardable_called);
}

TEST_F(ActivePageManagerContainerTest, MultiplePageUsageListeners) {
  std::vector<FakeDiskCleanupManager> page_usage_listeners(27);
  std::vector<PageUsageListener*> page_usage_listener_pointers;
  page_usage_listener_pointers.reserve(page_usage_listeners.size());
  for (auto& page_usage_listener : page_usage_listeners) {
    page_usage_listener_pointers.push_back(&page_usage_listener);
  }
  auto bit_generator = environment_.random()->NewBitGenerator<size_t>();
  const size_t external_request_count = std::uniform_int_distribution(2u, 10u)(bit_generator);
  const size_t internal_request_count = std::uniform_int_distribution(2u, 10u)(bit_generator);
  std::vector<PagePtr> external_requests;
  std::vector<std::unique_ptr<ExpiringToken>> internal_request_expiring_tokens;
  bool on_discardable_called;
  ActivePageManagerContainer active_page_manager_container(&environment_, kLedgerName.ToString(),
                                                           page_id_, page_usage_listener_pointers);
  active_page_manager_container.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  size_t requested_internal_requests = 0;
  size_t live_external_requests = 0;
  size_t live_internal_requests = 0;
  while (external_requests.size() < external_request_count ||
         requested_internal_requests < internal_request_count) {
    if (requested_internal_requests == internal_request_count ||
        (external_requests.size() < external_request_count &&
         std::uniform_int_distribution(0u, 1u)(bit_generator))) {
      PagePtr& external_request = external_requests.emplace_back();
      active_page_manager_container.BindPage(external_request.NewRequest(),
                                             [&](Status status) { live_external_requests++; });
    } else {
      active_page_manager_container.NewInternalRequest(
          [&](Status status, ExpiringToken token, ActivePageManager* active_page_manager) {
            std::unique_ptr<ExpiringToken> expiring_token_ptr =
                std::make_unique<ExpiringToken>(std::move(token));
            internal_request_expiring_tokens.emplace_back(std::move(expiring_token_ptr));
            live_internal_requests++;
          });
      requested_internal_requests++;
    }
  }
  RunLoopUntilIdle();
  active_page_manager_container.SetActivePageManager(Status::OK, std::move(active_page_manager_));
  ASSERT_EQ(live_external_requests, external_request_count);
  ASSERT_EQ(live_internal_requests, internal_request_count);
  for (const FakeDiskCleanupManager& page_usage_listener : page_usage_listeners) {
    EXPECT_EQ(page_usage_listener.externally_used_count, 1);
    EXPECT_EQ(page_usage_listener.internally_used_count, 1);
  }

  // We'll be closing both the internal and external requests in an order randomized relative to the
  // order in which they were created.
  std::shuffle(external_requests.begin(), external_requests.end(), bit_generator);
  std::shuffle(internal_request_expiring_tokens.begin(), internal_request_expiring_tokens.end(),
               bit_generator);
  while (true) {
    RunLoopUntilIdle();
    for (const FakeDiskCleanupManager& page_usage_listener : page_usage_listeners) {
      EXPECT_EQ(page_usage_listener.externally_unused_count, external_requests.empty() ? 1 : 0);
      EXPECT_EQ(page_usage_listener.internally_unused_count,
                internal_request_expiring_tokens.empty() ? 1 : 0);
    }
    if (internal_request_expiring_tokens.empty() ||
        (!external_requests.empty() && std::uniform_int_distribution(0u, 1u)(bit_generator))) {
      PagePtr external_request = std::move(external_requests[external_requests.size() - 1]);
      external_requests.pop_back();
      external_request.Unbind();
    } else {
      std::unique_ptr<ExpiringToken> expiring_token =
          std::move(internal_request_expiring_tokens[internal_request_expiring_tokens.size() - 1]);
      internal_request_expiring_tokens.pop_back();
      expiring_token.reset();
    }
    if (external_requests.empty() && internal_request_expiring_tokens.empty()) {
      break;
    }
  }
  RunLoopUntilIdle();
  for (const FakeDiskCleanupManager& page_usage_listener : page_usage_listeners) {
    EXPECT_EQ(page_usage_listener.externally_unused_count, 1);
    EXPECT_EQ(page_usage_listener.internally_unused_count, 1);
  }
  EXPECT_TRUE(on_discardable_called);
}

TEST_F(ActivePageManagerContainerTest, DeletedDuringPageUsageListenerOnExternallyUnused) {
  FakeDiskCleanupManager first_page_usage_listener{};
  FakeDiskCleanupManager second_page_usage_listener{};
  PagePtr external_request;
  ExpiringToken internal_request_token;
  bool on_discardable_called;

  std::unique_ptr<ActivePageManagerContainer> active_page_manager_container =
      std::make_unique<ActivePageManagerContainer>(
          &environment_, kLedgerName.ToString(), page_id_,
          std::vector<PageUsageListener*>{&first_page_usage_listener, &second_page_usage_listener});
  active_page_manager_container->SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  active_page_manager_container->BindPage(external_request.NewRequest(), [&](Status status) {});
  active_page_manager_container->NewInternalRequest(
      [&](Status status, ExpiringToken token, ActivePageManager* active_page_manager) {
        internal_request_token = std::move(token);
      });
  RunLoopUntilIdle();
  active_page_manager_container->SetActivePageManager(Status::OK, std::move(active_page_manager_));
  EXPECT_EQ(first_page_usage_listener.externally_used_count, 1);
  EXPECT_EQ(second_page_usage_listener.externally_used_count, 1);
  EXPECT_EQ(first_page_usage_listener.internally_used_count, 1);
  EXPECT_EQ(second_page_usage_listener.internally_used_count, 1);
  EXPECT_FALSE(on_discardable_called);

  first_page_usage_listener.set_on_OnExternallyUnused(
      [&] { active_page_manager_container.reset(); });

  external_request.Unbind();
  RunLoopUntilIdle();

  EXPECT_EQ(first_page_usage_listener.externally_unused_count, 1);
  EXPECT_EQ(second_page_usage_listener.externally_unused_count, 1);
  EXPECT_FALSE(on_discardable_called);
}

TEST_F(ActivePageManagerContainerTest, DeletedDuringPageUsageListenerOnInternallyUnused) {
  FakeDiskCleanupManager first_page_usage_listener{};
  FakeDiskCleanupManager second_page_usage_listener{};
  PagePtr external_request;
  ExpiringToken internal_request_token;
  bool on_discardable_called;

  std::unique_ptr<ActivePageManagerContainer> active_page_manager_container =
      std::make_unique<ActivePageManagerContainer>(
          &environment_, kLedgerName.ToString(), page_id_,
          std::vector<PageUsageListener*>{&first_page_usage_listener, &second_page_usage_listener});
  active_page_manager_container->SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  active_page_manager_container->BindPage(external_request.NewRequest(), [&](Status status) {});
  active_page_manager_container->NewInternalRequest(
      [&](Status status, ExpiringToken token, ActivePageManager* active_page_manager) {
        internal_request_token = std::move(token);
      });
  RunLoopUntilIdle();
  active_page_manager_container->SetActivePageManager(Status::OK, std::move(active_page_manager_));
  EXPECT_EQ(first_page_usage_listener.externally_used_count, 1);
  EXPECT_EQ(second_page_usage_listener.externally_used_count, 1);
  EXPECT_EQ(first_page_usage_listener.internally_used_count, 1);
  EXPECT_EQ(second_page_usage_listener.internally_used_count, 1);
  EXPECT_FALSE(on_discardable_called);

  first_page_usage_listener.set_on_OnInternallyUnused(
      [&] { active_page_manager_container.reset(); });

  internal_request_token.call();

  EXPECT_EQ(first_page_usage_listener.internally_unused_count, 1);
  EXPECT_EQ(second_page_usage_listener.internally_unused_count, 1);
  EXPECT_FALSE(on_discardable_called);
}

}  // namespace
}  // namespace ledger

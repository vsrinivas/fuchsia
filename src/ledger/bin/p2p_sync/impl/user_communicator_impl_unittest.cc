// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_sync/impl/user_communicator_impl.h"

#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/encoder.h>
#include <lib/fit/function.h>
#include <lib/gtest/test_loop_fixture.h>

#include <algorithm>
#include <string>

// gtest matchers are in gmock and we cannot include the specific header file
// directly as it is private to the library.
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/p2p_provider/impl/p2p_provider_impl.h"
#include "src/ledger/bin/p2p_provider/public/user_id_provider.h"
#include "src/ledger/bin/p2p_sync/impl/page_communicator_impl.h"
#include "src/ledger/bin/storage/testing/page_storage_empty_impl.h"
#include "src/ledger/bin/testing/overnet/overnet_factory.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/coroutine/coroutine_impl.h"

namespace p2p_sync {
class PageCommunicatorImplInspectorForTest {
 public:
  static const std::set<p2p_provider::P2PClientId>& GetInterestedDevices(
      const std::unique_ptr<PageCommunicator>& page) {
    return static_cast<PageCommunicatorImpl*>(page.get())->interested_devices_;
  }
};

namespace {

// Makes a P2PClientId from a raw id.
class FakePageStorage : public storage::PageStorageEmptyImpl {
 public:
  explicit FakePageStorage(std::string page_id) : page_id_(std::move(page_id)) {}
  ~FakePageStorage() override = default;

  storage::PageId GetId() override { return page_id_; }

  void MarkSyncedToPeer(fit::function<void(ledger::Status)> callback) override {
    callback(ledger::Status::OK);
  }

 private:
  const std::string page_id_;
};

class FakeUserIdProvider : public p2p_provider::UserIdProvider {
 public:
  explicit FakeUserIdProvider(std::string user_id) : user_id_(std::move(user_id)) {}

  void GetUserId(fit::function<void(Status, std::string)> callback) override {
    callback(Status::OK, user_id_);
  };

 private:
  std::string user_id_;
};

class UserCommunicatorImplTest : public ledger::TestWithEnvironment {
 public:
  UserCommunicatorImplTest() : overnet_factory_(dispatcher()) {}
  UserCommunicatorImplTest(const UserCommunicatorImplTest&) = delete;
  UserCommunicatorImplTest& operator=(const UserCommunicatorImplTest&) = delete;

  std::unique_ptr<UserCommunicator> GetUserCommunicator(uint64_t node_id,
                                                        std::string user_name = "user") {
    fuchsia::overnet::OvernetPtr overnet;
    overnet_factory_.AddBinding(node_id, overnet.NewRequest());
    std::unique_ptr<p2p_provider::P2PProvider> provider =
        std::make_unique<p2p_provider::P2PProviderImpl>(
            std::move(overnet), std::make_unique<FakeUserIdProvider>(std::move(user_name)),
            environment_.random());
    return std::make_unique<UserCommunicatorImpl>(&environment_, std::move(provider));
  }

 protected:
  ledger::OvernetFactory overnet_factory_;
};

TEST_F(UserCommunicatorImplTest, OneHost_NoCrash) {
  std::unique_ptr<UserCommunicator> user_communicator = GetUserCommunicator(1);
  user_communicator->Start();
  std::unique_ptr<LedgerCommunicator> ledger = user_communicator->GetLedgerCommunicator("ledger1");
  FakePageStorage storage("page1");
  std::unique_ptr<PageCommunicator> page = ledger->GetPageCommunicator(&storage, &storage);
  page->Start();
  RunLoopUntilIdle();
}

TEST_F(UserCommunicatorImplTest, ThreeHosts_SamePage) {
  std::unique_ptr<UserCommunicator> user_communicator1 = GetUserCommunicator(1);
  user_communicator1->Start();
  std::unique_ptr<LedgerCommunicator> ledger1 = user_communicator1->GetLedgerCommunicator("app");
  FakePageStorage storage1("page");
  std::unique_ptr<PageCommunicator> page1 = ledger1->GetPageCommunicator(&storage1, &storage1);
  page1->Start();
  RunLoopUntilIdle();

  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page1),
              testing::SizeIs(0));

  std::unique_ptr<UserCommunicator> user_communicator2 = GetUserCommunicator(2);
  user_communicator2->Start();
  std::unique_ptr<LedgerCommunicator> ledger2 = user_communicator2->GetLedgerCommunicator("app");
  FakePageStorage storage2("page");
  std::unique_ptr<PageCommunicator> page2 = ledger2->GetPageCommunicator(&storage2, &storage2);
  page2->Start();
  RunLoopUntilIdle();

  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page1),
              testing::SizeIs(1));
  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page2),
              testing::SizeIs(1));

  std::unique_ptr<UserCommunicator> user_communicator3 = GetUserCommunicator(3);
  user_communicator3->Start();
  std::unique_ptr<LedgerCommunicator> ledger3 = user_communicator3->GetLedgerCommunicator("app");
  FakePageStorage storage3("page");
  std::unique_ptr<PageCommunicator> page3 = ledger3->GetPageCommunicator(&storage3, &storage3);
  page3->Start();
  RunLoopUntilIdle();

  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page1),
              testing::SizeIs(2));
  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page2),
              testing::SizeIs(2));
  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page3),
              testing::SizeIs(2));

  page2.reset();
  RunLoopUntilIdle();

  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page1),
              testing::SizeIs(1));
  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page3),
              testing::SizeIs(1));
}

TEST_F(UserCommunicatorImplTest, ThreeHosts_TwoPages) {
  std::unique_ptr<UserCommunicator> user_communicator1 = GetUserCommunicator(1);
  user_communicator1->Start();
  std::unique_ptr<LedgerCommunicator> ledger1 = user_communicator1->GetLedgerCommunicator("app");
  FakePageStorage storage1_1("page1");
  std::unique_ptr<PageCommunicator> page1_1 =
      ledger1->GetPageCommunicator(&storage1_1, &storage1_1);
  page1_1->Start();
  FakePageStorage storage1_2("page2");
  std::unique_ptr<PageCommunicator> page1_2 =
      ledger1->GetPageCommunicator(&storage1_2, &storage1_2);
  page1_2->Start();
  RunLoopUntilIdle();

  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page1_1),
              testing::SizeIs(0));
  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page1_2),
              testing::SizeIs(0));

  std::unique_ptr<UserCommunicator> user_communicator2 = GetUserCommunicator(2);
  user_communicator2->Start();
  std::unique_ptr<LedgerCommunicator> ledger2 = user_communicator2->GetLedgerCommunicator("app");
  FakePageStorage storage2_1("page1");
  std::unique_ptr<PageCommunicator> page2_1 =
      ledger2->GetPageCommunicator(&storage2_1, &storage2_1);
  page2_1->Start();
  RunLoopUntilIdle();

  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page1_1),
              testing::SizeIs(1));
  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page1_2),
              testing::SizeIs(0));
  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page2_1),
              testing::SizeIs(1));

  std::unique_ptr<UserCommunicator> user_communicator3 = GetUserCommunicator(3);
  user_communicator3->Start();
  std::unique_ptr<LedgerCommunicator> ledger3 = user_communicator3->GetLedgerCommunicator("app");
  FakePageStorage storage3_2("page2");
  std::unique_ptr<PageCommunicator> page3_2 =
      ledger3->GetPageCommunicator(&storage3_2, &storage3_2);
  page3_2->Start();
  RunLoopUntilIdle();

  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page1_1),
              testing::SizeIs(1));
  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page1_2),
              testing::SizeIs(1));
  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page2_1),
              testing::SizeIs(1));
  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page3_2),
              testing::SizeIs(1));

  page1_1.reset();
  RunLoopUntilIdle();

  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page1_2),
              testing::SizeIs(1));
  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page2_1),
              testing::SizeIs(0));
  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page3_2),
              testing::SizeIs(1));
}

// This test adds some delay (ie. runs the loop until idle) between the time a
// device becomes visible and the time the page we are interested in becomes
// active. This ensure we correctly connect pages that become active after the
// device is connected.
TEST_F(UserCommunicatorImplTest, ThreeHosts_WaitBeforePageIsActive) {
  std::unique_ptr<UserCommunicator> user_communicator1 = GetUserCommunicator(1);
  user_communicator1->Start();
  RunLoopUntilIdle();
  std::unique_ptr<LedgerCommunicator> ledger1 = user_communicator1->GetLedgerCommunicator("app");
  FakePageStorage storage1("page");
  std::unique_ptr<PageCommunicator> page1 = ledger1->GetPageCommunicator(&storage1, &storage1);
  page1->Start();
  RunLoopUntilIdle();

  std::unique_ptr<UserCommunicator> user_communicator2 = GetUserCommunicator(2);
  user_communicator2->Start();
  RunLoopUntilIdle();
  std::unique_ptr<LedgerCommunicator> ledger2 = user_communicator2->GetLedgerCommunicator("app");
  FakePageStorage storage2("page");
  std::unique_ptr<PageCommunicator> page2 = ledger2->GetPageCommunicator(&storage2, &storage2);
  page2->Start();
  RunLoopUntilIdle();

  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page1),
              testing::SizeIs(1));
  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page2),
              testing::SizeIs(1));

  std::unique_ptr<UserCommunicator> user_communicator3 = GetUserCommunicator(3);
  user_communicator3->Start();
  RunLoopUntilIdle();
  std::unique_ptr<LedgerCommunicator> ledger3 = user_communicator3->GetLedgerCommunicator("app");
  FakePageStorage storage3("page");
  std::unique_ptr<PageCommunicator> page3 = ledger3->GetPageCommunicator(&storage3, &storage3);
  page3->Start();
  RunLoopUntilIdle();

  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page1),
              testing::SizeIs(2));
  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page2),
              testing::SizeIs(2));
  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page2),
              testing::SizeIs(2));

  page2.reset();
  RunLoopUntilIdle();

  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page1),
              testing::SizeIs(1));
  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page3),
              testing::SizeIs(1));
}

}  // namespace
}  // namespace p2p_sync

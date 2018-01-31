// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/p2p_sync/impl/user_communicator_impl.h"

#include <algorithm>
#include <string>

#include "garnet/lib/gtest/test_with_message_loop.h"
// gtest matchers are in gmock and we cannot include the specific header file
// directly as it is private to the library.
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/p2p_provider/impl/p2p_provider_impl.h"
#include "peridot/bin/ledger/p2p_provider/public/user_id_provider.h"
#include "peridot/bin/ledger/p2p_sync/impl/page_communicator_impl.h"
#include "peridot/bin/ledger/testing/netconnector/netconnector_factory.h"

namespace p2p_sync {
class PageCommunicatorImplInspectorForTest {
 public:
  static const std::set<std::string, convert::StringViewComparator>&
  GetInterestedDevices(const std::unique_ptr<PageCommunicator>& page) {
    return reinterpret_cast<PageCommunicatorImpl*>(page.get())
        ->interested_devices_;
  }
};

namespace {

class FakeUserIdProvider : public p2p_provider::UserIdProvider {
 public:
  explicit FakeUserIdProvider(std::string user_id)
      : user_id_(std::move(user_id)) {}

  void GetUserId(std::function<void(Status, std::string)> callback) override {
    callback(Status::OK, user_id_);
  };

 private:
  std::string user_id_;
};

class UserCommunicatorImplTest : public gtest::TestWithMessageLoop {
 public:
  UserCommunicatorImplTest() {}
  ~UserCommunicatorImplTest() override {}

  std::unique_ptr<UserCommunicator> GetUserCommunicator(
      std::string host_name,
      std::string user_name = "user") {
    netconnector::NetConnectorPtr netconnector;
    net_connector_factory_.AddBinding(host_name, netconnector.NewRequest());
    std::unique_ptr<p2p_provider::P2PProvider> provider =
        std::make_unique<p2p_provider::P2PProviderImpl>(
            std::move(host_name), std::move(netconnector),
            std::make_unique<FakeUserIdProvider>(std::move(user_name)));
    return std::make_unique<UserCommunicatorImpl>(std::move(provider));
  }

 protected:
  void SetUp() override { ::testing::Test::SetUp(); }

  ledger::NetConnectorFactory net_connector_factory_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(UserCommunicatorImplTest);
};

TEST_F(UserCommunicatorImplTest, OneHost_NoCrash) {
  std::unique_ptr<UserCommunicator> user_communicator =
      GetUserCommunicator("host1");
  user_communicator->Start();
  std::unique_ptr<LedgerCommunicator> ledger =
      user_communicator->GetLedgerCommunicator("ledger1");
  std::unique_ptr<PageCommunicator> page = ledger->GetPageCommunicator("page1");
  page->Start();
  RunLoopUntilIdle();
}

TEST_F(UserCommunicatorImplTest, ThreeHosts_SamePage) {
  std::unique_ptr<UserCommunicator> user_communicator1 =
      GetUserCommunicator("host1");
  user_communicator1->Start();
  std::unique_ptr<LedgerCommunicator> ledger1 =
      user_communicator1->GetLedgerCommunicator("app");
  std::unique_ptr<PageCommunicator> page1 =
      ledger1->GetPageCommunicator("page");
  page1->Start();
  RunLoopUntilIdle();

  std::unique_ptr<UserCommunicator> user_communicator2 =
      GetUserCommunicator("host2");
  user_communicator2->Start();
  std::unique_ptr<LedgerCommunicator> ledger2 =
      user_communicator2->GetLedgerCommunicator("app");
  std::unique_ptr<PageCommunicator> page2 =
      ledger2->GetPageCommunicator("page");
  page2->Start();
  RunLoopUntilIdle();

  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page1),
              testing::UnorderedElementsAre("host2"));
  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page2),
              testing::UnorderedElementsAre("host1"));

  std::unique_ptr<UserCommunicator> user_communicator3 =
      GetUserCommunicator("host3");
  user_communicator3->Start();
  std::unique_ptr<LedgerCommunicator> ledger3 =
      user_communicator3->GetLedgerCommunicator("app");
  std::unique_ptr<PageCommunicator> page3 =
      ledger3->GetPageCommunicator("page");
  page3->Start();
  RunLoopUntilIdle();

  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page1),
              testing::UnorderedElementsAre("host2", "host3"));
  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page2),
              testing::UnorderedElementsAre("host1", "host3"));
  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page3),
              testing::UnorderedElementsAre("host1", "host2"));

  page2.reset();
  RunLoopUntilIdle();
  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page1),
              testing::UnorderedElementsAre("host3"));
  EXPECT_THAT(PageCommunicatorImplInspectorForTest::GetInterestedDevices(page3),
              testing::UnorderedElementsAre("host1"));
}

TEST_F(UserCommunicatorImplTest, ThreeHosts_TwoPages) {
  std::unique_ptr<UserCommunicator> user_communicator1 =
      GetUserCommunicator("host1");
  user_communicator1->Start();
  std::unique_ptr<LedgerCommunicator> ledger1 =
      user_communicator1->GetLedgerCommunicator("app");
  std::unique_ptr<PageCommunicator> page1_1 =
      ledger1->GetPageCommunicator("page1");
  page1_1->Start();
  std::unique_ptr<PageCommunicator> page1_2 =
      ledger1->GetPageCommunicator("page2");
  page1_2->Start();
  RunLoopUntilIdle();

  std::unique_ptr<UserCommunicator> user_communicator2 =
      GetUserCommunicator("host2");
  user_communicator2->Start();
  std::unique_ptr<LedgerCommunicator> ledger2 =
      user_communicator2->GetLedgerCommunicator("app");
  std::unique_ptr<PageCommunicator> page2_1 =
      ledger2->GetPageCommunicator("page1");
  page2_1->Start();
  RunLoopUntilIdle();

  std::unique_ptr<UserCommunicator> user_communicator3 =
      GetUserCommunicator("host3");
  user_communicator3->Start();
  std::unique_ptr<LedgerCommunicator> ledger3 =
      user_communicator3->GetLedgerCommunicator("app");
  std::unique_ptr<PageCommunicator> page3_2 =
      ledger3->GetPageCommunicator("page2");
  page3_2->Start();
  RunLoopUntilIdle();

  EXPECT_THAT(
      PageCommunicatorImplInspectorForTest::GetInterestedDevices(page1_1),
      testing::UnorderedElementsAre("host2"));
  EXPECT_THAT(
      PageCommunicatorImplInspectorForTest::GetInterestedDevices(page1_2),
      testing::UnorderedElementsAre("host3"));
  EXPECT_THAT(
      PageCommunicatorImplInspectorForTest::GetInterestedDevices(page2_1),
      testing::UnorderedElementsAre("host1"));
  EXPECT_THAT(
      PageCommunicatorImplInspectorForTest::GetInterestedDevices(page3_2),
      testing::UnorderedElementsAre("host1"));
}

}  // namespace
}  // namespace p2p_sync

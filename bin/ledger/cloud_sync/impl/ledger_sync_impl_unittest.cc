// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/impl/ledger_sync_impl.h"

#include "apps/ledger/src/callback/capture.h"
#include "apps/ledger/src/network/fake_network_service.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/concatenate.h"
#include "lib/mtl/tasks/message_loop.h"

namespace cloud_sync {

class LedgerSyncImplTest : public test::TestWithMessageLoop {
 public:
  LedgerSyncImplTest()
      : network_service_(mtl::MessageLoop::GetCurrent()->task_runner()),
        environment_(message_loop_.task_runner(), &network_service_),
        user_config_({true, "server_id", "test_user", dir_.path()}),
        ledger_sync_(&environment_, &user_config_, "test_id") {}

  // ::testing::Test:
  void SetUp() override { ::testing::Test::SetUp(); }

  void TearDown() override { ::testing::Test::TearDown(); }

 protected:
  files::ScopedTempDir dir_;
  ledger::FakeNetworkService network_service_;
  ledger::Environment environment_;
  UserConfig user_config_;
  LedgerSyncImpl ledger_sync_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(LedgerSyncImplTest);
};

TEST_F(LedgerSyncImplTest, RemoteContainsRequestUrl) {
  network_service_.SetStringResponse("null", 200);
  RemoteResponse response;
  ledger_sync_.RemoteContains(
      "page_id",
      callback::Capture([this] { message_loop_.PostQuitTask(); }, &response));
  RunLoopWithTimeout();
  const std::string expected_url = ftl::Concatenate(
      {"https://server_id.firebaseio.com/__default__V/test_userV/",
       storage::kSerializationVersion, "/test_idV/page_idV.json?shallow=true"});
  EXPECT_EQ(expected_url, network_service_.GetRequest()->url);
}

TEST_F(LedgerSyncImplTest, RemoteContainsWhenAnswerIsYes) {
  network_service_.SetStringResponse("{\"commits\":true,\"objects\":true}",
                                     200);
  RemoteResponse response;
  ledger_sync_.RemoteContains(
      "page_id",
      callback::Capture([this] { message_loop_.PostQuitTask(); }, &response));
  RunLoopWithTimeout();

  EXPECT_EQ(RemoteResponse::FOUND, response);
}

TEST_F(LedgerSyncImplTest, RemoteContainsWhenAnswerIsNo) {
  network_service_.SetStringResponse("null", 200);
  RemoteResponse response;
  ledger_sync_.RemoteContains(
      "page_id",
      callback::Capture([this] { message_loop_.PostQuitTask(); }, &response));
  RunLoopWithTimeout();

  EXPECT_EQ(RemoteResponse::NOT_FOUND, response);
}

TEST_F(LedgerSyncImplTest, RemoteContainsWhenServerReturnsError) {
  network_service_.SetStringResponse("null", 500);
  RemoteResponse response;
  ledger_sync_.RemoteContains(
      "page_id",
      callback::Capture([this] { message_loop_.PostQuitTask(); }, &response));
  RunLoopWithTimeout();

  EXPECT_EQ(RemoteResponse::SERVER_ERROR, response);
}

}  // namespace cloud_sync

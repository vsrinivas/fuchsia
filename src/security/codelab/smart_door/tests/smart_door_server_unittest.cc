// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "src/security/codelab/smart_door/src/smart_door_server_app.h"

namespace smart_door {
namespace test {

using fuchsia::security::codelabsmartdoor::Access_AddHomeMember_Result;
using fuchsia::security::codelabsmartdoor::Access_Open_Result;
using fuchsia::security::codelabsmartdoor::AccessPtr;
using fuchsia::security::codelabsmartdoor::Error;
using fuchsia::security::codelabsmartdoor::UserGroup;

class SmartDoorServerAppForTest : public SmartDoorServerApp {
 public:
  SmartDoorServerAppForTest(std::unique_ptr<sys::ComponentContext> context,
                            std::shared_ptr<SmartDoorMemoryClient> client)
      : SmartDoorServerApp(std::move(context), std::move(client)) {}
};

class SmartDoorMemoryClientFake : public SmartDoorMemoryClient {
 public:
  bool write(const std::vector<uint8_t>& buffer) override {
    storage = buffer;
    return true;
  }
  bool read(std::vector<uint8_t>& buffer) override {
    buffer = storage;
    return true;
  }
  ~SmartDoorMemoryClientFake() {}

 private:
  std::vector<uint8_t> storage;
};

class SmartDoorServerTest : public gtest::TestLoopFixture {
 protected:
  void SetUp() override {
    TestLoopFixture::SetUp();
    memory_client_.reset(new SmartDoorMemoryClientFake());
    server_.reset(new SmartDoorServerAppForTest(provider_.TakeContext(), memory_client_));
  }

  void TearDown() override {
    server_.reset();
    TestLoopFixture::TearDown();
  }

  AccessPtr getSmartDoor() {
    AccessPtr smart_door;
    provider_.ConnectToPublicService(smart_door.NewRequest());
    return smart_door;
  }

  std::shared_ptr<SmartDoorMemoryClient> getStorageClient() { return memory_client_; }

 private:
  std::unique_ptr<SmartDoorServerAppForTest> server_;
  std::shared_ptr<SmartDoorMemoryClient> memory_client_;
  sys::testing::ComponentContextProvider provider_;
};

TEST_F(SmartDoorServerTest, TestAddHomeMemberOpenNormal) {
  AccessPtr smart_door = getSmartDoor();
  Access_AddHomeMember_Result result;
  std::vector<uint8_t> passphrase(16, 1u);
  smart_door->AddHomeMember("testuser", passphrase,
                            [&](Access_AddHomeMember_Result s) { result = std::move(s); });
  RunLoopUntilIdle();
  EXPECT_TRUE(result.is_response());

  Access_Open_Result open_result;
  smart_door->Open("testuser", passphrase,
                   [&](Access_Open_Result s) { open_result = std::move(s); });
  RunLoopUntilIdle();
  EXPECT_TRUE(open_result.is_response());
  EXPECT_EQ(open_result.response().group, UserGroup::REGULAR);
}

TEST_F(SmartDoorServerTest, TestAddHomeMemberOpenWrongPassphrase) {
  AccessPtr smart_door = getSmartDoor();
  Access_AddHomeMember_Result result;
  std::vector<uint8_t> passphrase(16, 1u);
  smart_door->AddHomeMember("testuser", passphrase,
                            [&](Access_AddHomeMember_Result s) { result = std::move(s); });
  RunLoopUntilIdle();
  EXPECT_TRUE(result.is_response());

  Access_Open_Result open_result;
  passphrase.push_back(1);
  smart_door->Open("testuser", passphrase,
                   [&](Access_Open_Result s) { open_result = std::move(s); });
  RunLoopUntilIdle();
  EXPECT_TRUE(open_result.is_err());
  EXPECT_EQ(open_result.err(), Error::WRONG_PASSPHRASE);
}

TEST_F(SmartDoorServerTest, TestAddHomeMemberExists) {
  AccessPtr smart_door = getSmartDoor();
  Access_AddHomeMember_Result result;
  std::vector<uint8_t> passphrase(16, 1u);
  smart_door->AddHomeMember("testuser", passphrase,
                            [&](Access_AddHomeMember_Result s) { result = std::move(s); });
  RunLoopUntilIdle();
  EXPECT_TRUE(result.is_response());

  smart_door->AddHomeMember("testuser", passphrase,
                            [&](Access_AddHomeMember_Result s) { result = std::move(s); });
  RunLoopUntilIdle();
  EXPECT_TRUE(result.is_err());
  EXPECT_EQ(result.err(), Error::USER_EXISTS);
}

TEST_F(SmartDoorServerTest, TestOpenAdmin) {
  AccessPtr smart_door = getSmartDoor();

  Access_Open_Result open_result;
  std::vector<uint8_t> passphrase;
  const char* test_admin_passphrase = "password";
  passphrase.insert(passphrase.end(), test_admin_passphrase,
                    test_admin_passphrase + strlen(test_admin_passphrase));
  smart_door->Open("", passphrase, [&](Access_Open_Result s) { open_result = std::move(s); });
  RunLoopUntilIdle();
  EXPECT_TRUE(open_result.is_response());
  EXPECT_EQ(open_result.response().group, UserGroup::ADMIN);
}

TEST_F(SmartDoorServerTest, TestOpenAdminWrongPassphrase) {
  AccessPtr smart_door = getSmartDoor();

  Access_Open_Result open_result;
  std::vector<uint8_t> passphrase;
  const char* test_admin_passphrase = "passphrase1";
  passphrase.insert(passphrase.end(), test_admin_passphrase,
                    test_admin_passphrase + strlen(test_admin_passphrase));
  smart_door->Open("", passphrase, [&](Access_Open_Result s) { open_result = std::move(s); });
  RunLoopUntilIdle();
  EXPECT_TRUE(open_result.is_err());
  EXPECT_EQ(open_result.err(), Error::WRONG_PASSPHRASE);
}

TEST_F(SmartDoorServerTest, TestSetDebugFlag) {
  AccessPtr smart_door = getSmartDoor();
  bool success = false;
  smart_door->SetDebugFlag(true, [&] { success = true; });
  RunLoopUntilIdle();
  EXPECT_EQ(success, true);
  success = false;
  smart_door->SetDebugFlag(false, [&] { success = true; });
  RunLoopUntilIdle();
  EXPECT_EQ(success, true);
}

}  // namespace test
}  // namespace smart_door

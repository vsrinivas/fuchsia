// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fuchsia/security/codelabsmartdoor/cpp/fidl.h>

#include "lib/sys/cpp/component_context.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace smart_door {
namespace test {

using fuchsia::security::codelabsmartdoor::Access_AddHomeMember_Result;
using fuchsia::security::codelabsmartdoor::Access_Open_Result;
using fuchsia::security::codelabsmartdoor::AccessResetSyncPtr;
using fuchsia::security::codelabsmartdoor::AccessSyncPtr;
using fuchsia::security::codelabsmartdoor::Error;
using fuchsia::security::codelabsmartdoor::UserGroup;

class SmartDoorFunctionalTest : public gtest::TestLoopFixture {
 protected:
  void SetUp() override {
    TestLoopFixture::SetUp();
    auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
    context->svc()->Connect(smart_door_.NewRequest());

    // Reset the component to its initial state.
    AccessResetSyncPtr smart_door_reset;
    context->svc()->Connect(smart_door_reset.NewRequest());
    smart_door_reset->Reset();
  }

  void TearDown() override { TestLoopFixture::TearDown(); }
  AccessSyncPtr smart_door_;
};

TEST_F(SmartDoorFunctionalTest, TestAddHomeMemberOpen) {
  Access_AddHomeMember_Result adduser_result;
  std::vector<uint8_t> password(16, 1u);
  // Create a test user.
  EXPECT_EQ(ZX_OK, smart_door_->AddHomeMember("testuser", password, &adduser_result));
  EXPECT_TRUE(adduser_result.is_response());

  // Try opening using the correct password.
  Access_Open_Result open_result;
  EXPECT_EQ(ZX_OK, smart_door_->Open("testuser", password, &open_result));
  EXPECT_TRUE(open_result.is_response());
  EXPECT_EQ(open_result.response().group, UserGroup::REGULAR);

  // Try opening the door using wrong password.
  password.push_back(1);
  EXPECT_EQ(ZX_OK, smart_door_->Open("testuser", password, &open_result));
  EXPECT_TRUE(open_result.is_err());
  EXPECT_EQ(open_result.err(), Error::WRONG_PASSPHRASE);

  // Try adding the same user again.
  EXPECT_EQ(ZX_OK, smart_door_->AddHomeMember("testuser", password, &adduser_result));
  EXPECT_TRUE(adduser_result.is_err());
  EXPECT_EQ(adduser_result.err(), Error::USER_EXISTS);
}

}  // namespace test
}  // namespace smart_door

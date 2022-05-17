// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/real_loop_fixture.h>

#include <gtest/gtest.h>

#include "fuchsia/examples/diagnostics/cpp/fidl.h"
#include "lib/async-loop/cpp/loop.h"
#include "lib/async-loop/loop.h"
#include "lib/async/dispatcher.h"
#include "lib/fidl/cpp/interface_ptr.h"
#include "profile_store.h"

class ProfileStoreTests : public gtest::RealLoopFixture {
 public:
  ProfileStoreTests() : store_(dispatcher()) {}

  ~ProfileStoreTests() override {}

  fidl::InterfacePtr<fuchsia::examples::diagnostics::ProfileStore> GetHandler() {
    fidl::InterfacePtr<fuchsia::examples::diagnostics::ProfileStore> ptr;
    store_.AddBinding(ptr.NewRequest(dispatcher()));
    return ptr;
  }

 private:
  ProfileStore store_;
};

// Disabled because this reliably fails.
TEST_F(ProfileStoreTests, DISABLED_Delete) {
  auto store_client = GetHandler();

  // Create a profile and set some details.
  fidl::InterfacePtr<fuchsia::examples::diagnostics::Profile> profile_client;
  store_client->CreateOrOpen("my_key", profile_client.NewRequest(dispatcher()));
  profile_client->SetName("my_name");
  profile_client->AddBalance(10);
  // Verify details were set.
  std::string set_name = "";
  int64_t set_balance = -1;
  profile_client->GetName([&](std::string n) { set_name = std::move(n); });
  profile_client->GetBalance([&](int64_t b) { set_balance = b; });
  RunLoopUntil([&]() { return !set_name.empty() && set_balance != -1; });
  EXPECT_EQ(set_name, "my_name");
  EXPECT_EQ(set_balance, 10);

  // Delete profile.
  bool delete_done = false;
  store_client->Delete("my_key", [&](bool successful) {
    EXPECT_TRUE(successful);
    delete_done = true;
  });
  RunLoopUntil([&]() { return delete_done; });
  profile_client.Unbind();
  RunLoopUntilIdle();

  // Check profile has been erased. A new profile with the same key should be empty.
  store_client->CreateOrOpen("my_key", profile_client.NewRequest(dispatcher()));
  set_name = "placeholder";
  set_balance = -1;
  profile_client->GetName([&](std::string n) { set_name = std::move(n); });
  profile_client->GetBalance([&](int64_t b) { set_balance = b; });
  RunLoopUntil([&]() { return !set_name.empty() && set_balance != -1; });
  EXPECT_EQ(set_name, "");
  EXPECT_EQ(set_balance, 0);
}

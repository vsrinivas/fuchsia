// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/service_directory.h>
#include <stdio.h>
#include <zircon/errors.h>

#include <cstdint>
#include <memory>

#include <gtest/gtest.h>
#include <src/lib/testing/loop_fixture/real_loop_fixture.h>

#include "fuchsia/examples/diagnostics/cpp/fidl.h"

const char* KEY = "demo_key";

class ProfileStoreTest : public gtest::RealLoopFixture {
 public:
  ProfileStoreTest() : svc_(sys::ServiceDirectory::CreateFromNamespace()) {}
  fuchsia::examples::diagnostics::ProfileStorePtr GetStorePtr() {
    fuchsia::examples::diagnostics::ProfileStorePtr store;
    svc_->Connect(store.NewRequest());
    return store;
  }

 private:
  std::shared_ptr<sys::ServiceDirectory> svc_;
};

TEST_F(ProfileStoreTest, Create) {
  auto store = GetStorePtr();
  fuchsia::examples::diagnostics::ProfilePtr profile;
  store->CreateOrOpen(KEY, profile.NewRequest());
  profile->SetName("my_demo_name");
  bool name_done = false, balance_done = false;
  profile->GetName([&](const std::string& name) {
    name_done = true;
    ASSERT_EQ(name, "my_demo_name");
  });

  profile->GetBalance([&](int64_t balance) {
    balance_done = true;
    ASSERT_EQ(balance, 0);  // initial balance is zero
  });

  RunLoopUntil([&]() { return name_done && balance_done; });
}

TEST_F(ProfileStoreTest, ProfileNotCreated) {
  auto store = GetStorePtr();
  fuchsia::examples::diagnostics::ProfileSyncPtr profile;
  store->Open(KEY, profile.NewRequest());
  profile->SetName("my_demo_name");
  std::string name;
  zx_status_t status = profile->GetName(&name);

  // as no profile was created we should get an error
  ASSERT_EQ(status, ZX_ERR_PEER_CLOSED);
}

TEST_F(ProfileStoreTest, Balance) {
  auto store = GetStorePtr();
  fuchsia::examples::diagnostics::ProfileSyncPtr profile;
  store->CreateOrOpen(KEY, profile.NewRequest());
  profile->AddBalance(20);
  int64_t balance;
  ASSERT_EQ(profile->GetBalance(&balance), ZX_OK);
  ASSERT_EQ(balance, 20);

  bool withdraw;
  profile->WithdrawBalance(30, &withdraw);
  // should fail as we are trying to withdraw balance more than available amount.
  ASSERT_FALSE(withdraw);

  profile->WithdrawBalance(15, &withdraw);
  ASSERT_TRUE(withdraw);
  ASSERT_EQ(profile->GetBalance(&balance), ZX_OK);
  ASSERT_EQ(balance, 5);

  profile->WithdrawBalance(5, &withdraw);
  ASSERT_TRUE(withdraw);
  ASSERT_EQ(profile->GetBalance(&balance), ZX_OK);
  ASSERT_EQ(balance, 0);

  // add new balance after withdraw
  profile->AddBalance(50);
  ASSERT_EQ(profile->GetBalance(&balance), ZX_OK);
  ASSERT_EQ(balance, 50);
}

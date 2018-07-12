// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/app/cloud_provider_impl.h"

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/callback/set_when_called.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fxl/macros.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/network_wrapper/fake_network_wrapper.h>

#include "peridot/lib/firebase_auth/testing/fake_token_provider.h"
#include "peridot/lib/firebase_auth/testing/test_firebase_auth.h"

namespace cloud_provider_firebase {

namespace {

Config GetFirebaseConfig() {
  Config config;
  config.server_id = "abc";
  config.api_key = "xyz";
  return config;
}

std::unique_ptr<firebase_auth::FirebaseAuth> InitFirebaseAuth(
    async_dispatcher_t* dispatcher, firebase_auth::TestFirebaseAuth** ptr) {
  auto firebase_auth = std::make_unique<firebase_auth::TestFirebaseAuth>(dispatcher);
  *ptr = firebase_auth.get();
  return firebase_auth;
}

}  // namespace

class CloudProviderImplTest : public gtest::TestLoopFixture {
 public:
  CloudProviderImplTest()
      : network_wrapper_(dispatcher()),
        cloud_provider_impl_(&network_wrapper_, "user_id", GetFirebaseConfig(),
                             InitFirebaseAuth(dispatcher(), &firebase_auth_),
                             cloud_provider_.NewRequest()) {}
  ~CloudProviderImplTest() override {}

 protected:
  firebase_auth::TestFirebaseAuth* firebase_auth_ = nullptr;

  network_wrapper::FakeNetworkWrapper network_wrapper_;
  cloud_provider::CloudProviderPtr cloud_provider_;
  CloudProviderImpl cloud_provider_impl_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(CloudProviderImplTest);
};

TEST_F(CloudProviderImplTest, EmptyWhenClientDisconnected) {
  bool on_empty_called = false;
  cloud_provider_impl_.set_on_empty(callback::SetWhenCalled(&on_empty_called));
  cloud_provider_.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_empty_called);
}

TEST_F(CloudProviderImplTest, EmptyWhenFirebaseAuthDisconnected) {
  bool on_empty_called = false;
  cloud_provider_impl_.set_on_empty(callback::SetWhenCalled(&on_empty_called));
  firebase_auth_->TriggerConnectionErrorHandler();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_empty_called);
}

}  // namespace cloud_provider_firebase

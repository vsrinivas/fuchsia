// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/app/cloud_provider_impl.h"

#include <fuchsia/cpp/cloud_provider.h>
#include "garnet/lib/gtest/test_with_message_loop.h"
#include "garnet/lib/network_wrapper/fake_network_wrapper.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"
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
    async_t* async,
    firebase_auth::TestFirebaseAuth** ptr) {
  auto firebase_auth =
      std::make_unique<firebase_auth::TestFirebaseAuth>(async);
  *ptr = firebase_auth.get();
  return firebase_auth;
}

}  // namespace

class CloudProviderImplTest : public gtest::TestWithMessageLoop {
 public:
  CloudProviderImplTest()
      : network_wrapper_(message_loop_.async()),
        cloud_provider_impl_(
            &network_wrapper_,
            "user_id",
            GetFirebaseConfig(),
            InitFirebaseAuth(message_loop_.async(), &firebase_auth_),
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
  cloud_provider_impl_.set_on_empty([this, &on_empty_called] {
    on_empty_called = true;
    message_loop_.PostQuitTask();
  });
  cloud_provider_.Unbind();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(on_empty_called);
}

TEST_F(CloudProviderImplTest, EmptyWhenFirebaseAuthDisconnected) {
  bool on_empty_called = false;
  cloud_provider_impl_.set_on_empty([this, &on_empty_called] {
    on_empty_called = true;
    message_loop_.PostQuitTask();
  });
  firebase_auth_->TriggerConnectionErrorHandler();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(on_empty_called);
}

}  // namespace cloud_provider_firebase

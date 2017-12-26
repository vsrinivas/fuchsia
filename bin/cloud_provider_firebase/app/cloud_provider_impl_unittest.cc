// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/app/cloud_provider_impl.h"

#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"
#include "peridot/lib/firebase_auth/test/fake_token_provider.h"
#include "peridot/lib/firebase_auth/test/test_firebase_auth.h"
#include "peridot/lib/gtest/test_with_message_loop.h"
#include "peridot/lib/network/fake_network_service.h"

namespace cloud_provider_firebase {

namespace {
ConfigPtr GetFirebaseConfig() {
  auto config = Config::New();
  config->server_id = "abc";
  config->api_key = "xyz";
  return config;
}

std::unique_ptr<firebase_auth::FirebaseAuth> InitFirebaseAuth(
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    firebase_auth::test::TestFirebaseAuth** ptr) {
  auto firebase_auth = std::make_unique<firebase_auth::test::TestFirebaseAuth>(
      std::move(task_runner));
  *ptr = firebase_auth.get();
  return firebase_auth;
}

}  // namespace

class CloudProviderImplTest : public gtest::TestWithMessageLoop {
 public:
  CloudProviderImplTest()
      : network_service_(message_loop_.task_runner()),
        cloud_provider_impl_(
            message_loop_.task_runner(),
            &network_service_,
            "user_id",
            GetFirebaseConfig(),
            InitFirebaseAuth(message_loop_.task_runner(), &firebase_auth_),
            cloud_provider_.NewRequest()) {}
  ~CloudProviderImplTest() override {}

 protected:
  firebase_auth::test::TestFirebaseAuth* firebase_auth_ = nullptr;

  ledger::FakeNetworkService network_service_;
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
  cloud_provider_.reset();
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

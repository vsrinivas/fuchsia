// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/cloud_provider_firebase/cloud_provider_impl.h"

#include "apps/ledger/services/cloud_provider/cloud_provider.fidl.h"
#include "apps/ledger/src/auth_provider/test/test_auth_provider.h"
#include "apps/ledger/src/test/fake_token_provider.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "garnet/public/lib/fidl/cpp/bindings/binding.h"
#include "garnet/public/lib/fxl/macros.h"

namespace cloud_provider_firebase {

namespace {
ConfigPtr GetFirebaseConfig() {
  auto config = Config::New();
  config->server_id = "abc";
  config->api_key = "xyz";
  return config;
}

std::unique_ptr<auth_provider::AuthProvider> InitAuthProvider(
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    auth_provider::test::TestAuthProvider** ptr) {
  auto auth_provider = std::make_unique<auth_provider::test::TestAuthProvider>(
      std::move(task_runner));
  *ptr = auth_provider.get();
  return auth_provider;
}

}  // namespace

class CloudProviderImplTest : public test::TestWithMessageLoop {
 public:
  CloudProviderImplTest()
      : cloud_provider_impl_(
            message_loop_.task_runner(),
            "user_id",
            GetFirebaseConfig(),
            InitAuthProvider(message_loop_.task_runner(), &auth_provider_),
            cloud_provider_.NewRequest()) {}
  ~CloudProviderImplTest() override {}

 protected:
  auth_provider::test::TestAuthProvider* auth_provider_ = nullptr;

  cloud_provider::CloudProviderPtr cloud_provider_;
  CloudProviderImpl cloud_provider_impl_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(CloudProviderImplTest);
};

TEST_F(CloudProviderImplTest, EmptyWhenDisconnected) {
  bool on_empty_called = false;
  cloud_provider_impl_.set_on_empty([this, &on_empty_called] {
    on_empty_called = true;
    message_loop_.PostQuitTask();
  });
  cloud_provider_.reset();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(on_empty_called);
}

}  // namespace cloud_provider_firebase

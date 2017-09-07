// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/cloud_provider_firebase/cloud_provider_impl.h"

#include "apps/ledger/services/cloud_provider/cloud_provider.fidl.h"
#include "apps/ledger/src/auth_provider/test/test_auth_provider.h"
#include "apps/ledger/src/test/fake_token_provider.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "garnet/public/lib/fidl/cpp/bindings/binding.h"
#include "garnet/public/lib/ftl/macros.h"

namespace cloud_provider_firebase {

namespace {
ConfigPtr GetFirebaseConfig() {
  auto config = Config::New();
  config->server_id = "abc";
  config->api_key = "xyz";
  return config;
}

}  // namespace

class CloudProviderImplTest : public test::TestWithMessageLoop {
 public:
  CloudProviderImplTest()
      : fake_token_provider_("id_token", "local_id", "email", "client_id"),
        token_provider_binding_(&fake_token_provider_,
                                token_provider_.NewRequest()),
        cloud_provider_impl_(message_loop_.task_runner(),
                             GetFirebaseConfig(),
                             std::move(token_provider_),
                             cloud_provider_.NewRequest()) {}
  ~CloudProviderImplTest() override {}

 protected:
  modular::auth::TokenProviderPtr token_provider_;
  test::FakeTokenProvider fake_token_provider_;
  fidl::Binding<modular::auth::TokenProvider> token_provider_binding_;

  cloud_provider::CloudProviderPtr cloud_provider_;
  CloudProviderImpl cloud_provider_impl_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(CloudProviderImplTest);
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

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/factory_impl.h"

#include "peridot/bin/cloud_provider_firebase/fidl/factory.fidl.h"
#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "peridot/bin/ledger/callback/capture.h"
#include "peridot/bin/ledger/network/fake_network_service.h"
#include "peridot/bin/ledger/test/fake_token_provider.h"
#include "peridot/bin/ledger/test/test_with_message_loop.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/time/time_delta.h"

namespace cloud_provider_firebase {
namespace {

ConfigPtr GetFirebaseConfig() {
  auto config = Config::New();
  config->server_id = "abc";
  config->api_key = "xyz";
  return config;
}

class FactoryImplTest : public test::TestWithMessageLoop {
 public:
  FactoryImplTest()
      : fake_token_provider_("id_token", "local_id", "email", "client_id"),
        token_provider_binding_(&fake_token_provider_,
                                token_provider_.NewRequest()),
        network_service_(message_loop_.task_runner()),
        factory_impl_(message_loop_.task_runner(), &network_service_),
        factory_binding_(&factory_impl_, factory_.NewRequest()) {}
  ~FactoryImplTest() override {}

 protected:
  modular::auth::TokenProviderPtr token_provider_;
  test::FakeTokenProvider fake_token_provider_;
  fidl::Binding<modular::auth::TokenProvider> token_provider_binding_;

  ledger::FakeNetworkService network_service_;
  FactoryImpl factory_impl_;
  cloud_provider_firebase::FactoryPtr factory_;
  fidl::Binding<cloud_provider_firebase::Factory> factory_binding_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(FactoryImplTest);
};

TEST_F(FactoryImplTest, EmptyWhenNoCloudProviders) {
  bool on_empty_called = false;
  factory_impl_.set_on_empty([this, &on_empty_called] {
    on_empty_called = true;
    message_loop_.PostQuitTask();
  });

  cloud_provider::CloudProviderPtr cloud_provider;
  cloud_provider::Status status;

  factory_->GetCloudProvider(GetFirebaseConfig(), std::move(token_provider_),
                             cloud_provider.NewRequest(),
                             callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_provider::Status::OK, status);

  // Close the client connection to cloud provider and verify that the factory's
  // on_empty callback is called.
  cloud_provider.reset();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(on_empty_called);
}

TEST_F(FactoryImplTest, EmptyWhenTokenProviderDisconnects) {
  bool on_empty_called = false;
  factory_impl_.set_on_empty([this, &on_empty_called] {
    on_empty_called = true;
    message_loop_.PostQuitTask();
  });

  cloud_provider::CloudProviderPtr cloud_provider;
  cloud_provider::Status status;

  factory_->GetCloudProvider(GetFirebaseConfig(), std::move(token_provider_),
                             cloud_provider.NewRequest(),
                             callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_provider::Status::OK, status);

  token_provider_binding_.Unbind();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(on_empty_called);
}

}  // namespace
}  // namespace cloud_provider_firebase

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/identity/bin/google_auth_provider/factory_impl.h"

#include "lib/callback/capture.h"
#include "lib/callback/set_when_called.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/gtest/test_loop_fixture.h"
#include "lib/network_wrapper/fake_network_wrapper.h"
#include "src/identity/bin/google_auth_provider/settings.h"
#include "src/lib/fxl/macros.h"

namespace google_auth_provider {

class GoogleFactoryImplTest : public gtest::TestLoopFixture {
 public:
  GoogleFactoryImplTest()
      : network_wrapper_(dispatcher()),
        context_(sys::ComponentContext::Create().get()),
        factory_impl_(dispatcher(), context_, &network_wrapper_, {}) {
    factory_impl_.Bind(factory_.NewRequest());
  }

  ~GoogleFactoryImplTest() override {}

 protected:
  network_wrapper::FakeNetworkWrapper network_wrapper_;
  sys::ComponentContext* context_;
  fuchsia::auth::AuthProviderPtr auth_provider_;
  fuchsia::auth::AuthProviderFactoryPtr factory_;

  FactoryImpl factory_impl_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(GoogleFactoryImplTest);
};

TEST_F(GoogleFactoryImplTest, GetAuthProvider) {
  fuchsia::auth::AuthProviderStatus status;
  auth_provider_.Unbind();
  bool callback_called = false;
  factory_->GetAuthProvider(
      auth_provider_.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(fuchsia::auth::AuthProviderStatus::OK, status);
}

}  // namespace google_auth_provider

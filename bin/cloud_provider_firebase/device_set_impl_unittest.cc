// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/cloud_provider_firebase/device_set_impl.h"

#include "apps/ledger/services/cloud_provider/cloud_provider.fidl.h"
#include "apps/ledger/src/auth_provider/test/test_auth_provider.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "garnet/public/lib/fidl/cpp/bindings/binding.h"
#include "garnet/public/lib/fxl/macros.h"

namespace cloud_provider_firebase {

class DeviceSetImplTest : public test::TestWithMessageLoop {
 public:
  DeviceSetImplTest()
      : auth_provider_(message_loop_.task_runner()),
        device_set_impl_(&auth_provider_, device_set_.NewRequest()) {}

  ~DeviceSetImplTest() override {}

 protected:
  auth_provider::test::TestAuthProvider auth_provider_;
  cloud_provider::DeviceSetPtr device_set_;
  DeviceSetImpl device_set_impl_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(DeviceSetImplTest);
};

TEST_F(DeviceSetImplTest, EmptyWhenDisconnected) {
  bool on_empty_called = false;
  device_set_impl_.set_on_empty([this, &on_empty_called] {
    on_empty_called = true;
    message_loop_.PostQuitTask();
  });
  device_set_.reset();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(on_empty_called);
}

}  // namespace cloud_provider_firebase

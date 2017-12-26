// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/app/device_set_impl.h"

#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"
#include "peridot/lib/gtest/test_with_message_loop.h"

namespace cloud_provider_firestore {

class DeviceSetImplTest : public gtest::TestWithMessageLoop {
 public:
  DeviceSetImplTest() : device_set_impl_(device_set_.NewRequest()) {}

  ~DeviceSetImplTest() override {}

 protected:
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

}  // namespace cloud_provider_firestore

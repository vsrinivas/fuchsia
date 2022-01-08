// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ethertap.h"

#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"

class EthertapTests : public zxtest::Test {
 public:
  EthertapTests() {
    ASSERT_OK(eth::TapCtl::Create(nullptr, fake_parent_.get()));
    ASSERT_EQ(fake_parent_->child_count(), 1);
    tap_ctl_ = fake_parent_->GetLatestChild()->GetDeviceContext<eth::TapCtl>();
  }

  void OpenDevice(const char* name) {
    fuchsia_hardware_ethertap::wire::Config config = {
        .options = 0, .features = 0, .mtu = 1500, .mac = {1, 2, 3, 4, 5, 6}};
    auto tap = fidl::CreateEndpoints(&requester_side_);
    ASSERT_TRUE(tap.is_ok());
    ASSERT_OK(tap_ctl_->OpenDeviceInternal(name, config, std::move(tap.value())));
    ASSERT_EQ(tap_ctl_->zxdev()->child_count(), 1);
  }

  zx_device_t* tap_device() { return tap_ctl_->zxdev()->GetLatestChild(); }

 protected:
  std::shared_ptr<MockDevice> fake_parent_ = MockDevice::FakeRootParent();
  eth::TapCtl* tap_ctl_;
  // The fixture holds onto the requester side of the channel so the channel is not closed
  // at the end of OpenDevice.
  fidl::ClientEnd<fuchsia_hardware_ethertap::TapDevice> requester_side_;
};

TEST_F(EthertapTests, TestLongNameMatches) {
  const char* long_name = "012345678901234567890123456789";
  ASSERT_EQ(strlen(long_name), fuchsia_hardware_ethertap::wire::kMaxNameLength);

  ASSERT_NO_FATAL_FAILURE(OpenDevice(long_name));
  ASSERT_STREQ(long_name, tap_device()->name());
}

TEST_F(EthertapTests, TestShortNameMatches) {
  const char* short_name = "abc";
  ASSERT_NO_FATAL_FAILURE(OpenDevice(short_name));
  ASSERT_STREQ(short_name, tap_device()->name());
}

// This tests triggering the unbind hook via DdkAsyncRemove and verifying the unbind reply occurs.
TEST_F(EthertapTests, UnbindSignalsWorkerThread) {
  ASSERT_NO_FATAL_FAILURE(OpenDevice(""));
  // This should run the device unbind hook, which signals the worker thread to reply to the
  // unbind txn and exit.

  tap_device()->UnbindOp();
  tap_device()->WaitUntilUnbindReplyCalled();
  ASSERT_OK(tap_device()->UnbindReplyCallStatus());
}

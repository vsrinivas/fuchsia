// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hidctl.h"

#include <lib/fake_ddk/fake_ddk.h>

#include <zxtest/zxtest.h>

namespace hidctl {

TEST(HidctlTest, DdkLifecycle) {
  fake_ddk::Bind ddk;

  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  const fuchsia_hardware_hidctl::wire::HidCtlConfig config = {};
  fbl::Array<const uint8_t> report_desc;

  auto hiddev = std::unique_ptr<hidctl::HidDevice>(new hidctl::HidDevice(
      fake_ddk::kFakeParent, config, std::move(report_desc), std::move(local)));
  ASSERT_OK(hiddev->DdkAdd("hidctl-dev"));

  hiddev->DdkAsyncRemove();
  ASSERT_OK(ddk.WaitUntilRemove());
  ASSERT_TRUE(ddk.Ok());
}

// Tests that the device is removed if the worker thread exits on error.
TEST(HidctlTest, DdkLifecycleWorkerThreadExit) {
  fake_ddk::Bind ddk;

  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  const fuchsia_hardware_hidctl::wire::HidCtlConfig config = {};
  fbl::Array<const uint8_t> report_desc;

  auto hiddev = std::unique_ptr<hidctl::HidDevice>(new hidctl::HidDevice(
      fake_ddk::kFakeParent, config, std::move(report_desc), std::move(local)));
  ASSERT_OK(hiddev->DdkAdd("hidctl-dev"));

  // This should cause the worker thread to exit and call DdkAsyncRemove() on the device.
  remote.reset();

  ASSERT_OK(ddk.WaitUntilRemove());
  ASSERT_TRUE(ddk.Ok());
}

}  // namespace hidctl

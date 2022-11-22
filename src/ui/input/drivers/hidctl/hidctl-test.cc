// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hidctl.h"

#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"

namespace hidctl {

TEST(HidctlTest, DdkLifecycle) {
  auto fake_parent = MockDevice::FakeRootParent();
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  const fuchsia_hardware_hidctl::wire::HidCtlConfig config = {};
  fbl::Array<const uint8_t> report_desc;

  auto hiddev = std::unique_ptr<hidctl::HidDevice>(
      new hidctl::HidDevice(fake_parent.get(), config, std::move(report_desc), std::move(local)));
  ASSERT_OK(hiddev->DdkAdd("hidctl-dev"));
  hiddev.release();

  auto* child = fake_parent->GetLatestChild();
  child->InitOp();
  child->WaitUntilInitReplyCalled();

  child->UnbindOp();
  child->WaitUntilUnbindReplyCalled();
  mock_ddk::ReleaseFlaggedDevices(fake_parent.get());
}

// Tests that the device is removed if the worker thread exits on error.
TEST(HidctlTest, DdkLifecycleWorkerThreadExit) {
  auto fake_parent = MockDevice::FakeRootParent();

  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  const fuchsia_hardware_hidctl::wire::HidCtlConfig config = {};
  fbl::Array<const uint8_t> report_desc;

  auto hiddev = std::unique_ptr<hidctl::HidDevice>(
      new hidctl::HidDevice(fake_parent.get(), config, std::move(report_desc), std::move(local)));
  ASSERT_OK(hiddev->DdkAdd("hidctl-dev"));
  hiddev.release();

  auto* child = fake_parent->GetLatestChild();
  child->InitOp();
  child->WaitUntilInitReplyCalled();

  // This should cause the worker thread to exit and call DdkAsyncRemove() on the device.
  remote.reset();

  child->WaitUntilAsyncRemoveCalled();
  child->UnbindOp();
  child->WaitUntilUnbindReplyCalled();
  mock_ddk::ReleaseFlaggedDevices(fake_parent.get());
}

}  // namespace hidctl

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zx/vmo.h>

#include <ddktl/device.h>
#include <zxtest/zxtest.h>

TEST(FakeDdk, InspectVmoLeak) {
  fake_ddk::Bind bind;

  zx::vmo inspect_vmo;
  ASSERT_OK(zx::vmo::create(4096u, 0, &inspect_vmo));

  zx::vmo dup_vmo;
  ASSERT_OK(inspect_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_vmo));

  ddk::DeviceAddArgs args("test-driver");
  args.set_inspect_vmo(std::move(dup_vmo));
  device_add_args_t device_args = args.get();

  zx_device_t* device;
  EXPECT_OK(device_add(fake_ddk::kFakeParent, &device_args, &device));

  device_async_remove(device);
  EXPECT_TRUE(bind.Ok());

  zx_info_handle_count_t count;
  ASSERT_OK(inspect_vmo.get_info(ZX_INFO_HANDLE_COUNT, &count, sizeof(count), nullptr, nullptr));

  // |inspect_vmo| should be the only handle.
  EXPECT_EQ(1u, count.handle_count);
}

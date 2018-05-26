// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_queue_waiter.h"

#include <lib/async-testutils/test_loop.h>

#include "garnet/lib/machina/virtio_device_fake.h"
#include "gtest/gtest.h"

namespace machina {
namespace {

TEST(VirtioQueueWaiter, Wait) {
  async::TestLoop loop;

  bool wait_complete = false;
  VirtioDeviceFake device;
  VirtioQueueWaiter waiter(loop.async(), device.queue(),
                           [&](zx_status_t status, uint32_t events) {
                             EXPECT_EQ(ZX_OK, status);
                             wait_complete = true;
                           });

  ASSERT_EQ(device.Init(), ZX_OK);
  EXPECT_EQ(ZX_OK, waiter.Begin());

  loop.RunUntilIdle();
  EXPECT_FALSE(wait_complete);

  // Signal without a descriptor should not invoke the wait callback.
  device.queue()->Signal();
  loop.RunUntilIdle();
  EXPECT_FALSE(wait_complete);

  // Add a descriptor and signal again. This should invoke the waiter.
  uint8_t buf[8];
  ASSERT_EQ(device.queue_fake()
                .BuildDescriptor()
                .AppendReadable(buf, sizeof(buf))
                .Build(),
            ZX_OK);
  device.queue()->Signal();
  loop.RunUntilIdle();
  EXPECT_TRUE(wait_complete);
}

}  // namespace
}  // namespace machina

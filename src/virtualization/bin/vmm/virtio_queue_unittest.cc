// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/virtualization/bin/vmm/phys_mem_fake.h"
#include "src/virtualization/bin/vmm/virtio_device.h"
#include "src/virtualization/bin/vmm/virtio_device_fake.h"
#include "src/virtualization/bin/vmm/virtio_queue_fake.h"

namespace {

TEST(VirtioQueueTest, HandleOverflow) {
  VirtioDeviceFake device;
  VirtioQueue* queue = device.queue();
  VirtioQueueFake* queue_fake = device.queue_fake();

  // Setup queue pointers so that the next descriptor will wrap avail->idx
  // to 0.
  VirtioRing* ring = queue_fake->ring();
  const_cast<uint16_t&>(ring->avail->idx) = UINT16_MAX;
  ring->index = UINT16_MAX;

  uint16_t expected_desc;
  uint32_t data = 0x12345678;
  ASSERT_EQ(queue_fake->BuildDescriptor()
                .AppendReadable(&data, sizeof(data))
                .Build(&expected_desc),
            ZX_OK);

  uint16_t desc;
  ASSERT_EQ(queue->NextAvail(&desc), ZX_OK);
  ASSERT_EQ(desc, expected_desc);
  ASSERT_EQ(queue_fake->ring()->avail->idx, 0);
  ASSERT_EQ(queue_fake->ring()->index, 0);
}

}  // namespace

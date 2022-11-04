// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/nvme/queue.h"

#include <lib/ddk/driver.h>
#include <lib/fake-bti/bti.h>
#include <zircon/syscalls.h>

#include <zxtest/zxtest.h>

#include "src/devices/block/drivers/nvme/nvme_bind.h"

namespace nvme {

constexpr uint32_t kQueueMagic = 0xabbacaba;

class QueueTest : public zxtest::Test {
 public:
  void SetUp() { ASSERT_OK(fake_bti_create(fake_bti_.reset_and_get_address())); }

 protected:
  zx::bti fake_bti_;
};

TEST_F(QueueTest, CappedToPageSize) {
  auto queue = Queue::Create(fake_bti_.borrow(), /*queue_id=*/1, /*max_entries=*/100,
                             /*entry_size=*/zx_system_get_page_size());
  ASSERT_OK(queue.status_value());
  ASSERT_EQ(1, queue->entry_count());
}

TEST_F(QueueTest, WrapsAround) {
  // Create a queue with two elements.
  auto queue = Queue::Create(fake_bti_.borrow(), /*queue_id=*/1, /*max_entries=*/100,
                             /*entry_size=*/zx_system_get_page_size() / 2);
  ASSERT_OK(queue.status_value());

  // To start with, the next item in the queue should be the first item in the queue.
  ASSERT_EQ(0, queue->NextIndex());
  // Set the first item in the queue to |kQueueMagic| and move forward.
  *static_cast<uint32_t*>(queue->Next()) = kQueueMagic;
  // The next index in the queue should now be 1 (the second item).
  ASSERT_EQ(1, queue->NextIndex());
  // Set the second item in the queue to 0 and move forward.
  *static_cast<uint32_t*>(queue->Next()) = 0;
  // We should have wrapped around to the start of the queue.
  ASSERT_EQ(0, queue->NextIndex());
  // Check that the first item in the queue is still |kQueueMagic|.
  ASSERT_EQ(*static_cast<uint32_t*>(queue->Next()), kQueueMagic);
}

TEST_F(QueueTest, CappedToMaxEntries) {
  auto queue =
      Queue::Create(fake_bti_.borrow(), /*queue_id=*/1, /*max_entries=*/100, /*entry_size=*/1);
  ASSERT_OK(queue.status_value());
  ASSERT_EQ(100, queue->entry_count());
}

TEST_F(QueueTest, PhysicalAddress) {
  auto queue =
      Queue::Create(fake_bti_.borrow(), /*queue_id=*/1, /*max_entries=*/100, /*entry_size=*/1);
  ASSERT_OK(queue.status_value());
  ASSERT_EQ(FAKE_BTI_PHYS_ADDR, queue->GetDeviceAddress());
}

}  // namespace nvme

static zx_driver_ops_t stub_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  return ops;
}();

ZIRCON_DRIVER(fake_driver, stub_driver_ops, "zircon", "0.1");
